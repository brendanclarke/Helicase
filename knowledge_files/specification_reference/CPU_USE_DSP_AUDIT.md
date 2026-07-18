# LXR-02 DSP Performance Audit

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
