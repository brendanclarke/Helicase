# LXR-02 DSP Performance Audit

> **Where to start (Session 087 close).** This document is the cost audit and
> the record of every DSP optimisation and cost addition (the priority list
> at the end; S074 items 27–31, S075 items 32–35, S085 items 36–40, S086
> items 41–44, S087 items 45–49). For how the DSP works today, what each
> stage costs and how to change it, read `INSTRUMENTS_DSP_REFERENCE.md` and
> `MIXER_SYSTEM_DSP_REFERENCE.md` and `EFFECTS_MODULES_DSP_REFERENCE.md`
(all in this folder). The S073
> refactor's full record is `073_SESSION_HANDOFF_LOG.md` §6; the S074 DSP
> additions are in `074_SESSION_HANDOFF_LOG.md` §7–§10; the host test bench
> is `tools/dsp_test/DSP_TEST.md`.

Session 033 note: this audit was moved into `specification_reference/` as a
historical DSP/performance snapshot. In the remainder of this document,
references to current code mean the audited snapshot, not a Session 040
claim. The CPU scheduling advice remains useful,
but the instrument ownership model changed after this was written:
descriptor-backed instrument parameters now live under `Core/DSP/Instruments/`,
are stored in Scene kit images, and are applied to DSP runtime state through
Preset/InstrumentManager. Sessions 032-033 repaired the directory-kit path,
descriptor runtime application, descriptor Morph, per-voice Morph, and
descriptor/Scene LFO and velocity modulation targets. Step automation remains
the major descriptor-target runtime path that is still legacy/incomplete. Read
`knowledge_files/specification_reference/FILESYSTEM_SPEC.md` before changing
instrument file, Scene storage, or instrument parameter propagation.

## Session 043 current-state supersession

This audit's 2026-05 findings are historical. Current source has MPU-isolated
DMA memory, enabled M7 I/D caches, a mapped 3,768-B ITCM section, `-flto`,
`-Ofast` DSP compilation, and DTCM-resident CPU audio output buffers. The old
fixed voice globals are retired in favour of six 1,176-B tagged runtime slots
in SRAM1. The slider taper is a 1,024-entry native-float LUT (4,096 B, raw
`>> 2`, no LUT interpolation), and `transientData` is now a 26,460-B internal
FLASH ROM rather than DTCM data. For actual current memory placement and the
reservation policy, use `STORAGE_SRAM_MANIFEST.md`; free DTCM is delay-line-reserved
and free normal SRAM1 is Pattern-reserved. The historical recommendations
below must not be read as live placement or cache facts.

## Session 073 current-state audit (2026-09-28)

This section is the current DSP CPU audit. It supersedes the cost picture in
the 2026-05 sections below, which remain as the record of the platform work
that has already been done (caches, MPU, LTO, `-Ofast`, ITCM oscillators,
DTCM output buffers, NVIC order). The reduction plan built on it was
implemented in Session 073 (items 16–25 below; record in
`073_SESSION_HANDOFF_LOG.md` §6). The figures in this section describe the
tree **before** that refactor; the measured after-state is in items 16–22 and
in the two DSP references.

### Method and limits

- **Source read:** the full per-block render path on `dev-ph5-effects` after
  S072 Step 9: `main.c` render loop, `mixer.c`, `InstrumentManager.c`
  dispatch, the four voice engines, `Oscillator.c`, `ResonantFilter.c`,
  `distortion.c`, `BufferTools.c`, `transientGenerator.c`, the envelope
  helpers, `lfo.c`, `modulationNode.c`, `EffectsManager.c`,
  `StereoFilterEffect.c`, and the DMA pack in `AudioCodecManager.c`.
- **Build evidence:** disassembly of `build/lxr02.elf` (GCC 14.2.1,
  `text=482,632`, `data=416`, `bss=426,336`, 8,472 B free in the 480 KiB
  application window, ITCM 3,768 / 16,384 B). Division counts, inlining and call chains quoted below
  come from that image, not from reading the source.
- **Not measured on hardware.** Cycle figures are static estimates from
  instruction counts and Cortex-M7 latencies (`VDIV.F32` about 14 cycles,
  dependent `VFMA` about 3 cycles). Treat them as ±50 %. The per-stage
  profiler proposed for S073 was declined by the user (F-5). The existing `cpu` widget
  (`audioCodec_getQueueFreePercent()`) shows audio-queue pressure. It cannot
  attribute cost to individual stages.

### Render structure today

- `audio_check_and_render()` (`main.c:204`) fills each free 96-frame DMA slot
  with three 32-frame `mixer_calcNextSampleBlock()` calls under a BASEPRI
  mask. The budget is **4,897 cycles per output frame** (216 MHz / 44,108 Hz),
  or about 156,700 cycles per 32-frame block.
- Control rate is one pass per 32-frame block, **1,378 Hz**:
  `modNode_resetTargets()`, `modNode_reassignVeloMod()`, six LFO dispatches,
  six `SVF_recalcFreq()`, six async voice updates, and `effects_service()`
  (`mixer.c:717-744`).
- Audio rate, per slot (`mixer.c:810-835`): voice render → decimate → FX
  send (when the Effect is active) → dry gain/pan/route. The four voice
  render functions and the inline copies of `calcDistBlock()` are all inlined
  by LTO into `mixer_calcNextSampleBlock` (7.8 KB).
- The Effect, currently `StereoFilter`, runs two float ZDF filter instances
  on the bus (`StereoFilterEffect.c:95`). The DMA HT/TC ISR then packs the
  finished slot into the strongly-ordered DMA buffers
  (`AudioCodecManager.c:382`).
- **No voice ever skips rendering.** A slot whose amplitude envelope has
  reached zero still runs its oscillators, filter, transient and distortion,
  and then multiplies the result by zero. This is why cost stays flat as the
  pattern gets busier. It is also why the worst case, where every voice is
  sounding, is always paid.

### Estimated worst-case cost per output frame

Worst case here means two Advanced plus four Basic voices, all sounding,
filters in ZDF modes with drive, distortion on, 12 LFO targets on instrument
descriptors, two waveform-interpolated oscillators, and StereoFilter active
with every send open.

| Stage | Cycles/frame (est.) | Share of 4,897 |
|---|---|---|
| 6 × voice ZDF filter (`SVF_calcBlockZDF`) | 600–780 | 12–16 % |
| FX StereoFilter, 2 × `SVF_calcBlockZDFFloat` | 200–260 | 4–5 % |
| Oscillators (≈14, plus 2 with wave interpolation) | 200–300 | 4–6 % |
| Distortion × 6 (one `VDIV` per sample) | 130–150 | ~3 % |
| Transient, amp EG, velocity and mix passes × 6 | 150–200 | 3–4 % |
| Mixer dry path × 6, decimation | 120–160 | 2.5–3 % |
| FX send × 6, bus conversion, return | 100–150 | 2–3 % |
| Control rate, amortised (LFO writes, restores, EGs, `effects_service`) | 120–300 | 2.5–6 % |
| DMA `pack_half()` ISR | 40–80 | 1–1.6 % |
| **Total audio work** | **≈1,650–2,400** | **≈34–49 %** |

This excludes the TIM3 sequencer ISR, UI ISRs and foreground work, which
also compete for the main loop.

### Findings

**F1. The ZDF filter is the largest single cost (≈16–21 % with the FX
filter).** Each sample of every non-naive type runs 5 `VDIV.F32` (6 for LP).
They are the input `softClipTwo`, `tanhXdX` for t0, `tanhXdX` for t1, g0,
and y1, plus LP's output `fastTanh`. In the current image the unswitched
`SVF_calcBlockZDF.constprop.0` holds 38 `vdiv` and `SVF_calcBlockZDFFloat`
holds 38. Four of those divisions sit on one dependent chain
(x → t0 → g0 → y1), so they cannot overlap. GCC already notices that
`softClipTwo(s1) == s1 * t1` and shares that work, and it already hoists the
naive 2-pole `q` division out of the loop. What remains is algebraic. The
three reciprocals in stage B (t0, g0, y1) and the two in stage A (input clip,
t1) can each be batched into one division. A host sweep of 28,800
configurations (276 M samples, all six ZDF types, f/reso/drive/signal grid)
compared the batched form with today's code. 99.77 % of samples are
identical, and the signal-to-difference ratio is 81 dB. Larger deviations
occur only in 56 self-oscillating high-resonance configurations. Rebuilding
today's code with a different FMA-contraction setting diverges in 54 of
those same configurations. So the batched form changes the output only as
much as a compiler flag does. For ARM `-Ofast` the batched loop compiles to
2 divisions per sample (3 for LP) instead of 6. It is about 340 B larger per
variant.

**F2. Every LFO write to an instrument parameter searches by string, every
block.** `lfo_dispatchNextValue()` (`lfo.c:134`) →
`instrumentManager_updateLfoAdapters()` → `applyLfoDescriptorTarget()` →
`instrumentManager_writeRuntimeInternal()` →
`instrumentManager_writeSpecialRuntime()` (`InstrumentManager.c:2917`). The
disassembly of that path contains 20 `strcmp`, 2 `strstr`, and 7 `strncmp`
through `instrumentManager_osc()`. Most of it runs before a plain
instance-offset target falls through to its ordinary write. Estimated cost is
200–600 cycles per write, and 12 LFO targets run 1,378 times a second. That
is about 1.5–4.5 % of the CPU for a descriptor-LFO-heavy Scene. The same
writer serves the foreground Morph worker (`presetMorph_tick()`,
`presetMorphEngine.c:439`), one parameter per pass. An LFO aimed at a voice
Morph re-queues a full pass of every Morphable parameter every block
(`presetMorph_setVoiceLfoModulation()`, `presetMorphEngine.c:645`), so this
cost also reaches the foreground continuously. The key-to-writer mapping is
fixed per descriptor row and can be resolved at compile time.
`instrument_runtime_binding_t` has one padding byte after `parameter_type`,
so an ID fits there without growing the descriptor tables.

**F3. Distortion costs one division per sample per voice.**
`calcDistBlock()` (`distortion.c:61`) computes
`(1+k)x/(1+k|x|)` even when `k == 0`, where the curve is the identity. It is
also a separate pass with its own int16 round trip after the amp and
velocity stages.

**F4. Voice post-chains make several int16 passes.** Drum runs
`bufferTool_addGainInterpolated()` (amp EG), then `bufferTool_addGain()`
(velocity), then `calcDistBlock()` (`DrumVoice.c:340-350`). That is three
loads, three conversions and three truncating stores per sample.
Snare/Cymbal/HiHat already fuse mix × EG × velocity but keep distortion as a
separate pass (`Snare.c:262`, `CymbalVoice.c:272`, `HiHat.c:291`).

**F5. The mixer reads each voice block twice when the Effect is active.**
`mixer_addVoiceToFxBus()` (`mixer.c:543`) and
`mixer_addVoiceInt16ToOutput()` (`mixer.c:409`) each reload the samples and
recompute a gain ramp.

**F6. The DMA pack writes one halfword at a time to strongly-ordered
memory.** `pack_half()` (`AudioCodecManager.c:382`) issues four 16-bit
stores per stereo frame per DAC. That is 768 non-bufferable stores per ISR,
into MPU region 1, which `clocks.c:187-193` sets to TEX=0 C=0 B=0. The DMA
consumes halfwords, but the layout can be written as one rotated 32-bit
store per channel. A Normal non-cacheable attribute (TEX=1 C=0 B=0) would
let the write buffer absorb those stores and still stay coherent with DMA.
The output bits would be unchanged.

**F7. `log2f()` still runs at control rate during pitch sweeps.**
`osc_setFreq()` (`Oscillator.c:918`) caches by effective frequency. A pitch
envelope changes that frequency every block, so each wavetable oscillator
calls `freqToTableIndex()` → `log2f()` (`Oscillator.c:63`) once per block
while a drum sweeps. The octave index can be found by comparing against 11
precomputed frequency thresholds. This is small (≈0.5 %).

**F8. Oscillators are already efficient.** The ITCM wavetable loop is about
20 instructions per sample, with phase kept in a register and one store. No
further work is recommended beyond F7.

**F9. Audio-rate noise reads the RNG peripheral directly.**
`calcNoiseBlock()` (`Oscillator.c:499`) calls `GetRngValue()`
(`random.c:62`) on each phase wrap. That is an AHB2 peripheral read without
a DRDY check. The F765 RNG delivers a new word about every 0.83 µs, and the
render produces samples faster than that, so high noise frequencies re-read
stale values. A software xorshift would be cheaper and would give fresh
values, but it changes the noise statistics slightly. It is optional.

**F10. Idle voices cost as much as sounding ones.** When the amp EG is in
decay or stopped at value ≤ 0 and the previous gain is zero, every
engine's output block is exactly zero. For Drum the transient and filter
are gated by the EG. For Snare/Cymbal/HiHat the EG multiplies the summed
signal, and distortion of 0 is 0. Skipping the render for such blocks leaves
the output identical. It changes only the oscillator phase and filter state
that the next trigger sees. Drum already resets its filter and phase on
trigger (`DrumVoice.c:216`); the other three do not. This lowers the
**average** load a lot but leaves the **worst case**, and so the underrun
threshold, unchanged.

### Corrections to the historical sections

- §9 below says double-precision arithmetic is "software emulation" on this
  part. That is wrong for the STM32F765. Its Cortex-M7 has a
  double-precision FPU, and `-mfpu=fpv5-d16` executes doubles in hardware.
  The current image contains `vfma.f64`. Double literals cost conversions
  and slower DP operations, not library calls. The promotions listed in §9
  are fixed. The one remaining double literal in the filter,
  `(1-filter->q) *1.4` at `ResonantFilter.c:172`, is loop-invariant, and GCC
  hoists it.
- §4/§6 figures that assume `sine_table` in DTCM are superseded. S072 Step 1
  moved it to flash, and D-cache now serves it.

### Checked and rejected

| Idea | Why not |
|---|---|
| ITCM for the filter or distortion | Tried in Session 023. Measured worse, because the forced out-of-line call costs more than it saves once I-cache is on. |
| Filter state kept in locals | GCC already keeps `s1/s2/zi` and the oscillator phase in registers across the loop and writes them back once. |
| Skip `SVF_recalcFreq()` when f is unchanged | About 0.1 % (six `fastTan` per block). Not worth the state. |
| Write-back instead of write-through SRAM cache | Hot loops keep state in registers, so the gain is small, while the coherency and ownership review would be large. |
| Voice runtime slots in DTCM | Free DTCM is the reserved FX arena under `STORAGE_SRAM_MANIFEST.md` policy. |
| Morph "skip unchanged value" cache | A listed Failed Approach (Session 16): it skipped required DSP restores. |
| Larger `OUTPUT_DMA_SIZE` (control block) | Envelope and LFO increments are per block, so every decay time would change. It also breaks the LXR-master cadence rule. |
| Decimator short-circuit at rate 1.0 | Already declined (item 11): tiny, and it hides the real budget. |
| Disable nonlinear integrators or halve LFO rate | Saves CPU, but the change is audible (resonance character, LFO stepping). Keep only as an explicit user-chosen "eco" option. |

### Sound-impact classes used by the S073 plan

- **S0, bit-identical.** Same output bits.
- **S1, rounding-level.** Only float rounding differs. That is the same
  class of difference as changing a compiler flag, and a host harness checks
  it.
- **S2, requantisation-level.** An intermediate int16 truncation moves. The
  change is at most 1 LSB before distortion. Distortion can amplify that LSB
  at low levels, but it amplifies the existing quantisation noise equally,
  so the noise floor stays the same and only its pattern changes.
- **S3, subtle behavioural.** State or noise differences meant to be
  inaudible. These need a listening A/B.
- **S4, audible.** A character change. S073 does not recommend these.

Audited: 2026-05-08. Firmware: LXR-02 Open Firmware (branch: LXR02Open-prime).
Problem statement: DSP render does not always complete within the 2.18ms
budget (96 samples at 44108Hz), causing audible underruns.

---

## 1. Should DSP math move from main() into DMA HT/TC callbacks?

**No.** This was already tried and documented as a failed approach (Session 10).

The DMA fires at HT and TC, giving a hard 2.18ms ceiling per callback. If the
DSP ever exceeds that budget the system locks up with no graceful degradation --
the DMA ISR re-enters or starves all lower-priority interrupts. In the main
loop, an overrun simply causes a buffer underrun (audible click) but the system
recovers on the next iteration. The main loop approach is correct.

The real problem is not *where* the render runs, but that other main-loop work
and missing hardware acceleration steal cycles from it.

**Current code** (`main.c:179`):
```c
if (audioCodec_queueFreeSlots() > 0) {
    mixer_calcNextSampleBlock(...);
    audioCodec_commitRenderBuffer();
}
```

**Key issue with this code**: it renders only 1 block per loop iteration even
when both queue slots are free. See priority #2 below.

---

## 2. Are NVIC priorities configured so audio beats control interrupts?

**No -- they are inverted.** Audio DMA is *lower* priority than UI timers.

| IRQ | Priority | Handler | Role |
|-----|----------|---------|------|
| TIM1_CC | 1 | Encoder edges | Control |
| TIM6 | 2 | 1kHz LED/button SPI | Control |
| TIM7 | 3 | 10kHz LCD drain | Control |
| DMA1_S4 | 4 | Audio refill master | **Audio** |
| DMA1_S7 | 4 | Audio refill slave | **Audio** |
| OTG_FS | 5 | USB MIDI | Control |

(`AudioCodecManager.c:378`, `timebase.c`)

On Cortex-M, lower number = higher priority. TIM6 and TIM7 can preempt the
DMA ISR mid-pack, and they preempt the main loop during DSP render. The
combined ISR overhead steals ~1-4% CPU from the render budget depending on
LCD activity.

**However**, the DMA ISR only does `pack_half()` (copies rendered data to the
DMA buffer), which is fast (~5-10us). The expensive part -- `mixer_calcNextSampleBlock()`
-- runs in the main loop with no priority protection at all. Every ISR freely
preempts it. This is the correct architecture (see question 1), but the
priority inversion means control ISRs eat into the audio render budget.

**Recommendation**: Swap TIM6/TIM7 below audio DMA. The target IRQ table in
MEMORY.md already specifies this.

---

## 3. Are UI tasks confined to main() and unable to stall DSP?

**Mostly yes, but with structural problems.**

All UI tasks (encoder, knobs, buttons, LCD enqueue, ADC, LED) run in the main
loop alongside DSP. The LCD hardware driver is ISR-based (TIM7) and the
enqueue is non-blocking -- if the queue is full, operations are silently
dropped (`lcd.c:181`). This is correct.

**Problem 1 -- Sequential main loop** (`main.c:150-253`): Every iteration runs
ALL UI tasks before checking the audio queue. If the queue had 2 free slots,
the UI tasks delay the render needlessly. The render check should come first
and should loop until the queue is full:

```c
// Current (bad):  UI → render(1) → SD
// Better:         render(all free slots) → UI → SD
```

**Problem 2 -- Kit apply burst** (`main.c:176`): `menu_pollPresetStatus()` can
apply all ~273 parameters in a single main-loop iteration when a kit finishes
loading. Each parameter update calls into DSP state. This creates a timing
spike of hundreds of microseconds.

**Problem 3 -- SD poll variability**: `sd_fsm_tick()` calls `afatfs_poll()`
which does bit-bang SPI. During active transfers, each burst is ~48us (16 bytes
at ~3us/byte via GPIO bit-bang). Multiple bursts per poll call can total
100-200us.

---

## 4. Are L1 I-Cache and D-Cache enabled?

**Neither is enabled. This is the single largest performance issue.**

`clocks.c:100` enables only the ART accelerator (flash prefetch/instruction
cache), which is a separate peripheral from the Cortex-M7 L1 caches:

```c
FLASH_ACR = (7UL | (1UL << 8) | (1UL << 9));  // PRFTEN + ARTEN only
```

The M7 core's 16KB I-cache and 16KB D-cache are never enabled. No calls to
`SCB_EnableICache()` or `SCB_EnableDCache()` (or their register equivalents)
exist anywhere in the codebase. Confirmed by `memtest.c:242`:
`if (!(SCB_CCR & SCB_CCR_DC)) return;` -- explicitly expects D-cache off.

**Impact of no I-cache**: At 216MHz with 7 wait states, every instruction fetch
from flash costs 8 cycles on a cache miss. The ART accelerator only helps
sequential execution; branches (frequent in DSP switch statements, function
calls, filter type dispatch) defeat prefetch. Estimated 10-30% overhead on
branchy DSP code compared to having I-cache enabled.

**Impact of no D-cache**: Wavetable reads from flash (.rodata) pay 7 wait
states per access. With INTERPOLATE_OSC=1, each sample reads 2 table entries.
12+ oscillator instances x 96 samples x 2 reads = ~2304 flash data reads per
block at ~8 cycles each = ~18,432 cycles = ~85us = **~4% of render budget**
lost to flash latency alone.

**Enabling I-cache is safe and immediate** -- no DMA coherency concerns.
Enabling D-cache requires MPU configuration (see question 5).

---

## 5. Is the MPU configured for DMA buffer cache coherency?

**No. The MPU is not configured at all.**

No MPU configuration exists anywhere in the codebase. This is why D-cache
cannot be safely enabled -- DMA buffers in SRAM1 (`dma_buffer[768]`,
`dma_buffer2[768]` at `AudioCodecManager.c:175-176`) would become incoherent
with the cache.

To enable D-cache safely:
1. Configure MPU to mark SRAM1 DMA buffer region as non-cacheable (or
   device/strongly-ordered).
2. Mark the rest of SRAM1 as write-back cacheable.
3. DTCM does not need MPU configuration -- it bypasses cache by hardware
   design.

Alternative: use `SCB_CleanDCache_by_Addr()` / `SCB_InvalidateDCache_by_Addr()`
around DMA transfers, but MPU region isolation is cleaner for circular DMA.

---

## 6. Should DSP code be in ITCM and buffers/LUTs in DTCM?

**ITCM is not mapped in the linker script and no code is placed there.**

The STM32F765 has 16KB ITCM at 0x00000000 -- zero-wait-state instruction
execution, independent of flash wait states and cache. The linker script
(`STM32F765VIHx_FLASH.ld`) does not define an ITCM region at all.

**Recommendation**: For the hottest DSP inner loops (SVF_calcBlockZDF,
calcNextOscSampleBlock, calcDistBlock, pack_half), 16KB of ITCM would
eliminate flash wait state overhead entirely. However, enabling I-cache
(question 4) gets most of this benefit with zero code changes and should
be done first.

**DTCM placement is already partially done.** Voice state structs
(`voiceArray[3]`, `snareVoice`, `cymbalVoice`, `hatVoice`) are in DTCM via
`.dtcmz` sections. Mixer routing arrays are also in DTCM.

**Not in DTCM but should be:**
- `audioOutBuffer[2][192]` and `audioOutBuffer2[2][192]` (`AudioCodecManager.c:178-179`)
  -- these are the render target buffers written by mixer, read by `pack_half()`.
  Both accesses are CPU-only (not DMA), so DTCM is safe. Currently in SRAM1.
- `sampleData[96]` (stack-allocated in `mixer_calcNextSampleBlock`) -- stack is
  in SRAM1. Could be made `static` in DTCM, but enabling D-cache on SRAM1
  is a better fix.

**Wavetables cannot fit in DTCM.** `sine_table[4097]` = 8KB, `sawTable[11][1024]`
= 22KB, `triTable[11][1024]` = 22KB, `recTable[11][1024]` = 22KB. Total ~74KB
vs 128KB DTCM, which is already partially used by voice state. The sine table
alone might fit, but the benefit is marginal once D-cache is enabled.

---

## 7. Can CMSIS-DSP SIMD instructions be leveraged?

**Yes, but with caveats.**

The Cortex-M7 supports DSP SIMD instructions (`__QADD16`, `__QSUB16`,
`__SMLAD`, `__SHADD16`, etc.) that operate on two packed int16 values in a
single 32-bit register. The current code uses scalar loops throughout
(`BufferTools.c`, `mixer_addDataToOutput`, `pack_half`).

**Best candidates for SIMD:**
- `bufferTool_addBuffersSaturating()` -- saturating add of two int16 buffers,
  called 2-4 times per voice. Direct replacement with `__QADD16`.
- `bufferTool_clearBuffer()` -- can use 32-bit stores (2 samples at once).
- `pack_half()` in the DMA ISR -- 96 iterations of interleaved copy.
- `mixer_addDataToOutput()` stereo paths -- 96 iterations with sat-add.

**Caveats**: The DSP pipeline is mostly float-based (filters, distortion,
gain stages). SIMD instructions only help the int16 buffer manipulation, not
the float math. The FPV5 FPU already handles single-precision float in 1-cycle
multiply/add. Full CMSIS-DSP library integration is overkill for this project.

**Recommendation**: Hand-written `__QADD16` intrinsics in BufferTools.c for the
hot buffer operations. Don't pull in the full CMSIS-DSP library.

---

## 8. Is DMA in circular mode?

**Yes, correctly configured.**

`AudioCodecManager.c:356-357` sets `CIRC` (bit 8) in the DMA stream control
register for both streams. The DMA runs continuously in circular mode, firing
HT and TC interrupts. The CPU never resets DMA addresses.

```c
const uint32_t cr_base = (2UL<<16)|(1UL<<13)|(1UL<<11)|(1UL<<10)
                         |(1UL<<8) |(1UL<<6);  // bit 8 = CIRC
```

Buffer layout: `OUTPUT_DMA_SIZE * 8` = 768 halfwords per buffer. DMA streams
through both halves continuously. `pack_audio_half(0)` fills the first half on
HT, `pack_audio_half(1)` fills the second half on TC. No issues here.

---

## 9. Is the FPU enabled and are float constants marked correctly?

> **Session 073 correction:** the "software emulation" statement in this
> section is wrong for the STM32F765. Its FPU is double-precision, and doubles
> execute in hardware, only more slowly. The promotions listed here are fixed.
> See the Session 073 section at the top of this document.

**FPU is enabled.** `clocks.c:88` (called first in `main()`):
```c
*((volatile uint32_t *)0xE000ED88UL) |= (0xFUL << 20);  // CP10+CP11 full access
```

Compiler flags (`Makefile:11`):
```
-mfpu=fpv5-d16 -mfloat-abi=hard
```

This is correct -- hardware FPU for all float operations.

**However, there are double-precision constants in hot paths.** The FPV5-D16
FPU handles single-precision in 1 cycle but double-precision operations fall
back to software emulation (~20-70 cycles each). Found in the filter inner
loop:

| File | Line | Code | Problem |
|------|------|------|---------|
| `ResonantFilter.c` | 141 | `tanhXdX(0.5*in)` | `0.5` is `double`, promotes `in` to double |
| `ResonantFilter.c` | 167 | `1.0 - f_lp2` | `1.0` is `double`, promotes entire expression |

`softClipTwo()` is called from `SVF_calcBlockZDF()` for every sample of every
voice (6 voices x 96 samples = 576 calls per block). Line 141's double
promotion means each call does: float→double conversion, double multiply,
double function call, double→float conversion. At ~20-70 cycles of software
double emulation per call, this wastes **11,520-40,320 cycles per block** --
up to **~0.19ms or ~9% of the render budget**.

**Additional double literals** exist elsewhere but are less hot.

---

## 10. Are there heavy trig operations replaceable with LUTs?

**Yes.**

| Function | File:Line | Calls/block | Cost | Replaceable? |
|----------|-----------|-------------|------|-------------|
| `log2f()` | `Oscillator.c:57` | Up to 12 (once per osc freq change) | ~50-100 cycles | Yes -- integer approx or LUT |
| `fastTan()` | `ResonantFilter.c:89` | 6 (once per filter recalc) | ~15 cycles (polynomial) | Already a fast approx -- OK |
| `tanhXdX()` | `ResonantFilter.c:129` | 576-1728 (per-sample, per-voice) | ~10-15 cycles | Already a Pade approx -- OK |
| `softClipTwo()` | `ResonantFilter.c:138` | 576-1728 | ~15 cycles + double penalty | Fix double literal, then OK |
| `fabsf()` | `distortion.c:50` | 576 | ~1 cycle (HW) | Already optimal |
| `sinf()` | Oscillator.c | 0 in production (test only) | N/A | N/A |

The polynomial approximations (`fastTan`, `tanhXdX`) are already efficient.
The main trig concern is `log2f()` from libm, but it's only called per-block
(not per-sample), so its impact is moderate.

**The real wins are not trig replacement but fixing the double-precision
promotions and enabling caches.**

---

## Additional Findings

### VLA in DrumVoice.c (line 228)

```c
void calcDrumVoiceSyncBlock(..., const uint8_t size) {
    int16_t modBuf[size];  // VLA -- forbidden per MEMORY.md
```

This VLA was supposed to be eliminated (Session 8/12 fixed Snare/Cymbal/HiHat).
DrumVoice.c was missed. `size` is always `OUTPUT_DMA_SIZE` (96), so this
allocates 192 bytes on the stack at runtime instead of using a static buffer.
With -O2, the compiler may not optimize this as well as a fixed-size array, and
stack probing adds overhead. Should be `static int16_t modBuf[OUTPUT_DMA_SIZE]`.

### Float division per sample in hot loops

- `BufferTools.c:120`: `i/(size-1.f)` -- float division every iteration, called
  3x per block (once per DrumVoice). Should precompute `1.f/(size-1.f)` and
  multiply.
- `ResonantFilter.c:213`: `1.f / (1.f + f*t0*2*R)` -- float division per
  sample per voice. Cortex-M7 VDIV.F32 takes ~14 cycles. 6 voices x 96
  samples = 576 divisions = ~8,064 cycles = ~37us.
- `distortion.c:50`: `(1+dist->shape)*x/(1+dist->shape*fabsf(x))` -- float
  division per sample per voice.

### No link-time optimization (LTO)

The Makefile does not use `-flto`. DSP code spans 10+ translation units
(Oscillator.c, ResonantFilter.c, BufferTools.c, distortion.c, DrumVoice.c,
Snare.c, CymbalVoice.c, HiHat.c, mixer.c). Without LTO, the compiler cannot
inline across files. Critical per-sample functions like `bufferTool_satAdd16()`
are `static inline` in headers (good), but `calcDistBlock()`,
`SVF_calcBlockZDF()`, `tanhXdX()`, `softClipTwo()` are all cross-TU calls.

### -O2 vs -Ofast for DSP files

`-Ofast` enables `-ffast-math` which permits float reordering, assumes no
NaN/Inf, and enables more aggressive optimization. The DSP code does not
depend on strict IEEE 754 semantics. Using `-Ofast` for DSP source files
specifically could yield 5-15% speedup.

### mixer_decimateBlock() overhead at rate 1.0

`mixer.c:71-85`: When decimation rate is 1.0 (the common case), every sample
still evaluates a float comparison and assignment. The loop should short-circuit
when the rate product is >= 1.0.

### audioOutBuffer placement

`AudioCodecManager.c:178-179`: The render buffers are in SRAM1 (default .bss).
The mixer writes these intensively during render (96 stores per half per voice).
They could be placed in DTCM for guaranteed single-cycle access. Both the
writer (main loop mixer) and reader (`pack_half()` in DMA ISR) are CPU code,
not DMA, so DTCM is accessible. The DMA reads from `dma_buffer`/`dma_buffer2`,
which correctly remain in SRAM1.

---

## Priority List (ordered by estimated impact)

1. **DONE Enable Cortex-M7 I-Cache** -- Immediate ~15-25% DSP speedup. Zero risk to
   DMA coherency. Two register writes in `sysclk_init()`:
   `SCB->ICIALLU = 0; __DSB(); __ISB(); SCB->CCR |= SCB_CCR_IC_Msk; __DSB(); __ISB();`

2. **DONE Change `if` to `while` for audio render** (`main.c:179`) -- Ensures both
   queue slots are filled before servicing UI. Eliminates the case where UI
   tasks delay the second render by a full loop iteration. Trivial change.

3. **DONE Fix double-precision constants in ResonantFilter.c** -- Change `0.5` to
   `0.5f` (line 141) and `1.0` to `1.0f` (line 167). Eliminates software
   double emulation in the hottest per-sample loop. Saves ~9% of render budget.

4. **DONE Enable D-Cache with MPU** -- Configure MPU to mark DMA buffer region as
   non-cacheable, enable D-cache for everything else. Speeds up all SRAM1
   accesses and flash data reads (wavetables). ~5-15% DSP speedup. Requires
   care but well-documented pattern for STM32F7.

5. **DONE Move audio render to top of main loop** -- Currently UI tasks run first
   (`main.c:152-176`), then render, then SD. Audio should be serviced
   immediately when the queue has free slots, before any UI work.

6. **DONE Precompute reciprocal in bufferTool_addGainInterpolated()** --
   Replace `i/(size-1.f)` with `i * inv_size` where `inv_size = 1.f/(size-1.f)`
   is computed once before the loop. Saves 3 x 96 = 288 float divisions per
   block.

7. **DONE Fix DrumVoice.c VLA** (line 228) -- Change `int16_t modBuf[size]` to
   `static int16_t modBuf[OUTPUT_DMA_SIZE]`. Consistent with fixes already
   applied to Snare/Cymbal/HiHat.

8. **DONE Enable LTO** (`-flto` in Makefile CFLAGS and LDFLAGS) -- Allows cross-TU
   inlining of DSP functions. Particularly benefits the many small functions
   called per-sample from filter/distortion/oscillator code. ~5-10% speedup
   expected.

9. **DONE Use -Ofast for DSP source files** -- Add a separate Makefile rule for
   `Core/DSPAudio/*.c` with `-Ofast` instead of `-O2`. Enables
   `-ffast-math` for float reordering and NaN/Inf assumption removal.

10. **DONE: Swap NVIC priorities** -- Move TIM6 and TIM7 below DMA audio (priority 5-6
    instead of 1-2). Audio DMA pack becomes priority 2-3. Reduces worst-case
    ISR preemption of the DMA pack operation. Match the target IRQ table in
    MEMORY.md.

11. **DECLINE (better to leave the render budget known): Short-circuit mixer_decimateBlock() at rate 1.0** -- Add early return when
    `mixer_decimation_rate[voiceNr] * mixer_decimation_rate[6] >= 1.0f`. Saves
    6 x 96 = 576 float comparisons per block in the common case.

12. **DONE : Place audioOutBuffer in DTCM** -- Tag with `INDTCMZ`. Written by mixer
    (main loop), read by `pack_half()` (DMA ISR) -- both CPU, not DMA.
    Guaranteed single-cycle access for the most-written buffers during render.

13. **DONE Use `__QADD16` intrinsics in BufferTools.c** -- Replace
    `bufferTool_addBuffersSaturating()` scalar loop with packed SIMD. Processes
    2 samples per cycle instead of 1. Also applicable to `bufferTool_clearBuffer()`
    (use 32-bit zero stores).

14. **DONE (for oscillator, tried for filter/dist, no observable impact) Map ITCM for hottest DSP functions** -- Add 16KB ITCM region to linker
    script, place `SVF_calcBlockZDF`, `calcNextOscSampleBlock`, inner oscillator
    loops. Zero-wait-state execution regardless of cache state. Do this after
    I-cache is enabled -- diminishing returns but useful for guaranteed timing.

15. **DECLINE (ideally, I want to REDUCE latency) Increase OUTPUT_DMA_SIZE to 128** -- Increases render budget from 2.18ms
    to 2.9ms (+33%). Increases total audio latency from ~4.35ms to ~5.8ms
    (imperceptible for a drum machine). Straightforward config.h change, but
    audit all `uint8_t` loop counters first (128 still fits in uint8_t).

### Session 073 additions (implemented 2026-09-28/29; record in `073_SESSION_HANDOFF_LOG.md` §6)

Estimates are static and cover the worst case. Class is the sound-impact
class defined in the Session 073 section above.

16. **DONE (S073 Step 0): host golden harness and ARM codegen checks.** The
    per-stage DWT profiler was not built per F-5; the worst-case Scene and
    hardware measurements remain user-owned.
17. **DONE (S073 Step 1): batched ZDF divisions** for both the voice int16
    path and Effect float path. The full host grid covers 276,480,000 samples
    per variant; S1 passed at 91.69 dB int16 / 86.55 dB float, with all >16-LSB
    cases confined to the cutoff 0.8/resonance 0.98 self-oscillating family.
    ARM object `vdiv` count is 81 → 39.
18. **DONE (S073 Step 2): string-free descriptor special writers.** The
    compile-time tags classify 155 registry rows with zero mismatches against
    the old classifier. Class S0.
19. **DONE (S073 Step 3): DMA pack as rotated 32-bit stores, with MPU region
    1 changed to Normal non-cacheable.** The host pack comparison reports zero
    differing bytes. Class S0; hardware ordering and audio regression remain
    user-owned.
20. **DONE (S073 Step 4): fused voice post-chain.** The constant-cost fused
    paths preserve the distortion calculation and report zero differing host
    samples; ARM operation-sequence checks MATCH. Class S0.
21. **DONE (S073 Step 5): dry path and FX send in one mixer pass.** The host
    comparison reports zero differing samples. ARM value-operation multisets
    MATCH for every dry routing × send type combination and for the
    send-only default case, after removing the input-sample and loop-index
    conversions the combined loop shares. Loop instructions per sample, old
    dry + send against combined:

    | Dry routing | Mono-input send | Stereo-input send |
    |---|---|---|
    | Single output (DAC1/DAC2 L or R) | 55 → 44 | 69 → 59 |
    | Stereo (DAC1/DAC2 stereo) | 73 → 63 | 87 → 78 |

    These are static counts from `make -C tools/dsp_test armcheck-mixer`.
    Class S0.
22. **DONE (S073 Step 6): wavetable octave selection by threshold table
    instead of `log2f()`.** The exhaustive host check reports 16 mismatches,
    all within 2 ulps and 0.000404 cents of an octave edge. Class S0 except
    those boundary choices. **ITCM +400 B:** after this change (the only
    S073 edit to `Oscillator.c`), `osc_setFreq()` (`INITCM`) is linked as
    its own 400 B function in ITCM. Before, it was inlined into its callers
    in flash and had no ITCM copy. Callers now reach it through an 8 B flash
    veneer. ITCM is 4,168 / 16,384 B (was 3,768).
23. **REJECTED (S073 Step 7): silence gating for idle voices.** It violates
    the constant-CPU policy and was not implemented.
24. **REJECTED (S073 Step 8): software PRNG for audio-rate noise.** It would
    change the noise character and add state; the hardware RNG remains in use.
25. **NOT RECOMMENDED:** the items in the Session 073 "Checked and rejected"
    table.
26. **Hardware result (user, 2026-09-29):** items 17–22 together gave about
    10 % less CPU on the user's worst-case Scene with the StereoFilter Effect
    (the plan estimated 9–14 %). Nothing was skipped to get it, so the saving
    holds with everything active.

### Session 074 additions (2026-09-29/30; record in `074_SESSION_HANDOFF_LOG.md` §7–§10)

These items **add** cost, so the worst case to budget against grows. All
of it is constant work except item 28, which is the one user-approved
exception to the constant-CPU rule: the compressor does nothing while `cmp`
is off.

27. **ADDED (S074): CrumpBit Effect (`cbt`).** When CrumpBit is the active
    Effect it runs on the FX bus instead of StereoFilter:
    - per sample, for both channels: an 8-bit ADC, the data-line masks, the
      DAC and the AC coupling;
    - per sample, the tape loop: glide, interpolated read, re-quantised
      write, ramped mix;
    - estimated 75–95 instructions per sample in total, against 160–206
      for StereoFilter's two ZDF channels;
    - per block: one `expf`, a StepScale walk and one divide (14 steps in
      S074; 128 positions since S078, `stepScale_ticksQ8()` — still a small
      per-block cost, not re-measured; the LTO build stopped unrolling the
      walk and text fell about 3.6 KB);
    - no per-sample division or transcendental.

    Every stage runs whatever the settings. Not measured separately on
    hardware; the user accepted the type on listening. Flash 11.6 KB
    (`-Ofast` unrolled the 32-frame loop: `crumpBit_process` 4,804 B,
    `crumpBit_syncDivision` 3,684 B).
28. **ADDED (S074): master bus compressor, while `cmp` is on.** Per block,
    one `log2f`, one `exp2f`, the knee, the two-stage cell and a few
    divisions (about 350–450 cycles). Per sample, the detector, the ramped
    gain, the cubic and the fade (34 instructions per stereo frame
    initially). Estimated 0.8–1 % of 216 MHz. **0 while off** (user
    decision; the stage returns after one settings read).
29. **ADDED (S074): compressor band-split saturation + full-scale knee.**
    This fixes HF harshness (aliased 3rd harmonic and kick–hat IMD).
    - The loop grew from 34 to 71 instructions per stereo frame (63
      floating point), so the compressor is about **1.4–1.8 %** while on.
    - The planning estimate was +0.45 %; the measured increase is +0.6–0.8 %
      (the knee's sign handling and FMA register copies).
    - The knee is branchless (`copysignf` → `vabs` + predicated `vneglt`).
    - The worst-case `cpu` widget reading with `cmp` on against off has not
      been taken yet.
30. **ADDED (S074): `xfd` fader mode.** One division per `xfd` slot per
    block (`adc_sliderGainMirrored()`). Otherwise the same as `fx`; the
    worst case is unchanged.
31. **ADDED (S074): mixer `off` branch.** Two stores per block while the
    Effect is `off` (the return-ramp reset). Negligible.

### Session 075 additions (2026-10-01/03; record in `075_SESSION_HANDOFF_LOG.md` §7, §10, §11)

S075 changed no voice engine or Effect DSP. Its audio-path changes are tiny;
the new copy/clear work is foreground-only and bounded.

32. **REMOVED (S075): global `srt` multiplier.** `mixer_decimation_rate[6]`
    and the `* rate[6]` multiply per voice decimation step are gone (DTCM
    −8 B). A Scene at 127 is bit-identical (×1.0f is exact): sound class S0.
33. **ADDED (S075 F2): FX-send Normal/Morph interpolation.**
    `preset_getEffectiveFxSendAmount()` runs six times per block (from
    `mixer_faderGains()`): one byte compare per voice when the endpoints are
    equal (every Scene without a Morph send); otherwise one LFO-layer scan
    (6 × 2 contributions), one integer lerp and, with an active LFO layer,
    one 12-step resolver pass. Under 1 µs per block at 216 MHz. Budget the
    worst case (all six voices with different endpoints and an LFO on their
    Morph); the equal-endpoint fast path returns the same value and is
    control-rate work, not a DSP bypass. There is no skip on send 0.
34. **CHANGED (S075 F2): Effect stereo pan law centred on 63** in the mixer
    return and the CrumpBit delay; same operation count.
35. **ADDED (S075): copy/clear service and the automation guard
    (foreground).** `ccSvc_tick()` at 500 Hz: at most 8 step placements, 16
    snapshot reads or 32 region entries per tick, and ≤ 0.1 % CPU on
    average while an older filesystem writer still runs (trickle governor).
    Nothing runs while no operation is pending. The F3 guard adds one bit
    test per Morph-worker parameter write; a menu edit now does one
    interpolation instead of queueing up to 64 parameter writes (less
    foreground work).

### Session 085 P1 additions (record in `085_SESSION_HANDOFF_LOG.md`)

36. **ADDED (S085 P1): Drum `rng`.** The ring law is folded into
    per-waveform carrier copies. It costs the plain loop + 0…4 per sample
    and has no separate pass. A separate pass (9 scalar, 7.5 packed) was
    measured and rejected: with user-sample oscillators it made `rng` ≥
    `fm` (89.5–91 vs 90).
37. **ADDED (S085 P1): Drum `snc`.** The FM-page oscillator is not
    rendered. The cost is a 7-instruction reset-list builder plus the
    slave's sync copy (plain + 10…11 on the reset path) and one VDIV per
    block. The dearest `snc` (crash slave, 48) is below the dearest `fm`
    (user samples, 90).
38. **UNCHANGED (S085 P1): `fm` and `mix`.** The existing loops are
    byte-identical in the linked firmware (`elf_osc_check`; one unused
    trailing literal-pool word is the only diff at
    `calcNextOscSampleBlock.constprop.0`). Device check: the user reported
    P1 "checks out completely fine on the hardware"; no `cpu` figures were
    recorded.
39. **CHANGED (S085 P1 follow-up): `rng` depth ramp.** `snc`/`rng` follow
    the mod envelope (d = amt² × env, the `fm` response; one multiply per
    block). The `rng` ring depth is ramped per sample from the last block's
    value: the ring copies cost about +3 per sample (table 31, sine 30,
    crash 33, noise 16–19, user sample 46–48 standalone / 49–50 linked)
    and one divide per block. The modulator renders at gain 1 through the
    baseline gain-1 clone. Dearest `rng` (user samples) ≈ 90–91 vs dearest
    `fm` ≈ 92 in the linked image: under `fm`, by the narrowest margin of
    any mode. Hardware (user): P1 passed; the ramp is to be re-tested.
40. **REJECTED (S085, first attempt, rolled back): the full oscillator
    cherry-pick from the S084 spike.** It added the types `pwm`/`smp`/`slp`/
    `wtb`, the run modes `rst`/`fre`/`itp`, a two-unit render pool and the
    first `snc`/`rng`.
    - **On hardware:** all instruments on `pwm` reached 100 % CPU and
      stalled the interface.
    - **Static worst cases:** Cymbal/HiHat 98 → 178 per sample; Drum `fm`
      68 → 135; `snc` 162; `rng` 135; system ≈ 1,278 per sample with the
      pool. `pwm` loops 42–58 plain.
    - **Shared plain/sync loops** added +2 to every plain render. ITCM
      reached 16,272 / 16,384 B before sharing.
    - **The cost was never measured on hardware before acceptance.**
    - **The rule that replaced it (user):** "no increase in CPU cycles
      overall", gated per stage on the device. Record:
      `085_SESSION_HANDOFF_LOG.md` §2; the failed tree
      `Helicase-dev-ph7-voice-pwm-spike/` (post-mortems there).

### Session 086 additions (record in `086_SESSION_HANDOFF_LOG.md`)

41. **ADDED (S086 P2): stock table waves `Re3`/`Sw3`.** Two `[11][1024]`
    tables in program flash (+45,056 B), played by the existing table
    loops through a variable table pointer (`osc_stockTables[]`): plain
    24 / 23, FM 35, the same sync and ring functions. No existing loop
    gained an instruction (`elf_osc86_check`, `elf_loops86_check`; the
    dispatchers changed by register allocation only). ITCM +720 B (the
    generic copies inlined into the ITCM dispatchers). Rejected before it:
    install-time generation into sample flash with new variable-shift
    readers (the sync copy was +2/+3, and the mechanism differed from the
    existing waves).
42. **ADDED (S086 P3/P4): `pos`, a continuous oscillator phase offset.** No
    per-sample cost: a shift and an add per trigger, and a per-block
    `phaseInc` bend (about 10 instructions per block per voice, ≈ 0.3 per
    sample) outside the loops. The alternative, a phase offset added inside
    every loop, would have cost +1 per sample (+3 with a per-sample ramp)
    in new loop copies.
43. **FOUND AND FIXED (S086 assessment F1): register pressure in the
    mixer.** The voice renders are inlined into
    `mixer_calcNextSampleBlock()`. The first `pos` version kept the bend in
    a local variable across the render; two inlined per-sample loops
    gained reloads (an FM loop with a Sine carrier 29 → 31, two flash
    literal loads; a noise loop 15 → 16, one stack reload). Keeping the
    step in `OscInfo` restored both to their P2 lengths.
44. **ADDED (S086): mixer hot-loop gate.** `tools/dsp_test/elf_hotloops.py`
    (`make elf86_mixer_check`) pairs the per-sample do-while loops of
    `mixer_calcNextSampleBlock()` with a P2 baseline by opcode sequence
    (layout-insensitive) and fails if one grew. Hardware (user): the
    six-slot worst-case Scene with an LFO on every `pos` reads the same
    `cpu` as before.

### Session 087 additions (record in `087_SESSION_HANDOFF_LOG.md`)

45. **REPLACED (S087): v1 user-sample loops removed.** The four v1
    dispatchers (`calcUserSampleOscBlock`, `calcUserSampleOscFmBlock`,
    `calcUserSampleOscRingBlock`, `calcUserSampleOscSyncBlock`) and
    `osc_syncWaveform()` are deleted. `osc_interp_a/b` (128 B DTCM)
    released. `stub_silence_buffer` (512 B flash) removed. Replaced by
    the type-aware readers below.
46. **ADDED (S087): `smp`/`slp` sample readers.** Eight loops in
    `.itcm.s087` (`osc_smpPlainBlock`, `osc_smpFmBlock`,
    `osc_smpSyncBlock`, `osc_smpRingBlock`, `osc_slpPlainBlock`,
    `osc_slpFmBlock`, `osc_slpSyncBlock`, `osc_slpRingBlock`). Q20.12
    phase, install-time zero padding, fixed loop region. Per-sample
    counts: `slp` plain 27, FM 49–50, ring 42; `smp` ring 38. All below
    the frozen Drum `fm` ceiling (≈ 92 linked).
47. **ADDED (S087): `wtb` wavetable reader.** Frame blending through two
    interpolation slots (stamps 1..255). `osc_wtbPlanBlock()` splits at
    frame crossings. Four blend copies (plain, FM, ring, sync). Blend
    inner loops: 34–38 / 42–46 / 43–47 / 44–48. Per-slot blend cost
    +10..+13 over one frame, against ≈ +66 today.
48. **ADDED (S087 P3): transient click sample list.** New sample loop: 20
    instructions per sample. Built-in click loop: 22 (unchanged). No
    worst-case increase.
49. **CHANGED (S087): ITCM.** 8,512 (S086) → 15,032 (S087 D, before P1
    v1 removal) → 12,640 (after P1). Net +4,128 B from S086. Build at
    S087 close: text 670,628, Flash headroom 83,036 B, ITCM 12,640 /
    16,384 B.

### Session 088 additions (record in `088_SESSION_HANDOFF_LOG.md`)

50. **ADDED (S088 P0): gain-matched amp retrigger.**
    `slopeEg2_retriggerFrom()` (control rate, one divide per trigger) and the
    `slopeEg2_isAudibleGain()` threshold (1/32768). No per-sample cost.
    `fre`/`itp` never cost more than `rst`: the comparison is per trigger.
51. **ADDED (S088 P0): post-chain gain ramps (Layer 2b).**
    `voicePost_drum()` gains a per-sample velocity ramp (+3 instructions per
    sample, 32 → 35); `voicePost_mixAddGainDist()` and
    `voicePost_addGainDist()` gain a per-sample amp ramp (+5, 30 → 35 and
    25 → 30). Paid in every mode, every block (constant CPU); with equal
    endpoints each ramp evaluates to exactly the block gain, so the S073
    output is bit-identical (host law B1, `run_modes`).
52. **ADDED (S088 P0): Drum FM depth ramp (D1).** The fm branch scales the
    modulator block by a linear ratio `lastFmDepth / fmMod` → 1: one float
    multiply plus the ramp add per sample on Drum `fm`, in every mode. When
    the voice is not armed the ratio is exactly 1, so the multiply is the
    identity and `rst` is unchanged. G1: the frozen Drum `fm` ceiling rises
    from ≈ 92 to ≈ 107 linked instructions per sample; re-measure with
    `fpseq.py` on the S088 ELF before acceptance.
53. **ADDED (S088 P0): itp crossfade.** One `OscCarrierSnapshot` render
    (the same plain carrier loop class the voice already runs) plus a linear
    blend (float multiply-add per sample, ≈ 8). Budgeted as the two-slot
    pool always full, exactly like the S087 E wavetable blend slots. The
    two slots are shared; ongoing `itp` fades are reserved before
    `modNode_resetTargets()`, so wavetable blending steps (3 ms) when both
    are held.
54. **CHANGED (S088 P0): filter coefficient path.** In `fre`/`itp`,
    `SVF_recalcFreqSlew()` replaces `SVF_recalcFreq()` at the same call site
    (once per block per slot): one `fastTan` (unchanged) plus one
    multiply-add. `rst` keeps the original function.
55. **CHANGED (S088 P1): float-through distortion.** All three post-chains
    now pass the pre-gained float directly to `distortion_curveSampleF()`
    instead of truncating to int16 first. Removes one int16 store per sample
    in the Drum chain (the two gain multiplies are merged). Snare and
    Cymbal/HiHat chains keep the saturation add as int16 but the amp gain ×
    distortion path is float. Net: text −80 B vs the P0 close. No per-sample
    cost increase (the division and the curve are unchanged).
56. **ADDED (S088 P1): block-rate fader smoothing.** One-pole EMA
    (`MIXER_FADER_SMOOTH_ALPHA` 0.15) on `mixer_fader_smooth[6]` in
    `mixer_faderGains()`, once per slot per block: one multiply-add and one
    compare per slot. Negligible; no per-sample cost.

### Session 089 P0 (per-instrument SRR enable)

57. **CHANGED (S089 P0): conditional SRR.** `instrumentManager_slotUsesSrr()`
    gates `mixer_decimateBlock()` per slot. Returns 1 for Drum/Snare/Cymbal/
    HiHat default chains. The S&H loop (10 instructions/sample) is unchanged
    and opcode-identical to HEAD. Mixer grew by 340 B (13,508 → 13,848) from
    the predicate branch; inlined set unchanged; 36 hot loops equal, 0 shrank,
    1 grew (dry-mix layout case 27→28, same executed instructions). `.bss`
    412,884 B unchanged. No new RAM. Flash +336 B.
58. **REVERTED (S089 P0 follow-up): integer S&H.** Q31 packed halfword pair
    reduced the S&H from 10 to 7 instructions per slot. Built and verified;
    reverted by user decision. Record: `S089_P0_NEW_SRR_DISABLE.md` §8.

### Session 089 P2 additions (record in `S089_P2_IMPLEMENTATION.md`)

59. **ADDED (S089 P2): WFd fold loops.** `voiceFold.c`: five per-sample
    loops in `.itcm.s089` (one is selected per block) and one non-inlined
    per-block entry, `voiceFold_process()`, in flash. Measured on the
    linked build with `elf_loopdump.py`:

    | Loop | Types | Instructions |
    |---|---|---:|
    | `voiceFold_triLoop` | Tri, TrD, TrI | 12 |
    | `voiceFold_chbLoop` | Chb | 14 |
    | `voiceFold_fd2Loop` | Fd2 | 20 |
    | `voiceFold_sinLoop` | Sin, SnD | 22 |
    | `voiceFold_asnLoop` | Asn | 22 |

    All are within the P2 limit (24 per sample, the click's 22 + mix-in 2;
    Cymbal/HiHat 26). The sine family was one loop at 23 as first
    delivered; splitting Sin/SnD from Asn removed one accumulator copy per
    sample (assessment F4). Every loop is branch-free (|x| is an IT block).
    `voiceFold_process()` does the per-block setup (type, frq, depth,
    decay, ramp constants) once per voice per block.
60. **ADDED (S089 P2): `voicePost_gainDist()`.** The no-add twin of
    `voicePost_addGainDist()`, used by Cymbal/HiHat under WFd. The linked
    mixer shows two 21-instruction per-sample loops (one inlined per
    engine), in place of the 25-instruction add versions under T+D
    (−4 per sample).
61. **CHANGED (S089 P2): chain dispatch.** One `chainType` branch per
    engine render (and per trigger/async snap path). With `chainType` 0 the
    T+D path runs as before: `elf_hotloops.py` on the P0 mixer vs this
    mixer reports 31 hot loops equal, 6 shrank, 0 grew.
62. **CHANGED (S089 P2): ITCM and flash.** ITCM 12,656 → 13,032 B
    (+376 B; D4): the fold loops only, after `voiceFold_process()` (1,036 B)
    moved to flash (assessment F2; it was 13,960 B as first delivered). No
    existing ITCM function moved; only the three linker veneers were laid
    out after `.itcm.s089`. Text 676,600 → 681,232 B (+4,632 B), flash
    headroom 72,008 B. `.bss` size 412,884 B and `runtime_slots` 7,584 B are
    unchanged, but `.data` 420 → 424 B shifts the start of `.bss` by 8 B
    (`STORAGE_SRAM_MANIFEST.md`, S089 P2).

### Session 089 P3 (Drum FM depth ramp efficiency)

63. **REDUCED (S089 P3): Drum FM depth ramp folded into carrier loops.**
    Option B: ramped twins of the 3 dearest FM carriers (`slp`, `smp`, `wtb`
    blend) replace the separate 10-instruction depth-ramp pass. Each twin =
    original + 1 `vadd.f32`. Flash dispatcher `osc_renderTypedFmRamp()`
    selects by `oscType`; stock carriers keep the S088 pass unchanged.

    | Carrier | Today | Folded | Saving |
    |---|---:|---:|---:|
    | `slp` (`osc_slpFmRampBlock`) | 90 | 81 | −9 |
    | `wtb` blend (`osc_wtbFmBlendRampBlock`) | 86 | 77 | −9 |
    | `smp` (`osc_smpFmRampBlock`) | 82 | 73 | −9 |

    Twin hot loops: `slp` 51/40, `smp` 43, `wtb` blend 47/43, `wtb`
    fallback 33/35 — each original + 1. ITCM 13,032 → 15,240 B
    (`.itcm.s089p3`, 2,208 B). Text 683,512 B (+2,280 B vs P2). `.bss`
    412,884 B unchanged. Mixer: 39 hot loops equal, 0 shrank, 0 grew.
    Host laws FR1 (unarmed = original, S0) and FR2 (armed ramp endpoints).
    Hardware H1–H6 pending at S089 handoff.

### Session 089 CPU ceiling (binding, S089 P2 §8)

64. **BINDING (S089 P2): no per-sample DSP cost may increase.** The
    worst-case Scene reached 100 % on the `cpu` widget after P2. The user
    declared: "absolutely no possibility to increase anything at this point."
    This covers every voice engine, chain, oscillator mode/type, the mixer,
    the bus, and the Effects. Any new DSP feature must be paid for by an equal
    or larger measured saving elsewhere, gated with `elf_loopdump.py`,
    `elf_hotloops.py`, and the inlined-set diff, and confirmed on the `cpu`
    widget. A new chain/fold may only replace stages within its measured
    budget. The unused margin under WFd budgets is not headroom — the
    worst-case Scene is set by the dearest settings across the system.

### Session 091 P1 (record in `S091_P1_Reductions_Implementation.md`)

65. **REDUCED (S091 P1): CrumpBit (`cbt`), bit-identical (S0).** Item 27
    under-counted the type: the S078 Sync walk ran all 128 StepScale
    positions every block (128 × 12–19 = 1,536–2,432 instructions,
    ≈ 48–76 per frame), on top of a 111-instruction per-sample loop
    (≈ 191 per frame in the worst case, not 75–95).
    - R1: a fixed 7-step binary search returns the same division
      (117 instructions per block, straight line).
    - R2: the ADC uses `USAT` instead of two float clamps and a fold.
    - R3: `io->frames` is read once.
    - R4: AC coupling on integer code differences with a fixed-point
      `vcvt #7`, the same single-rounding VFMA as before.
    - Linked loop 111 → 98 per frame; total ≈ 191 → ≈ 106 per frame.
      Text +40 B; RAM 0 B; mixer loops unchanged (86/86 exact).
    - Host laws CB1–CB5 bit-exact (`make -C tools/dsp_test crumpbit`);
      gate `crumpbit_elf_check`. Hardware: H1–H6 of the schedule.

66. **GREW WITHIN THE S091 SAVING (S091 P2): CrumpBit expansion.**
    Elastic delay (whole arena), feedback to 1.2, DC blocker, tone tilt
    and a cubic soft knee on the return, and a stereo TPT output filter
    (eight voice types, Q <= 8) as a second pass. Drives deferred.
    - Linked: stage-2 loop 98 -> 126 per frame (register pressure: six
      float constants and four literals re-materialised, five stack
      slots), output-filter loop 31, per block ~15-19: ~106 -> ~172-176
      per frame, below the pre-S091 ~191 (item 61). No per-sample VDIV.
    - Per-block tables replace expf, fastTan and valueShaper divisions;
      the Rate limit (101 instructions) runs once per block in
      effects_service().
    - RAM 0 B (runtime 76 B fills the union); text +3,576 B.
    - Host CB1-CB9, five breaks; gate crumpbit_elf_check.

67. **CHANGED (S091 P3): CrumpBit sub-types.** Four new sub-engines
    (`cmb` comb, `pit` two-head pitch shifter, `grn` pitched granular
    plus delay line, `rvb` 8-bit 4-line FDN), one per block by `sub`,
    each its own frame loop; shared dry path, mix stage and output filter.
    - Linked worst case per frame (loop + output 31 + per block):
      `dly` 124 -> 172 (HEAD 173), `cmb` 124 -> 168, `pit` 135 -> 181,
      `grn` 131 -> 192, `rvb` 145 -> 192. No per-sample VDIV.
    - Reductions inside the engines: integer 0.32 head phase and Q14
      head reads (`pit`), VCVT #7 lanes and packed XOR (`rvb`), two
      spans around the block's one grain restart and no delay glide
      (`grn`), block fields as locals (all).
    - RAM +12 B (runtime 88 B); text +12,864 B.
    - Host CB1-CB15, nine breaks; gate crumpbit_elf_check per engine.

68. **CHANGED (S091 P4): CrumpBit `grn` rework, filter balance.** Grains
    triggered by Density independently of Length, flat-top windows with
    exact 128-frame crossfades, steal-oldest, per-grain overwrite clamp,
    anchored freeze, own delay-time curve; shared `bal` (per-channel
    output-filter coefficients).
    - Linked worst case per frame: `grn` 131 -> 136 loop, ~192 -> ~201
      whole Effect; `dly`/`cmb`/`pit`/`rvb` +0.7 (`bal`). No per-sample
      VDIV.
    - RAM +12 B (runtime 100 B); text +2,840 B.
    - Host CB1-CB16, twelve breaks.

69. **REDUCED (S091 P5): CrumpBit sub-type efficiencies.** User cap: 170
    instructions per frame for the whole Effect, every sub-type, worst
    case, gated by `crumpbit_cpu_check`. Q14 tape read (`dly`/`cmb`);
    drop-sample `pit` heads; low-pass-only `rvb` damping; `fbk`/`dpn` per
    block (`mix` keeps its ramp, user); stage 1 through a 256 B table;
    `grn`: Position clamped in the engine (cached fit), grain ends on block
    boundaries and no window lower clamp, plain echo below unity; a one-pole
    output filter in `grn`/`rvb`.
    - Linked worst case per frame: `dly` 174.2 -> 160.0, `cmb` 170.3 ->
      153.8, `pit` 183.6 -> 153.6, `grn` 203.0 -> 166.5, `rvb` 194.9 ->
      163.9. No per-sample VDIV.
    - RAM +260 B `.bss` (SRAM1); runtime 100 B unchanged; text +456 B.
    - Rejected after measurement: X3 (filter in the engine loops, +6..+13),
      R3 (+1.1), V2 (no saving), V4 (hardware: the reciprocal table is up
      to 10 cycles per sample slower; `VDIV` overlaps other work on the
      M7). Plan: `S091_P5_SUBTYPE_EFFICIENCIES.md` §5.
    - Host CB1-CB19, eighteen breaks.
    - **P5 follow-up (parameter pass):** all four pitch rows changed to ±36 st;
      new `fin` row (row 23, ±52 cents fine pitch); `EFFECT_MOD_RANGE(lo,hi)`
      macro replaces hand-coded range pairs; pitch window glide; `snp` inert in
      `pit`. Host CB21 (pitch ±36), CB22 (band centre ratio). `pit`
      153.6 → 154.1 per frame. Flash 710,240 → 710,876 B. Row count 23 → 24.

### Session 091 P6 (CrumpBit parameter cleanup)

70. **CHANGED (S091 P6): CrumpBit parameter cleanup.** Labels hook replaces
    `name3` in EffectsManager registry. `dpn` renamed to `spn` ("SubPan").
    `snc` removed from `dly` sub-type lanes. New 128-step dly delay time
    table (`crumpBit_dlyTime[128]`) with tempo-relative curve and 13 stops
    (/64 through 2br); replaces old Sync binary-search path and `rateLimit`
    call for dly. `pit` interval labelled "pit"/"Shift"/"SemiPtch". `grn`
    below /64: 4 positions below lowest stop, showing row number. Tap fade:
    one-block linear fade on `grn` time-parameter changes via `tap_d`/`tap_on`
    state; grain frame loop 116 → 114.
    - New host law CB23 (tap fade), breaks 23–25, `runGrain` gate 114.
    - Linked worst case per frame: `dly` 157.6, `cmb` 153.8, `pit` 154.1,
      `grn` 165.3, `rvb` 163.9 (worst 165.3 ≤ 170 cap).
    - Flash 712,684 B (headroom 40,980 B). RAM unchanged from P5.
    - Host CB1–CB23, twenty-five breaks.

### Session 093 P4 (Alkaline instrument; record in `S093_P5_FIRMWARE_IMPLEMENTATION_PHASE_2.md`)

71. **ADDED (S093 P4): Alkaline (TB-303) voice, Basic, within the Drum
    ceiling.** A fifth tagged engine (`AlkalineVoice` 540 B, 548 B after
    S093 P6's `AlkVeg.keep` + `attack`), its descriptor table (26 rows, 28
    after S093 P6), the B2R diode ladder and the N1 network (r6: 6 states on
    one balanced basis, 64 samples).
    - Linked per-sample loop (G3): fused oscillator + B2R, **214** (3 VDIV)
      inside `Alkaline_calcVoiceSyncBlock.constprop.0`; standalone 213
      (`DiodeLadder_renderB2Osc`, inlined by LTO).
    - Linked per block (G4): `Alkaline_calcVoiceAsync` 884 (9 VDIV) +
      sync-block entry/exit 332 + dispatch 14 = **1,230 (9 VDIV)** after
      S093 P6 (`pos` bend +13, VEG factor −1, drive and MEG +0). The P4
      figure was 1,218; the stub link gave 1,285 (915 + 356 + 14).
    - Whole voice (G5): 214 + 1,230/32 = **252.5 per sample, 3 + 9/32
      VDIV**, against the Drum ceiling **261.3** (frozen in S093 P3 and
      re-measured on the P4 image): margin ≈ 8.8. The fused loop is still
      214 after S093 P6 (no P6 edit is in the sync block); the attack
      interpolation runs in the trigger, and Res ×1.5 was measured at 0 per
      sample.
    - S093 P6 flash: +1,152 B (4 Res points) + 20 B (5 prewarp points) +
      108 B (attack table) and the code; headroom 47,836 B (the `make all`
      of this phase). Text 705,408 B, data 420 B, bss 413,280 B;
    - P4 flash: headroom 49,412 B;
      ITCM 14,552 B (+40 B of entry code in five existing `.itcm.s087`
      blend functions; no new ITCM code; accepted as LTO layout, A1);
      `menu_instrumentLoadIndex` +2 B (R1).
    - Mixer G6: 30 hot loops equal, 0 grew (`elf_p4_check`), still 0 after
      S093 P6. By cyclic cost
      (`elf_loopsrc.py --compare`, each loop's longest in-range cycle paired
      by source line): 41 -> 42 cyclic loops, every group equal; the one
      extra loop is a second 10-instruction `mixer_decimateBlock` copy (the
      per-slot SRR stays 10); no loop grew. Oscillators G7: 93/93 exact
      loops; `osc_setFreq` per block 128.
    - **Device `cpu` result: pending** (OPEN_TESTS A5: Alkaline in the four
      Basic slots at Res full, drive x16 after S093 P6, accents and slides, against a Drum
      baseline Scene; 0 underruns over 5 min). If the device confirms a
      failure, the fallback is Advanced (K1, the user's decision).

### Session 093 P8 budget findings (measured 2026-11-10; record in `S093_P8_WAVETABLE_REASSESSMENT.md` §14)

Method for all items: linked P6 and P8 images, `elf_looppath.py
--cond-heads --in-range` (per-sample loops), `elf_blockpath.py` and
`control_cost.py --elf` (per block), and a `-g` twin of the P8 image for
mixer-inlined loops (`elf_loopsrc.py` helpers, cyclic cost). "Dearest" is the
most expensive choice an oscillator can make.

72. **MEASURED (S093 P8): wavetable readers after the 8-bit removal.** One
    16-bit loop per reader, with the fixed 1,024-point read:

    | Reader | P6 (8-bit / 16-bit copy) | P8 |
    |---|---:|---:|
    | plain | 26 / 24 | 22 |
    | blend | 38 / 34 | 33 |
    | FM / FM blend | 34 / 46 | 30 / 41 |
    | FM ramp / FM blend ramp | 35 / 47 | 31 / 42 |
    | ring / ring blend | 34 / 47 | 30 / 42 |
    | sync / sync blend | 36 / 48 | 32 / 45 |

    - In P8 the blend readers also show an outer per-segment cycle (61–85,
      shortest 14–19). It runs when a block crosses a frame (at most twice per
      block), not per sample. `elf_looppath.py --top 1` reports it first; use
      `--top 4` and take the per-sample inner loop.
73. **MEASURED (S093 P8): the per-voice worst cases did not change.** No
    wavetable reader was ever the dearest choice for its oscillator:

    | Oscillator role | Dearest (P6 = P8) | Next | Wavetable reader (P8) |
    |---|---|---|---|
    | Plain (modulators, Snare tonal, Drum `mix`) | crash 30 | `slp` 27 | plain 22, blend 33 |
    | Drum FM carrier | `slp` FM ramp 51 | `smp` 43 | FM blend ramp 42 |
    | Cymbal/HiHat FM carrier | `slp` FM 50 | `smp` 42 | FM blend 41 |
    | Drum `rng` carrier | `slp` 42 | `smp` 38 | ring blend 42 |
    | Drum `snc` slave | `slp` 41 (no slot); the wavetable sync blend 45 with a slot | crash / `smp` 36 | sync 32, sync blend 45: the dearest slave, but the `snc` mode stays below Drum's dearest mode (item 77) |

    Per voice, no slot held: Drum **261.3** (unchanged, the Basic ceiling);
    Snare and Cymbal/HiHat unchanged; Alkaline 252.5 (P6) → **255.2** (P8
    image, item 78).
74. **MEASURED (S093 P8): the crash wave's cost.** `crashSample`
    (unsigned 8-bit, 32,769 B flash) is read by:
    - the plain path inside `calcNextOscSampleBlock` (the `ldrb` loop):
      **30 per sample, the dearest plain read in the system**. Every
      modulator and plain oscillator is budgeted at 30 because of it;
    - `calcSampleOscFmBlock` 32, `calcSampleOscRingBlock` 33,
      `calcSampleOscSyncBlock` 36 (ITCM 168 + 196 + 176 B). None is the
      dearest in its mode.

    Without crash: Drum 261.3 → 258.3, Snare −3, Cymbal/HiHat −6, and the
    `itp` fade's per-slot cost 45 → 42 (item 75).
75. **MEASURED (S093 P8): the interpolation pool's ceiling is the `itp`
    fade.** What one slot adds on top of its voice's dearest no-slot
    setting:

    | Slot user | P6 | P8 |
    |---|---:|---:|
    | Wavetable blend on a plain oscillator | 38 − 30 = 8 | 33 − 30 = 3 |
    | Wavetable blend on an FM, ring or sync carrier | 0 | 0 |
    | `itp` fade: old carrier rendered plain (`oscSection_renderOldCarrier()`: crash 30 / `slp` 27 / `smp` 24 / wavetable plain) + the 15-instruction crossfade | **45** | **45** |

    - A blended carrier is never dearer than its voice's dearest setting, so
      it adds nothing. The `itp` fade runs on top of the live carrier, so all
      of it is extra.
    - The two-slot pool's worst case is **90** (84 without crash), set by the
      `itp` fade on both images. Item 47's "+10..+13" is the blend premium
      over the same reader, not the pool's charge.
76. **MEASURED (S093 P8): the Advanced ceiling (dearest Cymbal; HiHat runs
    the same loops).** T+D chain, both modulators on crash, `slp` FM
    carrier. Not frozen into `tools/dsp_test/baselines/`.

    | Per-sample loops | Instr. | VDIV |
    |---|---:|---:|
    | Modulators, crash × 2 (`calcNextOscSampleBlock`) | 60 | 0 |
    | Modulator sum (`bufferTool_addBuffersSaturating`, unrolled) | ≈ 2.5 | 0 |
    | Carrier `osc_slpFmBlock` | 50 | 0 |
    | `SVF_calcBlockZDF.constprop.0`, dearest type | 92 | 2 |
    | Transient click (mixer-inlined) | 22 | 0 |
    | `voicePost_addGainDist` (mixer-inlined) | 25 | 1 |
    | **Loops** | **251.5** | **3** |

    - **Per block ≈ 1,006 (31.4 per sample):**
      - async: `Cymbal_calcAsyncVoice` 62 + `osc_setFreq` × 3 at 128;
      - dispatch 14; filter recalculation ≈ 30 (S093 P3 figure);
      - `Cymbal_calcSyncBlockVoice` entry 133 (standalone `-Ofast`; LTO
        inlines it into the mixer);
      - render entries: `SVF_calcBlockZDF` 161, `calcNextOscSampleBlock`
        17 × 2, `osc_slpFmBlock` 71 + `osc_renderTypedFm` ≈ 12 (P3);
      - `pos`: `osc_typedPositionApply` 73 (`__aeabi_ldivmod` not counted, as
        in P3) + restore ≈ 11 (P3);
      - `transient_calcBlock` ≈ 21 (P3).
    - **SRR:** 10 (Cymbal/HiHat use `mixer_decimateBlock`).
    - **Total ≈ 292.9 per sample, 3 VDIV** (≈ 286.9 without crash). The WFd
      chain (fold ≤ 22 + `voicePost_gainDist` 21) is 4 lower than T+D.
    - The S093 P0 estimate (≈ 270) used `slp` modulators and no per-block
      count.
77. **MEASURED (S093 P8): the wavetable sync blend.** Inner loop 45: the
    frame read (22) + `osc_syncPull()` (≈ 10: `over[i]`, `umull`, `smull`,
    conditional add) + the second frame and mix (≈ 11) + **two stack
    reloads** (`inc`, `ratioQ16`). The plain blend and the plain sync copy
    have none; only their combination exceeds the integer registers.
    - It runs only on Drum osc1 in `snc` with a slot. That mode totals
      builder 7 + 45 = 52, against Drum's dearest 81 (FM): it sets no worst
      case, and its slot excess is 0.
    - Reading frame B through one pointer at +2,048 B measured 45 (no
      change).
78. **MEASURED (S093 P8): the Alkaline on the P8 image.**
    - Fused loop `DiodeLadder_renderB2Osc` 214 (3 VDIV).
    - Per block (`control_cost.py --elf`): async 956 + sync block 349 +
      dispatch 14 = **1,319** (9 VDIV).
    - Whole voice **214 + 41.2 = 255.2** (P6: 252.5), under 261.3.
    - Per block by stage (standalone `control_cost.py`, LTO inlines them in
      the image):

      | Stage | Instr. | VDIV |
      |---|---:|---:|
      | `alkMeg_step` | 247 | 2 |
      | `dl_slowUpdate` | 228 | 0 |
      | `alk_expoCurrent` | 190 | 3 |
      | `alkAcc_step` (`ALK_ACC_SOLVES` 1) | 129 | 3 |
      | `alkMeg_n53` | 73 | 0 |
      | `alkVeg_step` | 45 | 0 |
      | `alk_prewarp` | 36 | 1 |
      | `DiodeLadder_setNet` | 32 | 0 |

    - **Implemented in S094 P1.** (S093 P8 revision 3, D12/D13): frame
      blending with an interpolation slot.
      - Prototype: blend loop 228, no-slot loop 213, per block 1,391.
      - Whole voice 256.5 (no slot) / **271.5 (slot held)**. The slot-held
        figure is over 261.3; the user accepts it for now. Its excess (+15)
        is below the pool's per-slot ceiling (45, item 75).
79. **METHOD (S093 P8): findings for later measurements.**
    - **A call that writes `modulationNode` state from an engine's sync
      render moves the mixer's register allocation.** The Alkaline
      prototype called `modNode_interpClaim()` in `alk_renderWtb()`: two
      mixer hot loops grew by 1 (a decimation loop 10 → 11, a table shaper
      36 → 37). A stub with no global writes passed, and so did the claim
      moved to the async. Engine-side claims belong in the async.
    - **In a copied tree, plain `make` can build nothing.** With the
      dependency files present, the default goal becomes `build/main.o`, and
      copied timestamps look current. Use `rm -rf build && make all` in
      every scratch copy.

### Session 094 findings (measured 2026-11-11/12; records in S094_P2_ALKALINE_SAVINGS.md, S094_P1_IMPLEMENTATION.md)

80. **IMPLEMENTED (S094 P1 S2): crash wave removed.** `crashSample` deleted
    from firmware. The plain crash loop (30 per sample) was the dearest plain
    read in the system. Without it: Drum 261.3→258.3, Snare −3,
    Cymbal/HiHat −6, `itp` fade 45→42, pool 90→84. Card sample
    `SD_CARD/samples/02_crash.wav` replaces it (playable as `slp` or
    modulator `l` entry). ITCM −≈540 B; flash −32,769 B.
81. **IMPLEMENTED (S094 P1 S3): PWM oscillator type on Drum, Snare, Cymbal,
    HiHat.** `typ` 4 `pwm`. Hybrid mechanism: phase warp (W) for Sin/Tri,
    two-read (D) for Saw/Rec. Integer Q15/Q14 interpolation. 15 ITCM loops
    in `.itcm.s094p1`. Every PWM loop at or below its role ceiling. The key
    finding: integer interpolation pays for the second read (float two-read
    30, integer two-read 23). No voice's worst case rises. ITCM +≈2.4 KB.
82. **IMPLEMENTED (S094 P1 S4): Alkaline VCA overdrive + tone.** OTA input
    gain as per-block variable (`otaIn`); one-pole low-pass tone in `c` form
    on OTA output inside the B2R loop. A3 drive/makeup from tables (−40 per
    block). The three new live floats pushed the loop into spill reloads:
    stack loads/stores 30→41. The tone costs 6 per sample; the overdrive
    about 2. Loops: one-row 213→221, blend 228→234. Whole voice
    256.1→263.7 (no slot). Overdrive 0 / Tone 127 = bit-identical to before
    (P9-D7). The new accepted worst case ≈261.5.
83. **IMPLEMENTED (S094 P1 S5): Alkaline PWM.** Fused mode 3 in
    `dl_renderB2Body()`, `DiodeLadder_renderB2OscPwm()`. Read form 2
    (A interpolated, B truncated): 204 instructions. `ALK_TYP_PWM 2u`. The
    Alkaline's cheapest path (256.6). Removing it saves nothing on the worst
    path.
84. **IMPLEMENTED (S094 P1 S1): LFO sync fix and rate table.** New
    `IM_SPECIAL_LFO_SYNC` (writer 23, count 24). `lfoRateHz[128]` table:
    `frq` 0 = stopped, 1 = 100 s, semitone grid 63..127. Sync wins: rate
    scale 1.0 while synced. `LFO_MAX_F` removed.
85. **DISCOVERY (S094 P2 §13): SRAM D-cache was effectively disabled since
    Session 013.** MPU region 0 had S=1 (shareable). On Cortex-M7 with
    CACR.SIWT=0, shareable means the D-cache is OFF for SRAM. Fix: clear S
    bit. Result: −14.8% on the same Scene. Six Alkalines from pinning the
    CPU to 80.9%. All voices benefited (Alkaline −20.2%, drums −10-15%).
86. **IMPLEMENTED (S094 P2 §22): QADD.** `bufferTool_satAdd32()` replaced
    with `__QADD()` intrinsic. Bit-exact. Stereo dry add: 41-42→26
    instructions/sample. With send: 56-57→33-34.
87. **IMPLEMENTED (S094 P2 §22): Cymbal and HiHat moved to Basic.**
    `INSTRUMENT_FLAG_BASIC` set. No current type is Advanced. The Advanced
    machinery preserved for future types.
88. **ADOPTED (S094 P2 §21): cycle-based budget.** 97% average cap (152,004
    of 156,705 cycles per block). Replaces S089's instruction-based gates.
    Budget: 4 Basic × 19,620 + 2 Advanced × 21,803 + Effect 16,634 +
    shared mixer 12,784 + outside-mixer 4,701 + 500 slack.
89. **IMPLEMENTED (S094 P2 §23): CrumpBit resonant SVF restored on
    `grn`/`rvb`.** S091 P5 F1 had replaced it with one-pole. `grn` 180.8
    instructions/frame. Cap 170→181. About 16,280 cycles against 16,634
    Effect share.
90. **IMPLEMENTED (S094 P2 §10.2): post-filter overdrive doubled.**
    `OVD_OCT` 4.0→5.0 (gain ×1..×32), `OVD_MAKEUP_OCT` −2.0→−2.5.
91. **IMPLEMENTED (S094 P2 §10): cycle probe.** DEV_MODE_LOGGING-only probe
    using DWT_CYCCNT around each slot's async+sync. Per-slot block-rate and
    render timing. Seven mixer sections. Trace stage `p`. 94 B RAM (logging
    builds only).
92. **IMPLEMENTED (S094 P4): PWM position law fix.** Old law: pos 64 =
    unmodified, both directions narrow (half the knob wasted). New law:
    pos 0 = 50%, pos 127 = narrowest. 128 distinct widths from
    position-indexed tables. Narrowest: Rec 0.39%, Saw/Tri 2.54%,
    Sin 6.25%. No per-sample cost change.
93. **IMPLEMENTED (S094 P4): Alkaline waves expanded.** `wav` gains Tri
    (index 2): 0 per sample in existing fused loop. `pwm` gains Tri
    (index 2): new fused ladder mode.

---

## Binding Mandates — DSP CPU Policy

> **Any session that changes DSP cost must read and follow these rules.**
> (User, 2026-09-28)

- **Never save CPU by skipping, bypassing or switching off DSP work** because
  an element is currently inactive, silent, or set to zero (distortion at 0,
  a silent voice, a send at 0, and so on).
- **Why:** freed CPU gets filled by other features. Then, when everything is
  requested at once, the result is underruns or features dropped at random.
- **Headroom is only the worst case with everything active** (the stress
  Scene peak), never the average.
- **Any proposal** that turns processing off for a feature that can be active
  must be raised with the user. The expected answer is no.
- **CPU reductions must also meet the sound rules** agreed for each change
  (classes S0 bit-identical … S4 audible). The S073 decisions are in
  `073_SESSION_HANDOFF_LOG.md` §6.
- **How to prove a DSP change:** `tools/dsp_test/` (host comparison plus the
  ARM `fpseq.py` check; `DSP_TEST.md`).
- **The one approved exception (S074):** the master bus compressor does no
  work while `cmp` is off. With it on, its cost (about 1.4–1.8 %) counts
  against the worst-case Scene; the user accounts for it on the `cpu`
  widget.
