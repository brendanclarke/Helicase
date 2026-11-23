# S101 P3 — Implementation log: the `Spc` voice chain and the Cymbal/HiHat LP

**Date:** 2026-11-22. **Branch:** `dev-ph7-alkaline`, base `f3150d18`
("reductions land, tested ok"). **Status:** implemented, uncommitted, built,
host-tested and loop-gated. **Not yet on the device:** the CPU test is
yours (§11).

**The plan:** `S101_P3_BASIC_CHAIN_SPATIALIZER.md` revision 5 (decided).

**Your instruction (2026-11-22):** "write that in as decided and build the
spatializer with the cheaper LP on hi-hat and cymbal and then I'll test the
cpu use. *Don't* label the new LP as 'LP1' in cym/hi-hat, keep the label
'LP', same position as the 'LP' as previous, same in the list as Drum and
Snare. Implement that now, keep extensive notes on the implementation
progress as 'S101_P3_IMPLEMENTATION.md'."

**The image:** `build/LXRV2_lxr02.img`, 673,576 B payload (HEAD: 668,532 B).

---

## 0. Summary

| § | Change | Status |
|---|---|---|
| 2 | `Core/DSPAudio/voiceSpace.c/.h` (new): the spatializer (one-input and Snare forms) and the Cymbal/HiHat TPT low-pass | built; host laws pass; two breaks caught |
| 3 | The four engines: `Spc` branch in each sync render, the transient guards made explicit, Cymbal/HiHat `LP` → TPT low-pass in every chain, the Cymbal/HiHat `Snp` guard | built |
| 5 | InstrumentManager: the unit claim and bind, the right block, the SRR predicate, the row-byte stores, the LP state clear, the slot reserve pinned | built |
| 6 | The four parameter tables: `voice_chain` a fixed selector (no step automation); its descriptor index exported | built |
| 7 | Menu: `Spc` in the chain list; the five cells' short and long labels; spread and feedback values; the clamp and automation max | built |
| 8 | Makefile (`voiceSpace.o` `-Ofast -fno-lto`); `tools/dsp_test` target `voice_space` | built |

**The figures:**

| | HEAD | Now | Δ |
|---|---:|---:|---:|
| Flash (image) | 668,532 B | 673,576 B | **+5,044 B** (plan estimate 2.5–3.5 KB; §8) |
| ITCM | 15,424 B | 15,424 B | 0 |
| `.bss` | 422,480 B | 422,480 B | 0 (`runtime_slots` 7,584 B both) |
| `.data` | 436 B | 440 B | +4 B (section alignment; no symbol changed) |
| Space loop, one input / Snare (linked) | — | **48 / 54** a sample | the plan's gates ≤ 49 / ≤ 57 |
| Cymbal/HiHat LP loop (linked) | 101 + 3 VDIV (ZDF) | **31**, no VDIV | the plan's gate ≤ 32 |

**One open item for your decision (§4):** three of the mixer's own
per-sample loops are one instruction longer in this layout:
- the SRR decimator, 10 → 11;
- the T+D sample-transient read, 14 → 15;
- the mono add with a stereo-input send, 40 → 41.

This is LTO re-allocating registers in `mixer_calcNextSampleBlock()` after
the engine renders it inlines changed. Four ways of removing it were tried
and reverted (§4.2).

**Net per engine:**
- Cymbal/HiHat still fall (LP1 saves 9 instructions and a division).
- A T+D Drum/Snare is up to +3 instructions a sample (+0.14k expected).
- A mono Alkaline with a stereo-input send is +1 (+0.05k).

Every engine stays under the Tetrakt's 18.6k ceiling in the estimate (§11).

---

## 1. Decisions as built

| Item | Built as | Where |
|---|---|---|
| `chn` = 2 is `Spc` on Drum, Snare, Cymbal, HiHat | `VOICE_CHAIN_SPACE 2u`, `VOICE_CHAIN_COUNT 3u` | `voiceFold.h` |
| `Spc` replaces the transient, the post-chain drive and the SRR | the engine skips the transient and its post-chain; `instrumentManager_slotUsesSrr()` is 0 | §3, §5.5 |
| The five cells | `frq` time (`tim`), `vol` feedback (`fbk`), `wav` spread (`spr`), `srt` tone (**`spt`**), `drv` level (**`spc`**) | §7 |
| The stereo form | **E, the Tetrakt's law** (both taps interpolated). Form H: `-DVSPACE_SPREAD_INTERP=0` | `voiceSpace.h` |
| Time law | exponential, 1.0–49.3 ms (table) | `vspaceTime[]` |
| Feedback | bipolar, −0.97..+0.97, stored 63 = 0 | `vs_block()` |
| Cymbal/HiHat LP | the TPT low-pass in every chain, **labelled `LP`**, same list position | §3.3 |
| LP resonance | clean (default; Q2) | `voiceSpace_lp1()` |
| `chn` automation | removed (fixed selector; Q4) | §6 |
| RAM | ≈ 80 B per drum-family voice inside the reserve pinned at 1,264 B | §5.8 |

---

## 2. `Core/DSPAudio/voiceSpace.c/.h` (new)

### 2.1 State: `VoiceSpace` (80 B on the target; asserted)

| Field | Size | Meaning |
|---|---:|---|
| `line` | 4 | the FxBuffer voice unit, or NULL (no unit: the render is the mono dry ramp) |
| `wi` | 2 | the write position, a multiple of 32, < 2,176 |
| `time`, `tone`, `level` | 3 | the row bytes of `transient_freq`, `instrument_decimation`, `instrument_drive`, stored by their writers in every chain |
| `pad` | 3 | |
| `lp` | 4 | the tone pole (Q15 × 2: the taps' sum << 14) |
| `right[32]` | 64 | the right output block |

**Spread and feedback** are read from the engine's `transGen.waveform` (the
raw byte; `transient_setWaveform()` keeps 0..127) and `transGen.volume` (the
generic store: byte / 127, `InstrumentManager.c` `instrumentManager_writeParameter()`).

### 2.2 The ring

- **Layout:** `VSPACE_RING` 2,176 (68 × 32) + `VSPACE_MIRROR` 32 = one unit
  (2,208, asserted against `FXBUF_VOICE_UNIT_SAMPLES`).
- **The write** advances 32 a block, so it wraps only at a block boundary.
- **The reads** start in `[1, 2,176]` and read `[start − 1, start + 31]`.
- **The mirror** (`ring[2,176..2,207] = ring[0..31]`) is a 64 B block copy
  in `vs_end()` after the block that wrote `ring[0..31]`.
- **The taps are at least 32 samples:**
  - the time tap's floor is 44 (1 ms, the Tetrakt's);
  - the spread tap is clamped at `VSPACE_TAP_MIN` 32;
  - `VSPACE_TIME_MAX` is 2,174, so a tap's older sample never reaches the
    write position early.
- **Asserts:** the layout, the block alignment, `TAP_MIN ≥ 32` and the max
  tap are `_Static_assert`ed.

### 2.3 The law (per block, `vs_block()`)

- `tL = vspaceTime[time]`: `44 × (2174/44)^(v/127)`;
  - 1.0 ms at 0, 6.9 ms at 63, 49.3 ms at 127;
  - the table source is in the file comment.
- `tR = max(tL × (1 − 0.5 × spread/127), 32)`.
- `fL | fR << 16`: the fractions in Q15. Form H drops `fR`.
- `fb = (feedback × 127 − 63) × 0.97/64`, clamped to ±0.97, packed with
  `slv = level × 32767/127`.
- `fbK = tktFbLpK[tone]` (the Tetrakt's S1 tone table, 600 Hz–16 kHz).
- The ramp: `g = g0` in Q30 (clamped 0..1.999), `dg = (g1 − g0)/31` (a
  multiply).

### 2.4 The frame (per sample)

```
x   = smmul(in << 2, g)                 (Snare: in = ssat16(noise × mix + tone))
tl  = aL + smulwb((bL − aL) << 1, fLR)   tr = aR + smulwt((bR − aR) << 1, fLR)   (H: tr = aR)
lp += smulwb(((tl + tr) << 14) − lp, fbK) << 1
ring[w] = ssat16(x + (smulwb(lp, fbslv) >> 14))
L = ssat16(x + smulwt(tl << 1, fbslv))   R = ssat16(x + smulwt(tr << 1, fbslv))
```

The reads come before the write in each frame. On the host the DSP
intrinsics have exact portable forms (`VS_SMULWB` etc.).

### 2.5 The Cymbal/HiHat low-pass (`voiceSpace_lp1()`, labelled `LP`)

**Per block:** the ZDF's own coefficients:
- `g = filter->g`;
- `R = (f ≥ 0.4499) ? 1 : q` (the ZDF's stability rule; `q ≥ 0.02` from
  `SVF_setReso()`);
- `k = 2R`, `a1 = 1/(1 + g(g + k))` (the block's one division);
- `d = filter->drive / 32767` (the LP's drive floor offset is 0).

**Per sample:**
- the input cubic `u − u³/27` (|u| ≤ 3): the ZDF input saturator
  `2·tanh(u/2)`'s slope 1 and limit 2;
- the TPT step;
- the output cubic `c(0.967 − 0.1340c²)` (|c| ≤ 1.551): `fastTanh()`'s slope
  0.967 and limit 1.000. The coefficient was corrected from the proposal's
  0.1384, which peaks at 1.526 and dips before the clamp.

**State** is `filter->s1/s2`, cleared by the `filter_type` writer when the
type changes to or from LP.

### 2.6 Measured

Linked and standalone are the same: the object is `-fno-lto`.

| Function | Loop | Stack reloads | Per-block path (`elf_blockpath.py`) | Charged a sample (loop + (path + 10)/32 + mixer 7) |
|---|---:|---:|---:|---:|
| `voiceSpace_process` (form E) | **48** | 7 | 129 | **59.3** |
| `voiceSpace_processMix` (form E) | **54** | 8 | 142 | **65.8** |
| form H (`-DVSPACE_SPREAD_INTERP=0`) | 41 / 48 | | | 52.3 / 59.8 |
| `voiceSpace_lp1` | **31** | 0 | 35 | 32.4, no VDIV |

**Against the plan:** the one-input loop is 2 over the prototype's 46
(stack reloads), inside its gate of 49. The plan charged 60.4 and 68.6 a
sample; the build is 59.3 and 65.8.

---

## 3. The engines

### 3.1 Common to all four

- **The member:** `VoiceSpace space;` is the last struct member (every
  descriptor offset unchanged), with `#include "voiceSpace.h"`.
- **Init:** `voiceSpace_init(&voice->space)` at the end of each
  `*_initVoice()`.
- **The transient guards are now `== VOICE_CHAIN_DEFAULT`.** They were
  "not WFd", which would have run the transient under `Spc`:

  | Site | Was |
  |---|---|
  | `DrumVoice.c` (the pre-filter stage) | an `else` |
  | `Snare.c` (the transient add) | `!= VOICE_CHAIN_WAVEFOLDER` |
  | `CymbalVoice.c`, `HiHat.c` (`transient_calcBlock`) | `!= VOICE_CHAIN_WAVEFOLDER` |

### 3.2 The `Spc` call (in place of the post-chain)

| Engine | Call | Ramp endpoints |
|---|---|---|
| Drum | `voiceSpace_process(&space, buf, lastGain × lastVeloGain, ampFilterInput × veloGain, transGen.waveform, transGen.volume)` | `voicePost_drum()`'s endpoints; `lastVeloGain` updated after as before |
| Snare | `voiceSpace_processMix(&space, buf, transBuf, mix, lastAmpGain, ampGain, …)` | `lastAmpGain`, `ampRampArm` updated after as before |
| Cymbal, HiHat | `voiceSpace_process(&space, buf, lastAmpGain, ampGain, …)` | as above |

T+D and WFd keep their post-chains exactly.

### 3.3 Cymbal and HiHat

- **The filter:** `if (filterType == FILTER_LP) voiceSpace_lp1(&filter, buf);
  else SVF_calcBlockZDF(...)`, in every chain.
- **The type list is unchanged** (`filterTypes` reads `LP`, same position):
  no menu change.
- **The async `Snp` guard:** `if (chainType != VOICE_CHAIN_DEFAULT)
  pitchMod = 1` (was `== WAVEFOLDER`). Under `Spc` the `transient_wave` byte
  is the spread, and 0 is a valid spread; without this, spread 0 bent the
  pitch.
- **The Drum and Snare `Snp`/`Ofs` guards** were already
  `== VOICE_CHAIN_DEFAULT`.

---

## 4. The mixer: what LTO did, and what was tried

### 4.1 The finding

The engine renders for Snare, Cymbal and HiHat are inlined by LTO into
`mixer_calcNextSampleBlock()`; the Drum's (`Drum_calcVoiceSyncBlock.part`)
is not. The new branches and call sites changed the mixer's register
allocation and LTO's inlining choices:
- `busComp_processBlock()` is now inlined into the mixer; its loop is
  identical (71);
- `mixer_addVoiceInt16ToOutputAndFx()` is now out of line.

**The exact loop-length accounting** (the mixer, the add function and the
compressor, HEAD against now):

| Loop | HEAD | Now | Cause | Runs on |
|---|---:|---:|---|---|
| SRR decimator | 10 | **11** | `ldr r4, [sp, #20]`: the hold pointer reloaded each sample | every T+D/WFd drum-family slot |
| T+D sample-transient read (and its exit variants) | 14 / 17 | **15–16 / 18** | one `mov` | T+D with a sample transient |
| Mono add with a stereo-input send (two routes) | 40 | **41** | | every mono slot while its send is active (Drum family T+D/WFd, the Basic Alkaline) |

**Everything else is identical or not a per-sample loop:**
- 30 bodies identical after register normalisation;
- the add/send bodies unchanged in their new function (26 ×4, 34 ×2, 33 ×4,
  18, 25);
- HEAD's 30/32/34/36 and 17/19 were jump-table stubs;
- the new 38/44/50/101 are the FX return's multi-back-edge span, whose
  per-route bodies (15/20/29) are unchanged;
- every oscillator, filter, fold, PWM, Alkaline, Colloid and Tetrakt loop is
  exact (`elf_s099_check` clean; the whole-image loop compare, §10).

### 4.2 Attempts, all measured and reverted

| Attempt | Result |
|---|---|
| `busComp_processBlock()` noinline + `mixer_addVoiceInt16ToOutputAndFx()` always_inline (restore HEAD's inlining) | worse: the wavetable read 23 → 30 (stack loads) |
| busComp noinline alone | no change to the three growths |
| `instrumentManager_slotUsesSrr()` as one table read (`rightBlock[slot] == 0`) | no change (kept: it is cheaper and equivalent, §5.5) |
| `noinline` on the Snare/Cymbal/HiHat sync renders | much worse: ITCM +336 B, FM loops 23 → 35 (LTO re-cloned the oscillator code) |
| Two-argument noinline tail wrappers for the `Spc` call | no change |
| One noinline filter dispatcher (LP1/ZDF) in `voiceSpace.c` | no change; +3.4 KB flash (a second full copy of `SVF_calcBlockZDF()` without constprop) |

**What would remove it** (not attempted; your call): a further restructuring
of the inlined renders, or accepting it. Each is +1 instruction a sample on
the paths above; the cost is in §11.

---

## 5. InstrumentManager.c

1. **`instrumentManager_space(slot)`:** the drum-family `VoiceSpace`, or NULL.
2. **`instrumentManager_spaceBind(slot, want)`:**
   - want: acquire one unit if the slot owns none, then `voiceSpace_bind()`
     (clears on a new pointer, keeps the tail on the same one);
   - !want: release, and bind NULL;
   - then `instrumentManager_slotRightUpdate()`.

   Foreground only.
3. **`instrumentManager_unitsWanted()`:** DRM/SNR/CYM/HAT return 1 when the
   incoming image's `voice_chain` byte is `VOICE_CHAIN_SPACE`.
   - The indices are `DRUM_IDX_VOICE_CHAIN` 42, `SNARE_IDX_VOICE_CHAIN` 41,
     `CYMBAL_IDX_VOICE_CHAIN` 42, `HIHAT_IDX_VOICE_CHAIN` 42.
   - They are asserted against the enums. The first guesses (43/42) were
     caught by those asserts.
4. **`instrumentManager_runtimeInit()` (boot):** the drum family claims its
   unit when the resident chain is `Spc`, before the engine init (the
   ALX/CLX chorus rule). The boot apply's chain write binds it.
5. **`instrumentManager_slotUsesSrr()`:** for the drum family,
   `instrumentManager_rightBlock[slot] == 0`. A drum-family slot has a right
   block exactly while `Spc` holds its unit.
6. **`instrumentManager_slotRightUpdate()`:** the drum family return
   `space.right` while `chainType == VOICE_CHAIN_SPACE && space.line`.
7. **Writers:**
   - `IM_SPECIAL_TRANSIENT_FREQ` → `space.time`;
   - `IM_SPECIAL_INSTRUMENT_DRIVE` → `space.level`;
   - `INSTRUMENT_BIND_SLOT_DECIMATION` → `space.tone`.

   Each in every chain, beside its existing store.
   - `IM_SPECIAL_FILTER_TYPE`: on Cymbal/HiHat, `SVF_reset()` when the type
     changes to or from LP (the two filters' states differ).
   - `IM_SPECIAL_VOICE_CHAIN`: after the store,
     `instrumentManager_spaceBind(slot, chain == VOICE_CHAIN_SPACE)`. An
     out-of-range byte now clamps to `Spc` (COUNT − 1).
8. **The slot reserve:** `INSTRUMENT_RUNTIME_SLOT_BYTES 1264u`, pinned. It
   was `2 × sizeof(DrumVoice)`.
   - Measured: DrumVoice 712 B (was 632), SnareVoice 532, CymbalVoice 588,
     HiHatVoice 600 (80 B more each). Every engine's `_Static_assert` holds.
   - `runtime_slots` stays 7,584 B.
   - The stale "1,176 B reserve" comment is corrected.

---

## 6. The parameter tables

- **`ROW_SELECT_ONOFF_FIXED_SPECIAL`** (new, in each of the four):
  `ROW_SELECT_ONOFF_SPECIAL` without `INSTRUMENT_PARAM_FLAG_AUTOMATABLE`,
  the `fx` row's form.
- **The `voice_chain` row uses it.**
- **What that means:**
  - `instrumentManager_targetValid(..., INSTRUMENT_TARGET_AUTOMATION)`
    (`InstrumentManager.c:1397`) refuses a SELECTOR without AUTOMATABLE;
  - the automation picker no longer offers `chn`;
  - step automation already recorded on `chn` is skipped at the drain
    (`sequencer.c:4153`) and no longer changes the chain (no migration).
- **`osc1_run`** keeps the automatable macro.
- **`*_IDX_VOICE_CHAIN`:** exported in each `*Parameters.h`, with a
  `_Static_assert` in the `.c`.

---

## 7. Menu (`menu.c`, `MenuText.h`)

- **`voiceChainNames`:** `{3}, T+D, WFd, Spc` (the count assert holds).
- **The roles:** `MENU_SPC_*` and `menu_spaceRole(slot, descriptor)` map,
  by `file_key` and only when the slot's chain is `Spc`:

  | Row | Role | Short | Long |
  |---|---|---|---|
  | `transient_wave` | spread | `spr` | Spread |
  | `transient_vol` | feedback | `fbk` | Feedback |
  | `transient_freq` | time | `tim` | Time |
  | `instrument_decimation` | tone | **`spt`** | Tone |
  | `instrument_drive` | level | **`spc`** | Level |

- **Category** "Spatial" for all five.
- **The short label** (`menu_instrumentShortLabel()`) is used:
  - in the page header (the cell names);
  - in the target short name;
  - on the step-automation target line.
- **The long labels** go through `menu_instrumentCellLabels()`, which also
  serves the modulation target displays.
- **The value text** (`menu_formatSpaceValue3()`): spread is a number
  0..127; feedback is value − 63 (the bipolar law). It is used in:
  - the cell painter (`menu_formatCellValue3()`);
  - the automation markers (`va_formatValue3()`);
  - the single-parameter view;
  - the automation value text (`menu_formatVoiceAutomationValue3()`).
- **The clamp:** `menu_clampCellValue()` allows the spread 0..127 under
  `Spc`; the step-automation max for the spread is 127.
- **The filter type list:** unchanged (Cymbal/HiHat read `LP`).

---

## 8. Build

- **`Makefile`:** `Core/DSPAudio/voiceSpace.c` is in `DSP_SRCS`, with an
  explicit `-Ofast -fno-lto` rule (the S095 P2 / S099 P1 reason).
- **Flash +5,044 B against an estimate of 2.5–3.5 KB:**

  | Part | Bytes |
  |---|---:|
  | `voiceSpace.o` | 2,444 (incl. the 512 B time table) |
  | The menu helpers and their call sites | ≈ 1.2 KB |
  | InstrumentManager (accessor, bind, writers, four `unitsWanted` cases, four right-block cases) | ≈ 0.9 KB |
  | Mixer re-layout (LTO) | the rest |

  Headroom 80,088 B.
- **ITCM:** 15,424 B, unchanged.
- **Clean build:** `make all` + `make img`, with no warnings from the
  changed files.

---

## 9. Host tests (`make -C tools/dsp_test voice_space`)

`modules/voice_space.c` compiles the real `voiceSpace.c/.h` (extracted) with
the Tetrakt's `tktFbLpK[]`.

| Law | Result |
|---|---|
| SP-RING, one input: 476 blocks (7 × the 68 phases), time 0 / 127 / swept, every spread, feedback, tone and level, a ramp changing every block; bit for bit against a plain modulo-ring reference of the same frame | pass (0 mismatching samples) |
| SP-RING, the Snare form | pass |
| SP-TAP: time 0, 20, 63, 100, 127; the left and right echoes within 1 sample of their taps | pass |
| SP-FB: +0.97 bounded and decaying (last 4,000 samples < 1 % of the first); −0.955 alternates the echo sign | pass |
| SP-DC: +0.97 DC input never wraps; −0.955 keeps the line ≤ the input | pass |
| SP-DRY / SP-GAIN: level 0 is the ramped dry input on both sides exactly; the ramp starts at g0 and ends at g1 | pass |
| SP-MIN: time 0, spread 127 → the spread tap at 32 | pass |
| SP-MIX: the Snare form's level-0 output | pass |
| SP-NOUNIT: no unit → the dry ramp, mono, nothing written | pass |
| LP1-RESP: fc 0.01 / 0.05 / 0.15 of fs; pass band within 3 % and stop band within 5 % of the analytic TPT magnitude × 0.967 | pass |
| LP1-STAB: q 0.02 (lowest damping), drive 6.4, full-scale noise and square, 3,000 blocks | pass (finite, states < 100) |
| **Break: the mirror copy removed** | caught (SP-RING fails) |
| **Break: the spread-tap floor at 2 samples** | caught (SP-RING / SP-MIN fail) |
| The same laws with `-DVSPACE_SPREAD_INTERP=0` (form H) | pass |

### The full suite (`make -k -C tools/dsp_test all`)

Everything passes, `check_special_tags.py` included (431 rows, 0
mismatches), except three items that **fail identically on a clean HEAD
copy:**
- **`voice_control`:** 14 failures (laws 1, 4, 7, 8).
- **`alkaline` AL-OD0:** "golden file missing or short (0, 0)".
- **`colloid` CO-PWG:** intermittent ("full-slew width clicks"): 3 of 30
  runs in this tree, 5 of 30 at HEAD.

The CrumpBit break-25 "Bus error" line is that break being caught, also at
HEAD.

These are reported, not changed (scope).

---

## 10. Loop gates

**Baselines:** `tools/dsp_test/baselines/s101p3/pre` (HEAD `f3150d18`):
`osc.txt`, `mixer.txt`, `engines.txt`.

| Gate | Result |
|---|---|
| `elf_s099_check S099BASE=baselines/s101p3/pre` (Alkaline, Colloid, Tetrakt engines) | **clean** |
| `elf_p4_check P4BASE=baselines/s101p3/pre`, oscillator set | every loop exact except `busComp_processBlock` **reported MISSING**: it was inlined into the mixer by LTO, and its 71-instruction loop is unchanged there (`BusCompressor.c:449–451`, checked on `-g` twins) |
| `elf_p4_check`, mixer (`elf_hotloops.py`) | reports grew/shrank by heuristic pairing; the exact accounting is §4.1 |
| `tetrakt_cpu.py` | identical: 379.1 (A2q steady 329.9) |
| Whole-image loop compare (all functions, register-normalised) | the per-sample changes are exactly those of §4.1, plus the new `voiceSpace_*` loops |

**The `-g` twins** were full builds of HEAD and this tree with `-g` added:
- their mixer loops equal the real images' (verified);
- they were used only to name loops.

---

## 11. The CPU estimate and your device test

### 11.1 The change of each engine's dearest cost

Thousands of cycles a block; harsh (added at 1.9, removed at 1.4, VDIV 10)
/ expected (1.5 both ways, VDIV 13).

| Engine | T+D (LP1 where it applies, and the §4 growths) | `Spc` (form E) against today's T+D | Engine worst against today |
|---|---:|---:|---:|
| Cymbal / HiHat | −0.54 / −0.71 | **+0.10** / −1.06 | +0.10 (harsh), −0.71 (expected) |
| Drum | +0.18 / +0.14 | +0.82 / −0.21 | +0.82 / +0.14 |
| Snare | +0.18 / +0.14 | +0.99 / −0.14 | +0.99 / +0.14 |
| Alkaline (mono), with a stereo-input send | +0.06 / +0.05 | — | +0.06 / +0.05 |

**The `Spc` column** = the space stage (59.3 / 65.8 a sample) − (transient
20 + post-chain 25 / 30 + SRR 10 + the post-chain VDIV), plus Cymbal/HiHat's
LP saving (−9 instructions, −1 VDIV).

**Against the 18.6k ceiling** (the Tetrakt as built), with the estimated
dearest T+D figures:

| Engine | Estimated dearest T+D | Harsh worst with this build | Under 18.6k? |
|---|---:|---:|---|
| Snare | 13.2–13.9k | 14.2–14.9k | yes |
| Drum | 16.0–17.0k | 16.8–17.8k | yes |
| Cymbal/HiHat | 17.9–18.7k | 18.0–18.8k | the top of the range is 0.2k over, harsh; under at the expected rate (16.9–17.7k) |

### 11.2 The test (the plan's Steps 0 and 3)

1. **Flash** `build/LXRV2_lxr02.img` (logging build, as `config.h` stands).
2. **The test Scene:** one slot each of Drum, Snare, Cymbal, HiHat at their
   dearest settings:
   - Cymbal/HiHat: carrier `slp` FM, both modulators `slp`;
   - Drum: `fm`, `slp` on both oscillators;
   - Snare: tone `slp`;
   - the Delay on, every send > 0, the compressor on.
3. **Run it three ways** and read each slot's window median and maximum
   (the S1 probe, `MIXER_PROBE_MAX` on), plus the SRR and send-add
   sections:

   | Run | Settings |
   |---|---|
   | (a) T+D | a sample transient, `srt` < 127, **filter LP** on all four |
   | (b) T+D | the dearest ZDF type instead (Peak, on Cymbal/HiHat) |
   | (c) `chn` = `Spc` on all four | level, feedback, spread up |

4. **The gate:** every engine's slot + SRR + send-add ≤ 18.6k (median) in
   every run, and the highest window ≤ 19,620.
5. **If Cymbal/HiHat `Spc` is over:** rebuild with
   `-DVSPACE_SPREAD_INTERP=0` (form H, −7 a sample; add it to `CFLAGS` or
   define it in `voiceSpace.h`) and repeat (c).
6. **For the sound:** the Cymbal/HiHat `LP` (the TPT low-pass: clean
   resonance, the ZDF's input and output curves kept) against a Drum `LP`
   at the same settings.

---

## 12. Behaviour changes (no migration, the standing rule)

1. **Cymbal/HiHat `LP`** is a different filter in every chain (§2.5).
   Stored sounds keep their bytes and play it.
2. **`chn` cannot be step-automated.** Recorded `chn` steps are skipped
   (§6).
3. **An out-of-range `voice_chain` byte** (MIDI/NRPN, corrupt storage) now
   becomes `Spc` (was WFd).
4. **A Scene/Kit/Instrument apply onto an `Spc` slot** clears its line (the
   slot reset re-inits the voice; the bind clears on a new pointer), so a
   ringing tail stops at a Scene change.
5. **Choosing `Spc` while playing** claims a unit and reseats the bus Effect
   (the Tetrakt's type-assignment path); leaving `Spc` releases it.
6. **The Effect's share** shrinks by one unit per `Spc` slot (≤ 6, the
   six-Tetrakt ceiling). The Delay/Echo maximum time follows
   (`space_timeLimit()`).

---

## 13. Files

**New:**
- `Core/DSPAudio/voiceSpace.h`, `Core/DSPAudio/voiceSpace.c`;
- `tools/dsp_test/modules/voice_space.c`;
- `tools/dsp_test/baselines/s101p3/pre/` (`osc.txt`, `mixer.txt`,
  `engines.txt`).

**Changed:**

| File | Change |
|---|---|
| `Core/DSPAudio/voiceFold.h` | `VOICE_CHAIN_SPACE`, COUNT 3 |
| `Core/DSP/Instruments/Drum/DrumVoice.h/.c` | §3 |
| `Core/DSP/Instruments/Snare/Snare.h/.c` | §3 |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h/.c` | §3 |
| `Core/DSP/Instruments/HiHat/HiHat.h/.c` | §3 |
| `Core/DSP/Instruments/{Drum,Snare,Cymbal,HiHat}/*Parameters.h/.c` | §6 |
| `Core/DSP/Instruments/InstrumentManager.c` | §5 |
| `Core/Menu/menu.c`, `Core/Menu/MenuText.h` | §7 |
| `Makefile`, `tools/dsp_test/Makefile` | §8, §9 |

**Unchanged in the end** (tried and reverted, §4.2): `mixer.c`,
`BusCompressor.c`, the engine renders' inline status.

---

## 14. Statements this makes out of date (for you to act on)

- **`MEMORY.md` "Voice chain" bullet:**
  - two chains, `VOICE_CHAIN_COUNT` 2;
  - SRR "gated by `instrumentManager_slotUsesSrr()`" (now: not under `Spc`);
  - `chn` automatable.
- **`MEMORY.md` slot-reserve statements:** "2 × sizeof(DrumVoice)", and the
  Drum 632 B / reserve figures. The reserve is now a pinned 1,264 B; the
  Drum is 712 B.
- **`INSTRUMENTS_DSP_REFERENCE.md`:**
  - §5 chain table (add `Spc`);
  - §6.5–6.6;
  - the filter section: Cymbal/HiHat `LP` is the TPT low-pass, and the
    per-type ZDF costs (§2.3 of the proposal).
- **`MIXER_SYSTEM_DSP_REFERENCE.md` §3.2 item 2:** `slotUsesSrr()`; the
  mixer loop changes of §4.1.
- **`TETRAKT_MODULE_REFERENCE.md` §8 and the `FxBuffer.h` header comment:**
  the voice-unit users (now the drum family under `Spc` as well).
- **`MENU_UI_SPEC.md`:** `Spc`, the five cells' labels and values, `chn`
  not automatable.
- **`MIDI_NRPN_TABLE.md`:** the `voice_chain` range (0..2).
- **`CPU_USE_DSP_AUDIT.md`:** a new item for S101 P3 (the figures of §2.6,
  §4, §11).
- **`S101_P2_REDUCTIONS.md` §A.2:** the Cymbal/HiHat dearest figure (Step 0
  replaces it).
