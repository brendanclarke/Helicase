# Session 096 — Tetrakt Instrument, Instrument Fixes, Colloid/Tetrakt MIDI Extensions

```
DATE: 2026-11-15 / 2026-11-20
SESSION GOAL: Implement the Tetrakt four-layer sample player (P1), six instrument fixes (P2),
              and the Colloid/Tetrakt MIDI extension set — gate, pitch envelope, noise, wavetable
              scan, MIDI note-mode module, paraphonic, channel pressure Learn, Tetrakt layers (P3).
COMPLETED: All three phases landed. Host tests green. Device cycle probes run for Tetrakt (P1)
           and Colloid Basic-share acceptance (P3). Assessment found 11 items; 7 fixed, 2 accepted,
           1 open (CPU F1 — resolved by Basic-share optimisation pass), 1 withdrawn.
VERIFIED ON HARDWARE: Yes — Tetrakt cycle probe (P1 §18, three runs with §19 fused read);
                      Colloid Basic-share cycle probe (P3 third run, all four modes fit);
                      listening check (P3 B5 filter steps, B10 oscillator/wts audit, B11 spread).
```

---

## Source Documents

This log retains all relevant detail from the following documents, which are deleted after this log is written:

- `S096_P1_TETRAKT_SAMPLE_PLAYER.md` — Tetrakt design (revision 7), all questions decided
- `S096_P1_IMPLEMENTATION.md` — P1 implementation schedule (SD1–SD15, steps S0–S12, §17–§19)
- `S096_P2_INSTRUMENT_FIXES.md` — Six instrument fixes (F1–F6)
- `S096_P3_COLLOID_TETRAKT_INST_MIDI.md` — P3 plan, all decisions settled (§1–§9)
- `S096_P3_IMPLEMENTATION.md` — P3 implementation (P3a–P3f, assessment, fixes, Basic-share work)

---

## Phase P1 — Tetrakt Four-Layer Sample Player

### Overview

The Tetrakt is the 7th instrument type, Basic class. Token `tkt`, extension `.tkt`, folder `/Instrument/Tetrakt/`. It is a four-layer sample player: each layer independently reads one user sample from the flash catalogue, shapes it through a per-layer processing chain, and the four layers sum into a mono voice that is then widened by an envelope-derived allpass (W2) and optionally sent to a 50 ms space stage (S1). The voice occupies one instrument slot and uses one FxBuffer voice unit for the S1 delay line.

### Files Created

| File | Purpose |
|------|---------|
| `Core/DSP/Instruments/Tetrakt/TetraktVoice.h` | Voice struct (1,064 B), layer struct, constants, entry points |
| `Core/DSP/Instruments/Tetrakt/TetraktVoice.c` | Async (control rate) and sync (audio rate) voice processing, fused sample read |
| `Core/DSP/Instruments/Tetrakt/TetraktParameters.h` | Registry exports, 64 descriptor rows, velocity-group helpers |
| `Core/DSP/Instruments/Tetrakt/TetraktParameters.c` | Parameter table (64 rows in page order), menu layout (8 sub-pages), velocity group lookup |
| `Core/DSP/Instruments/Tetrakt/TetraktTables.h` | Table declarations (decay, drive, SRR, tone coefficients) |
| `Core/DSP/Instruments/Tetrakt/TetraktTables.c` | Generated tables from `gen_tkt_tables.py` |
| `tools/tetrakt/gen_tkt_tables.py` | Table generator (decay curve, drive curve, SRR rate, tone SVF coefficients) |
| `tools/tetrakt/make_init_tkt.py` | Init file generator (matches `Tetrakt_initVoice()` defaults) |
| `SD_CARD/Instrument/Tetrakt/init.tkt` | Default instrument preset on the SD card |
| `tools/dsp_test/modules/tetrakt.c` | Host test module: 9 law groups, 6 break builds |
| `tools/dsp_test/tetrakt_cpu.py` | CPU instruction counter |

### Layer Chain (per layer, per sample)

```
read → decay × velocity → drive (tanh) → SRR (sample-rate reduction) → tone (2-pole bipolar SVF) → level → pan/send
```

- **Read**: fused into the post loop (§19). `osc_userBind()` for the bind; the per-sample read is the voice's own: `s = (a << 12) + (ph & 4095)(b − a)`, exact in integers; phase advanced by `phaseInc`, clamped to end. `bindGain` from the catalogue entry, read once per bind. Guard: only `OSC_BIND_SMP` with data reads; anything else reads silence.
- **Decay**: `colEgK[]` exponential envelope (shared with Colloid), one-block fade-to-zero on retrigger (R1 stages: IDLE/PLAY/FADE/START).
- **Velocity**: per-layer latched velocity, applied per TKT_KIND_* group (9 groups × 4 layers). Amount from the platform's velocity modulator node.
- **Drive**: tanh soft clip (`tktDriveK[]` table), DC blocker.
- **SRR**: sample-rate reduction via hold counter (`tktSrrRate[]` table, 0..127 → 1.0..31.75 samples per step).
- **Tone**: 2-pole bipolar SVF. Cutoff from `tktToneB1[]`/`tktToneB2[]` tables (120 Hz .. 3 kHz, bipolar ±63 → LP..HP). Envelope: `toneEnv` modulates cutoff by decay envelope level.
- **Level**: linear gain from row value.
- **Pan/Send**: per-layer pan into L/R bus; per-layer send amount into the space stage's feed.

### Trigger and On/Off Rules

- **Layer switch `lon`**: one 4-bit row (index 20) with 16 named stops. Shown as four on/off bit cells on the MOD page. Each trigger restarts the layers whose bits are set; clear layers continue undisturbed.
- **R1 fade-then-start**: a layer still sounding when retriggered enters FADE (one-block linear fade to exactly 0.0), then START (re-arm) on the next block. A fresh voice starts directly.
- **No step length**: Tetrakt layers are one-shot; they decay naturally. No `sus`/gate interaction.

### Space Stage

- **S1 delay line**: 50 ms (2,208 × int16) in one FxBuffer voice unit. Two taps: left at `tapL` = `stm × 50 ms`, right at `tapL × (1 − sdt × 0.5)` with ±0.15 sinusoidal detune wobble. Feedback `sfb` 0..0.97 (`TKT_FB_MAX`). Allpass `sto` modulated tone.
- **W2 envelope-derived width**: allpass coefficient derived from the master decay envelope. At `wid` 0 the voice is dry mono panned; at `wid` 127 the allpass disperses fully.
- **Constant-CPU rule**: every stage runs every block regardless of parameter settings.

### Parameter Table (64 of 64 rows)

8 sub-pages (OSC, AEG, MOD, FM, Click, FILTER, LFO, MIX):
- OSC: `smA..smD` (sample selectors), `tuA..tuD` (tune ±63 semitones)
- AEG: `lvA..lvD` (level), `dyA..dyD` (decay), `pnA..pnD` (pan)
- MOD: `onA..onD` (bit cells of `lon`), `dst`/`amt`/`vel` (velocity)
- FM: `drA..drD` (drive), `srA..srD` (SRR)
- Click: `spA..spD` (space send), `stm`/`sfb`/`sdt`/`sto` (space stage)
- FILTER: `tnA..tnD` (tone cutoff), `teA..teD` (tone envelope depth)
- LFO: the platform's LFO pair (the Drum/Colloid layout)
- MIX: `vol`/`pan`/`wid`/`slv` (voice volume, L-R balance, width, space level)

### Schedule Decisions (SD1–SD15)

| SD | Decision |
|----|----------|
| SD1 | Bit cells: page cell code 0xF0+b is "bit b of the type's switch row" |
| SD2 | Bit cell display/edit: dtype is plain on/off; commit writes the whole row mask |
| SD3 | Velocity groups: each of 9 groups names all four layers by its layer-A row; `*` suffix in display |
| SD4 | All rows bind TYPE_UINT8 bytes (the mixer's vol is TYPE_FLT as every engine) |
| SD5 | The sample read uses `osc_userBind()`; the read itself is the voice's own fused post-loop read (§19) |
| SD6 | The decay uses `colEgK[]` (shared with Colloid) |
| SD7 | The trigger is `Tetrakt_triggerVoice()` with per-layer mask |
| SD8 | The tone filter is a bipolar SVF with envelope |
| SD9 | The space is S1 (delay) + W2 (width) in one FxBuffer voice unit |
| SD10 | Quiet detection: all four layers idle |
| SD11 | Voice unit ownership: claimed at type assignment, released at type change |
| SD12 | The init file carries the first Tetrakt with `make_init_tkt.py` |
| SD13 | Makefile: `-Ofast -fno-lto` DSP recipes (outside LTO to protect other engines' loop partitioning) |
| SD14 | The P4 baseline check (`elf_p4_check`) gates LTO shifts |
| SD15 | The host test has 9 laws and 6 break builds |

### InstrumentManager Integration

- `INSTRUMENT_TYPE_TKT` = 6 (appended before UNKNOWN, which moves to 7)
- Registry row: token `"tkt"`, label `"Tetrakt"`, extension `".tkt"`, folder `"Tetrakt"`, Basic flags, 64 descriptors, 8 pages, switch row `lon` at index 20+1
- Tagged union: `TetraktVoice tetrakt` in `runtime_slots[]`
- `instrumentManager_tetraktRuntime()`, `instrumentManager_slotVoiceUnits()` (SD11)
- Stereo right channel via `instrumentManager_slotRight()` (the voice's `right[32]`)
- Wave domain max: `tkt_sample_*` rows clamp to installed sample count
- Velocity: `tetraktParameters_velocityKind()`/`velocityTokenHidden()` for the walker and validator

### §17 Space Stage Implementation

The space stage (`tkt_spaceOut()`) reads and writes the FxBuffer voice unit's 2,208-sample int16 delay line. Two taps with Hermite interpolation; feedback allpass; write position advances by block size. The wobble is a 32-step phase accumulator with a 32-entry cosine table. Runs every block (constant-CPU). ITCM +80 B for the space function (15,312 B total at §17).

### §19 Fused Sample Read

Each layer's sample read folded into its post loop, eliminating the four `osc_renderTyped()` dispatch calls and the shared int16 scratch buffer:
- Guard: only `OSC_BIND_SMP` with data reads; unbound layers are silent
- Per sample: `s = (a << 12) + (ph & 4095)(b − a)`, exact in integers (|s| < 2^29)
- Gain: `x = s × g` where `g` carries `bindGain / 4096`
- Phase write-back after the loop
- Result: −44 instructions per sample across four layers; worst block 422.4 → 382.7 instructions
- ITCM returned to 15,232 B (LTO dropped `osc_renderTyped()` section)
- Work area reduced 448 → 384 B
- Voice struct grew +16 B (1,048 → 1,064 B, `bindGain` per layer)
- New law TK-READ validates the fused read against `osc_smpPlainBlock()`

### Device Probe Results (§18)

| Resource | Value | Limit | Margin |
|----------|-------|-------|--------|
| CPU mean (one voice) | 20,442 cycles/block | 19,620 (Basic) | −822 |
| CPU median | 20,386 | 19,620 | −766 |
| CPU highest window | 21,893 | 19,620 | −2,273 |
| CPU last 100 windows (final settings) | 19,995 | 19,620 | −375 |
| Whole mixer highest block | 159,542 (101.8%) | 156,705 | overrun |
| Voice struct | 1,064 B | 1,264 B slot reserve | 200 B |
| Shared work area | 384 B .bss | approved | — |
| ITCM (after §19) | 15,232 B | 16,384 B | 1,152 B |

Cycles per instruction: 1.52 (1.41..1.63). The four simultaneous flash reads from internal flash (`0x080C0000`) cause higher and more variable ratio than the Colloid's 1.37..1.41. Render variation correlated with SRR rate changes (the SRR hold branch `blt` in the compiled code).

### Host Test Laws

| Law | Tests |
|-----|-------|
| TK-TRIG | Every `lon` 0..15: set layers restart, clear ones continue; FADE/START stages; fresh voice |
| TK-DEC | Decay monotone non-increasing per block, reaches IDLE below 1e-4 |
| TK-TUNE | tuA 63 at note 63: phaseInc 4,096 exactly; semitone law; clamp at extremes |
| TK-SRR | srA 127: exact match to SRR-free; srA 64: hold changes at law's sample count ±1 |
| TK-TONE | Bypass exact at tn 63 / te 63; no output step > input step + 1e-3; LP/HP -3 dB within 10% |
| TK-DRV | Exact dry at dr 0; zero input → zero output; DC blocker mean < 1e-4 at 100 Hz |
| TK-W2 | wid 0: busR equals y × gR exactly; wid 127 at env 1: same |
| TK-S1 | Tap delay ±1 sample; detune offset law; feedback decay bounded |
| TK-VEL | Per-layer latched velocity; group applies all four; retrigger of one leaves others |

Break builds: `tetrakt_break_fade`, `tetrakt_break_bias`, `tetrakt_break_srr`, `tetrakt_break_guard`, `tetrakt_break_gain`, `tetrakt_break_frac`.

---

## Phase P2 — Instrument Fixes

Six targeted fixes, all in code:

### F1: Delay/Echo Heads Linear Interpolation

Extra delay heads (the `hed` row) now use linear interpolation instead of nearest-sample. +57 instructions per frame for Delay, +17 for Echo. New caps: Delay 252.0 / Echo 210.5 instructions per sample.

### F2: Colloid Filter Envelope Depth

Filter envelope depth expanded from 72 to 115.7 semitones (the full ADSR range). The `eno` row now reaches ±57.85 semitones at the extremes.

### F3: UBP and Off Filter Types

Two new Colloid filter types: UBP (unity-gain bandpass, `a2 × HP`) and off (bypass). `COL_FT_COUNT` grows from 5 to 7. `colFiltTypeNames` updated.

### F4: Tetrakt Spatializer Ranges

Expanded after the first listening pass: `TKT_FB_MAX` → 0.97 (was 0.95), `TKT_SDT_SPAN` → 0.5 (was 0.3), `TKT_BUS_AP` → −0.6 (was −0.5).

### F5: Colloid PWM Gliding Fix

PWM Saw/Rec oscillators reduced from 8 to 6 with gliding second read and warp:
- The second read pointer per oscillator (`pwD`) now glides toward its target per block (the S095 pitch slew's law)
- The warp pair loops (`col_pwmWarpPairStore/Add`) gain glide support
- ITCM grows +136 B (15,368 B at P2 end)
- Result: smoother PWM position changes; 6 oscillators fit the cycle budget

### F6: Compressor Sidechain Duck Easing

Attack envelope now has a 0–10 ms easing curve (was instant), reducing pump artifacts on ducked signals.

---

## Phase P3 — Colloid/Tetrakt MIDI Extensions

### P3a — C1: Colloid Gate

A real note gate based on step length, length special, sustain, Scene/track change.

**Core changes:**
- `ColloidVoice.h/.c`: `gateLeft` (samples remaining in the gate window), `susBreak` (sustain latch), `midiHold` (MIDI hold state), `changeArm`/`susSeen`/`pitchEg` state. `col_egStep(held, isAmp)` replaces the old sustain logic: `held` = gate open OR amp in attack (the held-FP rule). The filter and pitch envelopes use the same `heldFP`.
- `Colloid_triggerVoice(stepSamples, first)`: receives the step's gate window in samples and the first-step flag.
- Gate entry points: `gateRelease()`, `gateRenew()`, `setMidiHold()`, `changeArm()`, `changeDecide()`, `noteSustainWrite()`.
- `InstrumentManager`: `instrumentManager_gateRelease[6]` (cleared at both reset sites), consumed in `calcSlotAsync()`. A Colloid's `requestGateDuck()` is a release; `requestGateRelease()`, `renewGate()`, `changeArm()`, `changeDecide()`, `automationWritten()`.
- `ColloidParameters`: `COL_IDX_COARSE`, `COL_IDX_SUSTAIN`, `COL_IDX_INTERVAL_1` with static asserts and `colloidParameters_isSustainRow()`.
- `MidiVoiceControl`: `resetFlags` reach the fire; leftover resets renew the gate; `seq_takeChangeMasks()` inside PRIMASK; arm before prepare, decide after fire.
- `sequencer.c/.h`: `seq_lenWindow[7]` (14 B), `seq_changeMask`/`seq_changeSteppedMask`/`seq_changePendingMask` (3 B), `seq_lenStart()`, `seq_markTrackChange()`, `seq_takeChangeMasks()`. Window starts on on-time and deferred plain fire, cleared by roll/reset, replaced by next trigger. Change marks at per-track Scene change, coalesce, whole-Scene select, master-boundary commit. Mute releases with `brk`. Every automation write reported.

**Tests:** CO-GATE, CO-RETRIG, CO-ROLLGATE, CO-CHANGE, SQ-LEN. Break builds: `colloid_break_atk`, `colloid_break_roll`.

### P3b — C2a: Pitch Envelope, C2b: Noise Source

**Pitch envelope (C2a):** Third ADSR envelope (`pAttack`/`pDecay`/`pSustain`/`pRelease`/`pAmount`). Range ±24 semitones (0.38 semitone per pam step). The async steps `pitchEg` with `heldFP` and folds `2^(pam × 2 × level)` into `col_oscControl()`'s multiplier (exactly 1.0 at pam 63). Nine new parameter rows appended (indices 50–58): `pat`/`pde`/`psl`/`pre` (AEG second screen), `pam` (OSC cell 6), `nlv`/`nto`/`nwd`/`npn` (MOD second screen). `make_init_col.py` and `init.col` carry the defaults: pitch 0/40/0/40, pam 63, noise 0/63/127/63.

**Noise source (C2b):** Hardware RNG per sample (`col_rngRead()` = inline `RNG->DR` read, identical to `GetRngValue()` with no call overhead). One-pole TPT HP then TPT SVF. Corners `h`/`l` closing on centre as `nwd` falls. SVF Q from √2 to 0.05. Mix: `w × LP + (1−w) × k × mk × band`. Level and pan gains into each channel's drive input. Mono route adds the mean gain once. Pan clamped to ±1 (stored 0/127 exactly full left/right). Constant-cost: the noise runs every block regardless of level.

**Cost:** +42.8 instructions per sample (pitch ≈ +2, noise ≈ +39 in the filter loop + ≈ +5.6 from `col_noiseControl()`). Register pressure in the filter loop: 25 stack accesses in the 145-instruction body (was 106 instructions, no spills).

**Tests:** CO-PENV (offset law, ±24 semitones, pam 63 bit-identical, gate releases it), CO-NOISE (level 0 bit-identical, four corners, k/mix law, pan ends, mono route).

### P3c — C2c: Wavetable Scan `wts`

New oscillator type `wts` (Colloid `typ` 3, `COL_TYP_WTS`, `COL_TYP_COUNT` 4).

**Scan loops:** `col_scanPairStore/Add` in `.itcm.s096p3` (pinned after `.itcm.s095p3` in the linker script). Two oscillators per call, each reading TWO adjacent frames blended by a Q31 `g` that glides per block (`g += dg`). Per sample: `a = col_read(A, p)`, `b = col_read(A + 1024, p)`, `s = a + ((b − a) × (g >> 16)) >> 15`. Mid/side bus adds as the plain pair.

**Position control:** `col_oscControl()` under wts: target `T = pos × (F−1) / 127 + r × k × F / 2`, triangle-folded into `[0, F−1]`. Position glides toward T at max one frame per block (top speed 1,378 frames/s). `pwD0`/`pwD` store Q15 start/end fractions (reusing the PWM arrays, 0 B new). The sync block computes `g0 = pwD0 << 16`, `g1 = pwD << 16`, `dg = (g1 − g0) >> 5`.

**Count decision:** `col_scanPairAdd` measured at 67 instructions per pair per sample (plan estimated 48–56), so the ≤52 gate for 8 oscillators fails. `COL_SCAN_COUNT` = **6** (the pre-decided fallback). `COL_NORM_SCAN` = `1 / sqrt(6)`.

**Type names:** `ONOFF_TAG_COL_TYPE 8` with Colloid-specific `colTypeNames` = wav, wtb, pwm, wts (the Alkaline keeps wav, wtb, pwm via its own tag).

**ITCM:** 15,976 B after P3c (+608 B from the two scan loops, 408 B free).

**Tests:** CO-SCAN (loop against two-frame/gliding-g law, fold at psp 1, one-frame-per-block gliding, top-frame representation, F = 1, wtb ↔ wts landing). Break build: `colloid_break_scan`.

### P3d — M: MIDI Note-Mode Module, T1: Tetrakt MIDI Layers

**MIDI note-mode module (`MidiNoteModes.h/.c`):** Owns all special MIDI note modes, resolved per message from the active Scene:
- `MNM_NORMAL`: today's single trigger (moved from `midiNotes_handle()`)
- `MNM_LAYER4`: Tetrakt track, 4 notes per pitch, each triggers one layer
- `MNM_PARA`: Colloid on own channel, paraphonic (P3f)
- `MNM_POLY`: reserved for future polyphonic mode

Fixed pools: 6 groups × (4 B header + 128 × 3 B keys) + 6 × 8 B voices + 4 B = **2,380 B SRAM1**. Groups store mode, channel, member mask, key count, and 128 keys in press order (note, velocity, owner). Voices store group linkage, flags, trigger note, fundamental, age, interval-assignment ages.

**Wide tracks (MidiNoteMap):** `wide` bit mask in settings. Rule 1 assigned range: `d = n − root; sub = d mod 4; internal = 63 + d / 4`. G stripe: wide track listed four times. Split: weighted-unit blocks (wide = 4). Inverse round-trips with sub.

**Layer triggers:** `VoiceTriggerEvent.layers` (ring 96 → 128 B). `voiceControl_noteOnLayers()`: non-dropping enqueue (earlier queued triggers of the same voice stay). `seq_triggerVoiceLayers()`: trigger jack + layer enqueue, no drop, no TIM3 TX. `Tetrakt_triggerVoice(..., layers)`: mask overrides `lon` for this trigger. `midiOut_trackLayers()`: one note-on per set layer bit (base + k), so recorded MIDI plays back the same layers.

**Overlay and LiveRecord:** `seq_liveLayers[7]` (7 B), `bh_overlay_track_t.layers` (overlay entries 5 B × 7 = 35 B). LiveRecord: `layers`, `autoCount`, `autoTarget[4]`, `autoValue[4]` on hit records; `LIVEREC_HIT_AUTO` kind; `commitHit()` writes the hit's `lon` automation (OR-merged within one step interval) then each auto entry. `liveRec_layerStep/Tick[7]` (21 B).

**RAM:** +32 B (ring), +7 B (liveLayers), +35 B (overlay), +21 B (layer step/tick). Total from P3d: +95 B beyond the pools.

**Tests:** `notemap_test` (wide laws, 200k randomised settings), `notemode_test` (all mode/layer-note cases), `voice_control` law 8 (two layer notes fire, first resets, second doesn't), `tetrakt` (mask override and `lon` fallback).

### P3e — C4: Channel Pressure Learn

**New MIDI Learn sources:** pressure, note number, velocity, with delta/absolute modes.

**Row encoding:** `space` byte bits 3..4 = source (`MIDI_LEARN_SRC_*`), bits 5..6 = mode (`MIDI_LEARN_MODE_*`). One row per (parameter, source). CC lines byte-identical to S083's format. New tokens: `prs+`/`prs−`/`prs=`, `nte+`/`nte−`/`nte=`, `vel+`/`vel−`/`vel=`.

**Capture:** `midiLearn_capturePressure()` stores mode + (positive delta) for a modulatable voice row; note/velocity never capture (they go to MidiNotes). **Apply:** `midiLearn_applySource()` unified applicator: absolute → menu edit; ± delta → `instrumentManager_deltaOverlaySet()`.

**Delta overlay system:** 72 B in InstrumentManager (`[6][4]` entries of {local, delta, base}). In `writeRuntimeInternal()`: retained writes (Morph, automation, menu) store the base and write `clamp(base + delta)`; LFO writes add delta to the passed value. `deltaOverlaySet()` takes the base from `morph_interpolation[]` or the automation-held runtime byte. Delta 0 frees and restores exactly. Cleared at slot reset.

**Recording:** `liveRec_paramValueIn()` records the effective value (base + delta) with the first-change rule and the R12 override.

**Channel pressure dispatch:** `midiIn_dispatch()` accepts `CHANNEL_PRESSURE` under RXF C. `midiControl_handlePressure()` tries capture first, then `applySource()`. Note/velocity applied by `midiLearn_applyNote()` before the note map (not consumed: the note still plays).

### P3f — C3: Paraphonic MIDI

Colloid on its own (non-G) channel with the BAR overlay off gets paraphonic note handling.

**First key (count 0):** triggers the voice at its velocity and pitch (the fundamental F = F0), automates `sus = on` via `seq_queueForegroundAutomation()`, sets `midiHold`, reports to LiveRecord with `autos = {sus 1}`.

**Later keys:** append to the group; take the first interval group (in1..in3) no held key owns, else the one assigned longest ago (round robin by `ivAge`). Automate `interval = 63 + note − F`. No trigger (envelopes continue). Report as `LIVEREC_HIT_AUTO`.

**Fundamental released:** F′ = earliest remaining key. Automate `coa = stored_coa + (F′ − F0)`. Re-express every interval as `63 + key − F′`. Free F′'s old interval group (restore to patch). Report all changed entries.

**Non-fundamental released:** its interval group returns to the patch value.

**All keys up:** automate `sus = 0`, clear `midiHold`, free the group.

**`prepareTrigger(voice)`:** re-asserts `sus 1`, `coa`, and every interval before a Pattern trigger that would otherwise reset them.

**`service()`:** revalidation per main-loop pass: if the group no longer resolves (type changed, overlay toggled, channel changed), queue `sus = 0`, clear hold, free group. On a playback-epoch change, re-queue `sus 1`.

**Foreground automation:** `seq_queueForegroundAutomation()` appends to the pending queue under PRIMASK with `SEQ_PENDING_LIVE_BIT` (the drain skips ramp starts for these entries). Drain consumes contiguous LIVE entries beyond the automation mark (ordering fix found in review).

**Tests:** `notemode_test` (mode resolution, first/later keys, round robin, fundamental hand-over, non-fundamental restore, all-up, reassert-on-change, type-change drop, six paraphonic groups).

---

## Assessment and Fixes (P3)

### Assessment Results (A1)

| Check | Result |
|-------|--------|
| Build | Clean, no warnings |
| Host tests | All pass: 25 Colloid laws, voice_control 4/7/8, sequencer, 77 break builds caught |
| SRAM1 (.bss) | +2,547 B (groups 2,328, overlay 72, voices 48, ring +32, lenWindow 14, layerTick 14, masks 7+7+7+7+6+3) |
| ColloidVoice | 1,236 B of 1,264 B slot reserve (28 B spare) |
| ITCM | 15,976 / 16,384 B (+608 B, 408 B free) |
| Flash | +12,618 B (headroom 10,968 B, below 16 KiB threshold) |
| Mixer hot loops | 58 equal, 0 grew |

### Findings and Disposition

| Finding | Severity | Status | Summary |
|---------|----------|--------|---------|
| F1 | High | Resolved by B-pass | Every Colloid mode over Basic share by host estimate |
| F2 | High | Fixed | `wts` position law: steps on upward crossings, stalls on downward |
| F3 | Medium | Fixed | Delta overlay freezes LFO on same parameter |
| F4 | Medium | Fixed | Revalidation writes old type's `sus` index into new instrument |
| F5 | Medium | Accepted | Flash headroom under 16 KiB threshold (later refactor) |
| F6 | Low | Fixed | Leftover resets renew a gate that no trigger opened |
| F7 | Low | Fixed | Noise level depends on oscillator normalisation |
| F8 | Low | Fixed | RNG address literal → `RNG->DR` |
| F9 | Low | Accepted | Paraphonic re-assert can wait one pass (rare, documented) |
| F10 | Low | Partly fixed | Test gaps (CO-SCAN sweep/descent, CO-NOISE level, 128-key fill added; spectra/IM-OVL/LR-VAL still open) |
| F11 | Low | Withdrawn | Log edits were user's own |

### Basic-Share Optimisation Pass (B1–B6, B11)

After the assessment found F1 (all modes over), six optimisations were implemented:

- **B1:** `COL_OSC_COUNT` 12 → 10 for wav/wtb (the pre-decided fallback)
- **B2:** Noise pre-pass: filtered noise written to 32-float scratch per block; filter loop reads one value per sample with one or two FMAs (−18 instr/sample estimated vs +39 inline)
- **B3:** `pwScale` computed per block instead of per sample (−69 instr/block in non-pwm modes)
- **B4:** One VDIV for both channels' overdrive (2 → 1 division per sample)
- **B5:** Stereo filter coefficients step every 8 samples (`COL_SUB_BLOCK`); 135 → 125.8 instr/sample with 16 stack accesses (listening check: inaudible)
- **B6:** Scan pair loops take one 64-word bus (67/65 → 64/63 instr/pair/sample)
- **B11:** `wts` spread was quantised to whole frames (integer `k` → float `k`)

### Device Cycle Probe — Basic-Share Acceptance (Third Run)

| Colloid mode | Windows | Median cycles | Against 19,620 | Projected | Second run |
|-------------|---------|---------------|-----------------|-----------|------------|
| wav 10 | 508 | **18,362** | −1,258 | 18,320 | 21,250 (12 osc) |
| wtb 10 | 1,247 | **18,681** | −939 | 18,870 | 21,902 (12 osc) |
| pwm 6 | 208 | **19,136** | −484 | 19,380 | 19,962 |
| wts 6 | 574 | **19,124** | −496 | 19,560 | 20,649 |

**Accepted: every Colloid mode fits the Basic share.** Measurements match or beat projections. 9 of 2,550 windows go above 19,620 by 10–146 cycles (each a 1.5 s average near edits). Noise is constant-cost as required. Reserve levers (packed Q14 scan blend, noise G table) not needed.

---

## Final Measurements

| Resource | P1 End | P3 End (after fixes) |
|----------|--------|---------------------|
| Flash | 728,436 B | 743,016 B |
| Flash headroom | 25,228 B | 10,648 B |
| ITCM | 15,232 B | 15,976 B |
| ITCM free | 1,152 B | 408 B |
| SRAM1 .bss | 286,316 B | 288,900 B |
| sizeof(TetraktVoice) | 1,064 B | 1,064 B |
| sizeof(ColloidVoice) | — | 1,236 B |
| Slot reserve | 1,264 B | 1,264 B |
| Tetrakt CPU (worst block) | 382.7 instr | 382.7 instr |
| Colloid CPU (device, worst mode) | — | 19,136 (pwm 6) |

### ITCM Subsection Order (Linker Script)

`.itcm.s089` → `.itcm.s089p3` → `.itcm.s094p1` → `.itcm.s095p2` → `.itcm.s095p3` → `.itcm.s096p3`

### Cycle Budget

97% average cap = 152,004 of 156,705 cycles per block. Basic share = 19,620 cycles. Advanced share = 21,803 cycles.

---

## Changes This Session (Files)

### New Files
- `Core/DSP/Instruments/Tetrakt/TetraktVoice.h`
- `Core/DSP/Instruments/Tetrakt/TetraktVoice.c`
- `Core/DSP/Instruments/Tetrakt/TetraktParameters.h`
- `Core/DSP/Instruments/Tetrakt/TetraktParameters.c`
- `Core/DSP/Instruments/Tetrakt/TetraktTables.h`
- `Core/DSP/Instruments/Tetrakt/TetraktTables.c`
- `Core/MIDI/MidiNoteModes.h`
- `Core/MIDI/MidiNoteModes.c`
- `tools/tetrakt/gen_tkt_tables.py`
- `tools/tetrakt/make_init_tkt.py`
- `SD_CARD/Instrument/Tetrakt/init.tkt`
- `tools/dsp_test/modules/tetrakt.c`
- `tools/dsp_test/tetrakt_cpu.py`
- `tools/midi_test/notemode_test.c`
- `tools/midi_test/learn_test.c` (scheduled but host coverage noted as incomplete)

### Modified Files (34 firmware files + host tests)
- `Core/DSP/Instruments/InstrumentManager.h` — INSTRUMENT_TYPE_TKT, bit-cell macros, switch row, velocity group API, delta overlay
- `Core/DSP/Instruments/InstrumentManager.c` — Tetrakt registry row, runtime accessor, voice units, trigger, velocity groups, delta overlay system, wave domain max, bit-cell resolver, all dispatch switches
- `Core/DSP/Instruments/Colloid/ColloidVoice.h` — gate state, pitch envelope state, noise state, COL_TYP_WTS, COL_SCAN_COUNT, scan flag
- `Core/DSP/Instruments/Colloid/ColloidVoice.c` — gate entry points, col_egStep, pitch envelope, noise source, wts scan dispatch, B1–B6 optimisations, assessment fixes
- `Core/DSP/Instruments/Colloid/ColloidOsc.h/.c` — col_scanPairStore/Add loops, .itcm.s096p3
- `Core/DSP/Instruments/Colloid/ColloidParameters.h/.c` — COL_IDX_* indices, sustain row test, 9 appended rows, ONOFF_TAG_COL_TYPE
- `Core/MIDI/MidiNotes.c` — note-mode dispatch, Learn note/velocity apply
- `Core/MIDI/MidiNoteMap.h/.c` — wide tracks, sub in resolve/inverse
- `Core/MIDI/MidiVoiceControl.h/.c` — layers on ring, non-dropping enqueue, prepareTrigger
- `Core/MIDI/MidiControl.h/.c` — handlePressure
- `Core/MIDI/MidiLearn.h/.c` — sources, modes, capture pressure, applySource, file format
- `Core/MIDI/MidiIn.c` — channel pressure accepted
- `Core/MIDI/MidiOut.h/.c` — trackLayers, wide track TX
- `Core/Menu/MenuText.h` — ONOFF_TAG_TKT_LAYERS, tktLayerNames, ONOFF_TAG_COL_TYPE, colTypeNames
- `Core/Menu/menu.c` — bit-cell display/commit/automation, velocity group asterisk, sample wave cell, type name tag
- `Core/Hardware/frontPanel/buttonHandler.h/.c` — overlay carries layer, MIDI sub
- `Core/Hardware/SD/storageTypes.h` — STORAGE_INSTRUMENT_TKT
- `Core/Sequencer/sequencer.h/.c` — lenWindow, changeMasks, triggerVoiceLayers, queueForegroundAutomation, LIVE bit
- `Core/Sequencer/LiveRecord.h/.c` — layers, autoTarget/autoValue, LIVEREC_HIT_AUTO, commitHit, paramValueIn, layerStep/Tick
- `Core/DSP/Effects/Space/SpaceEffect.c` — Delay/Echo linear interpolation (F1)
- `Core/DSP/Effects/compressor.c` — sidechain duck easing (F6)
- `config.h` — MIDI_NOTEMODE_KEYS, MIDI_NOTEMODE_GROUPS
- `random.h/.c` — RNG->DR (F8 fix)
- `main.c` — midiNoteMode_init, midiNoteMode_service
- `Makefile` — Tetrakt build rules, MidiNoteModes
- `STM32F765VIHx_FLASH.ld` — .itcm.s096p3

---

```
KNOWN ISSUES INTRODUCED:
- Flash headroom is 10,648 B (below the 16 KiB warning line). Further features need flash-budget
  awareness. Options: mark large foreground MIDI functions noinline where LTO duplicates them.
- Tetrakt CPU exceeds Basic share at worst-case SRR 127 on all four layers (highest window
  21,893 of 19,620). The fused read (§19) brings the median within range. The worst case
  needs the Advanced class or further trim (dropping SRR saves ≈1,170 cycles).
- Host test gaps remain: CO-NOISE spectra, IM-OVL, LR-VAL.

KNOWN ISSUES RESOLVED:
- Colloid Basic-share CPU (all modes now fit after B1–B6 optimisation pass and device acceptance).
- wts scan position law (F2: steps on crossings, stalls on descents — fixed).
- Delta overlay vs LFO (F3: hook now adds delta to LFO value instead of ignoring it).
- Revalidation writes old type index (F4: guard slot type).
- Leftover gate renew (F6: carry-only flag).
- Noise level depends on oscillator norm (F7: use constant norm).
- wts spread quantised to whole frames (B11: float k).

NEXT SESSION RECOMMENDED GOAL:
- Device test of Tetrakt (listening pass: all four layers, the space stage, velocity groups).
- Device test of P3 MIDI features (gate, paraphonic chords, pressure Learn, Tetrakt layer notes).
- Decide on Tetrakt's CPU classification: keep Basic (median fits) or promote to Advanced.
- Flash headroom decision: accept 10 KB or refactor.
- Close host test gaps (IM-OVL, LR-VAL, CO-NOISE spectra).

BLOCKERS:
- None hard. The flash headroom is a soft constraint on the next feature's size.

CRITICAL REMINDERS FOR NEXT SESSION:
- The P4 baseline (`elf_p4_check` default p4base) no longer matches the §19 image — re-snapshot
  from this build or keep the before-based check.
- COL_OSC_COUNT is now 10 for wav/wtb (B1); the old 12 figure no longer applies.
- COL_SCAN_COUNT is 6 (not 8); the scan loops are 67 instr/pair, not 48–56.
- The delta overlay's 72 B and the note-mode pools' 2,380 B are approved SRAM1.
- The Colloid's noise is in a pre-pass scratch (B2), not in the filter loop registers.
- sizeof(ColloidVoice) is 1,236 B with 28 B spare of the 1,264 B slot reserve.
```
