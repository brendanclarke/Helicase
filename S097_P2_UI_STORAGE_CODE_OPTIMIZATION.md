# S097 P2 — UI and Storage Code Optimised for Size: Plan

Date: 2026-11-17. Branch `dev-ph7-alkaline`, after S097 P1 (729,688 B used).
Status: **plan only; nothing here is applied to the tree.** Every number below
was measured on scratch copies of the tree at S097 P0 + P1 items 1/4/5
(729,688 B), built with `make all`. P1 item 2 (labels as indexes, −3,032 B,
726,656 B) landed afterwards. It touches only descriptor rows and menu
readers, but the measurements should be repeated against the current image
before step 1 (LTO jitter, P0 §3).

Goal (user): about **40–45 KB** of flash from compiling cold UI/storage code
with `-Os`, choosing the cases **least likely to affect CPU use**. Background:
`S097_P0_PROGRAM_FLASH_USE_REFACTOR.md` §3 (LTO jitter) and §4 A.

---

## 1. Summary

| Step | What | Flash (measured) | Per-sample loops | Build-policy change |
|------|------|-----------------:|------------------|---------------------|
| 1 | `-Os` on the **tier-1** files (§3) | **−17,816** | all exact | none |
| 2 | pin the LTO partition count (§4) | −24 | **one-time move**: 3 PWM loops +1, 2 PWM sync loops restructured, 2 sample-FM loops −1 (needs pricing) | Makefile LDFLAGS |
| 3 | with the pin: `-Os` on `filesystem.c` | −39,872 (on its own) | all exact against step 2 | none further |
| 1 + 2 + 3 | tier 1 + pin + `filesystem.c` | **−54,768** | all exact against step 2 | — |
| to land at 40–45 KB | pin + `filesystem.c` + tier 1 without `asyncfatfs.c`/`fat_standard.c` | ≈ −44,500 (estimate) | as above | — |

**Recommendation:**

- **Do step 1 now** (−17.8 KB). It needs no build-policy change and leaves
  every per-sample loop exact.
- **Then decide step 2** on its measured one-time loop change (§4.3). If the
  user accepts it, step 3 brings the total to ≈ 44 KB (leaving the FAT engine
  at `-O2`) or 54.8 KB (everything measured here).
- **40–45 KB cannot be reached without `filesystem.c` or `menu.c`.** Every
  cold file that is safe without the pin adds up to ≈ 18–24 KB. Today, both
  big files trip the LTO jitter and add +1 instruction to 9 mixer hot loops
  (§2), which the S089 ceiling does not allow.

---

## 2. What was learned while measuring

### 2.1 Per-file `-Os` (one file or group at a time, today's partitioning)

Reference: 729,688 B. "Gate": `elf_loopdump.py` + `elf_hotloops.py` on
`P4_OSC_SYMS` and the mixer, against the reference image. "Hot calls": the set
of `bl` targets in the mixer, the render function, TIM3 and every ISR.

| File(s) | Δ flash | Osc hot loops (eq/shr/grew) | Mixer hot loops | Hot calls | LTRANS parts |
|---------|--------:|------------------|-------|-----------|------:|
| `filesystem.c` | −39,724 | 68/4/5 | 49/0/**9** | same | 15 |
| `menu.c` | −20,984 | 68/4/5 | 49/0/**9** | same | 16 |
| CopyClear (4 files) | −10,428 | 70/4/3 | 58/0/0 | same | 17 |
| `asyncfatfs.c` + `fat_standard.c` | −10,272 | 77/0/0 | 58/0/0 | same | 17 |
| `buttonHandler.c` | −4,036 | 77/0/0 | 58/0/0 | same | 17 |
| `presetManager.c` | −3,836 | 70/4/3 | 57/0/1 | **mixer gains a call** to `scene_getVoiceFxSendAmount` | 17 |
| `menuEffects.c` | −2,576 | 77/0/0 | 58/0/0 | same | 17 |
| `storageTypes.c` | −2,400 | 77/0/0 | 58/0/0 | same | 17 |
| `ledHandler.c` | −2,100 | 77/0/0 | 58/0/0 | same | 17 |
| `MidiLearn.c` | −992 | 77/0/0 | 58/0/0 | same | 17 |
| USB stack (9 files) | −756 | 70/4/3 | 58/0/0 | **`OTG_FS_IRQHandler` changes** | 17 |
| `Autosave.c`, `AutosaveTrace.c`, `PatternTrace.c` | −392 | 77/0/0 | 58/0/0 | same | 17 |
| nine `*Parameters.c` | −256 | 77/0/0 | 58/0/0 | same | 17 |
| misc (`memtest.c`, `flashImage.c`, `screensaver.c`, `SplashAnimation.c`, `Cc2Text.c`) | −208 | 77/0/0 | 58/0/0 | same | 17 |
| `SampleInstall.c`, `sampleFlash.c` | −200 | 77/0/0 | 58/0/0 | same | 17 |
| `BankData.c` | **+192** | 77/0/0 | 58/0/0 | same | 17 |

### 2.2 The jitter follows the LTO partition count

Today the link makes **17** LTRANS partitions. Every variant that dropped to 16
or 15 (`filesystem.c`, `menu.c`, tier 1 + CopyClear) landed in the same state:
9 mixer hot loops +1 and 5 oscillator loops +1. Variants that stayed at 17 were
either exact or in a milder state (3 PWM loops +1, 4 −1). The count falls
because the partitioner sizes partitions by total code size. Shrinking cold
code changes which functions share a partition with the voice renders, and so
their register allocation.

**Pinning the count stops it.** With `--param=lto-partitions=17
--param=lto-min-partition=1000` the link always makes 17 partitions.
`-Os` on `filesystem.c`, on `menu.c`, or on tier 1 + `filesystem.c` then leaves
**all 94 oscillator loops exact (dispatch fragments included) and all 58 mixer
hot loops equal** against the pinned build without `-Os`.
`-flto-partition=one` does the same, but costs more once (§4.4).

### 2.3 ISR and dispatch code jitter too

The loop gate covers per-sample loops only. Comparing the normalised
instruction text of every `*_Handler`/`*_IRQHandler`, the render function, the
mixer and the panel exchange shows that the same partition states also change
some ISR bodies. For example `TIM1_CC_IRQHandler` (encoder) is 91 or 71
instructions, and `din_dout_exchange` (panel SPI) 268, 238 or 139, depending
on the state, even for files that contain no ISR code (`menu.c`, CopyClear).
Within one partition state the ISR bodies are stable, except where a file
really does contribute code to an ISR (§3.2).

**So each step needs an ISR/dispatch check as well as the loop gate (§5).**

---

## 3. Tier 1: lowest risk, no build-policy change (step 1)

### 3.1 The set (measured together: −17,816 B, gate clean)

| File(s) | Δ alone | What it is | When it runs |
|---------|-------:|------------|--------------|
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`, `fat_standard.c` | −10,272 | FAT engine | main loop, inside filesystem operations; boot |
| `Core/Hardware/SD/storageTypes.c` | −2,400 | text parse/format of Kit/Instrument/Effect files | during Load/Save |
| `Core/Menu/menuEffects.c` | −2,576 | Effect page cells and labels | menu repaint and edits |
| `Core/MIDI/MidiLearn.c` | −992 | Learn table, `midilrn.cfg` lines | Learn gesture, boot, MIDI Learn lookups |
| `Core/Bank/Scene/Autosave.c`, `AutosaveTrace.c`, `Core/Bank/Scene/Pattern/PatternTrace.c` | −392 | AutoSave image building, DEV traces | AutoSave drains, DEV logging |
| `Core/SampleRom/SampleInstall.c`, `sampleFlash.c` | −200 | `Load:[Samples]` install | modal, audio suspended |
| nine `*Parameters.c` (Drum, Snare, Cymbal, HiHat, Alkaline, Colloid, Tetrakt, CrumpBit, Space) | −256 | descriptor tables and page hooks | UI |
| `memtest.c`, `flashImage.c`, `screensaver.c`, `SplashAnimation.c`, `Cc2Text.c` | −208 | boot checks, screensaver, splash | boot, idle |

Measured together (`t1`): 729,688 → 711,872 B (**−17,816 B**). Oscillator
loops 77/0/0, mixer hot loops 58/0/0, hot call sets unchanged. ISR bodies:
`din_dout_exchange` is one instruction longer (268 → 269), the same as the misc
group alone. All other differences are equal-length (literal and register
layout). Step 1 should confirm whether the +1 goes away when misc is left out
(−208 B); if so, leave misc out.

### 3.2 Excluded from tier 1, and why

| File(s) | Saving | Reason |
|---------|-------:|--------|
| `SceneData.c`, `StepScale.c` | — | their accessors are inlined into `mixer_calcNextSampleBlock()`, `audio_check_and_render()` and `TIM3_IRQHandler`. At `-Os` they become calls (P0 §4 A) |
| `presetManager.c` | −3,836 | the mixer gains a call to `scene_getVoiceFxSendAmount()`: warm code |
| USB stack | −756 | changes `OTG_FS_IRQHandler` (an ISR) |
| `buttonHandler.c` | −4,036 | `din.c` calls `buttonHandler_buttonPressed()`/`buttonHandler_buttonReleased()` from the panel exchange (`din_dout_exchange`, interrupt context). Their code is inlined there |
| `ledHandler.c` | −2,100 | `sequencer.c` calls `led_notifyPatternChanged()` on the sequencer path; it also changes `din_dout_exchange` |
| `BankData.c` | +192 | grows |
| CopyClear (4 files) | −10,428 | CPU-safe in principle (main loop, time-bounded jobs) but it moves 3 PWM loops today; candidate for after step 2 |
| `menu.c` | −20,984 | needs step 2. Also, menu repaint is not time-bounded and runs every main-loop pass while playing, when there is no free render time (CPU widget 99–100 %); slower menu code is the most likely to be felt. Last candidate |
| `main.c`, `sequencer*.c`, MIDI, `InstrumentManager.c`, `presetMorphEngine.c`, timers, IO drivers, all `DSP_SRCS` | — | never: render loop, ISRs, per-block control |

`buttonHandler.c` and `ledHandler.c` (≈ −6.1 KB together) can join later if
the few functions that run in interrupt or sequencer context are kept at `-O2`
with `__attribute__((optimize("O2")))`, and the ISR check (§5) shows those
bodies unchanged. That is not measured here.

---

## 4. Steps 2 and 3: pin the partition count, then `filesystem.c`

### 4.1 Why `filesystem.c` is the right big file

- It is the largest single saving (−39.9 KB).
- Its runtime work is already time-bounded. Load/Save, AutoSave drains and
  directory scans batch work per main-loop tick under
  `filesystem_batchHasTime()` and stop at the budget. Slower code means more
  ticks per operation, not longer ticks. The binding Load/Save rules (no extra
  SD polls per pass while playing, the 12 s watchdog) are unaffected.
- Boot runs it blocking (directory trim, boot readers, AutoSave settle),
  before audio starts. Boot gets somewhat slower; nothing audible.
- It holds no ISR code. Its call sites in the render path were checked: hot
  call sets are unchanged.

### 4.2 Measured

| Build | Flash | vs current | Loops vs the pinned build | ISR/dispatch vs the pinned build |
|-------|------:|-----------:|---------------------------|----------------------------------|
| pinned (`p17`) | 729,664 | −24 | — | — |
| pinned + `filesystem.c` | 689,816 | −39,872 | all exact | TIM3 667 → 659, TIM7 160 → 156, mixer dispatch −6, render function equal length |
| pinned + `menu.c` | 707,988 | −21,700 | all exact | TIM7 −4 |
| pinned + tier 1 + `filesystem.c` | 674,920 | **−54,768** | all exact | as `filesystem.c`, plus `din_dout_exchange` +1 |

All the ISR/dispatch changes are equal or shorter. Each still needs pricing on
its longest path (§5), since shorter text is not proof of a shorter path.

### 4.3 The one-time cost of the pin (to be priced before adopting)

Pinned build against today's image (no `-Os` anywhere): flash −24 B, ITCM
+24 B (15,984), mixer hot loops all equal. Oscillator loops:

| Loop | Today | Pinned | Note |
|------|------:|-------:|------|
| `osc_pwmWsFmBlock` | 31 | 32 | +1 |
| `osc_pwmWsFmRampBlock` | 32 | 33 | +1 |
| `osc_pwmWsRingBlock` | 34 | 35 | +1 |
| `osc_pwmWsSyncBlock` | 38 | 23 | the tool paired a different, restructured loop. **Price by source pairing** (`elf_loopsrc.py` on the `-g` twins, the S093 P5 §3.2 method) |
| `osc_pwmWtSyncBlock` | 39 | 24 | same |
| `osc_smpFmBlock` | 43 | 42 | −1 |
| `osc_smpFmRampBlock` | 44 | 43 | −1 |

These are Drum-family PWM (warp, sine/tri) and sample-FM loops. Under the
S089/S093 rules the pin is acceptable only if the per-sample cost of every
voice path, measured the S093 way (loops × iterations + per-block work / 32,
VDIV counted separately), does not rise. If the three +1 loops are not paid
for by the −1 loops on the same voice paths, try other pin values
(`lto-min-partition`, `lto-partitions`) until a state with no growth is found.
Today's partition count is 17 and the pin only has to hold it. The search is
mechanical: one build and one gate run per value.

### 4.4 Alternative: `-flto-partition=one` (measured, larger one-time change)

One partition also freezes the per-sample loops against cold changes:
`filesystem.c` −39,452 B and `menu.c` −20,232 B with all loops exact against
the one-partition build. Tier 1 + `filesystem.c` comes to −60,936 B against
it. But its one-time change against today is larger: +1,596 B flash, ITCM
−64 B, 7 oscillator and 4 mixer hot loops +1 (4 −1), and `TIM3_IRQHandler`
re-inlined (667 → 548 instructions). The pin (§4.3) is the smaller step.

### 4.5 Reaching exactly 40–45 KB

Pin + `filesystem.c` + tier 1 **without** the FAT engine (`asyncfatfs.c`,
`fat_standard.c`) ≈ −54,768 + 10,272 ≈ **−44.5 KB**. This is an estimate: the
measured full set less the FAT engine's measured saving on its own, not this
exact set built. Leaving the FAT
engine at `-O2` keeps the lowest layer of every SD operation at today's speed.
With it, the total is −54.8 KB (measured).

---

## 5. Procedure for each step

1. **Makefile only.** A target-specific variable, as used for the
   measurements:
   ```make
   # S097 P2: cold UI/storage sources at -Os (see S097_P2_…md for the gate)
   OS_SRCS = <tier list>
   $(patsubst %.c,$(BUILD)/%.o,$(OS_SRCS)): CFLAGS += -Os
   ```
   For the pin, the link line becomes
   `-O2 -flto --param=lto-partitions=17 --param=lto-min-partition=1000 -lc -lm`,
   with a comment explaining why the count is pinned. No source changes.
2. **Loop gate** against a snapshot of the image before the step:
   `elf_loopdump.py --compare` (0 MISSING) and `elf_hotloops.py --compare`
   on `P4_OSC_SYMS` and the mixer (0 GREW). Write the dumps outside
   `tools/dsp_test/build/`, whose files are tracked. If the step is the pin
   itself, price §4.3 instead and re-baseline after acceptance.
3. **ISR/dispatch check.** Compare the normalised instruction text of every
   `*_IRQHandler`/`*_Handler`, `audio_check_and_render()`,
   `mixer_calcNextSampleBlock()` (dispatch), `voiceControl_processPending()`,
   `seq_rampService()`, `din_dout_exchange()`, `endlessPots_tick()` and
   `encode_read4()`. For each that changed, price the longest path
   (`elf_blockpath.py` / `elf_looppath.py`, `tools/alkaline/control_cost.py`).
   It must not grow.
4. **Hot call sets** unchanged (no new `bl` from the render function, the
   mixer, TIM3 or an ISR into a `-Os` function).
5. **Device:** the DWT cycle probe (DEV build, trace stage `p`) on the
   worst-case Scene, before and after (the S094 P2 §21 budget). A Scene
   Save/Load, a Bank Load and an AutoSave drain while playing, timed and
   checked against the 12 s Load/Save watchdog. Boot to sound time. Menu
   responsiveness while playing.
6. **Record** the flash, the gate output and the device timings in the
   session log.

---

## 6. Not in scope

- Any DSP, render-loop, ISR or sequencer source at `-Os`.
- `DEV_MODE_LOGGING` / `DEV_STALL_DETECTION` (≈ −19.7 KB): kept for now by the
  user; recorded as a future lever in P0 §4 B.
- Source refactors: S097 P1.
