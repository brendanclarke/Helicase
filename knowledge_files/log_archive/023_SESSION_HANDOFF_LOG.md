# Session 023 Handoff Log

## Session Header

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: Refactor and clean up the codebase, reduce CPU spikes, improve foreground/DSP scheduling, audit remaining DSP efficiency items, and bound oscillator interpolation CPU use without changing sound quality.  
**Last session summary**: Session 022 widened the mixer/output/codec path to true signed-24 audio via `sample_mx_t`, fixed the loudness regression caused by an extra `>>8`, and left the `dth` global menu option planned but not wired.  
**Working repository**: `/Users/bc/LXR02Open/LXR02-current/lxr02-037_port` local working directory. At session end this directory is expected to become the project git repository.  
**Constraints today**: Preserve sound quality, keep DSP render in the main loop, do not bypass the decimator, keep sample interpolation as a headline feature, and leave `AUDIO_DMA_FRAMES` at the stable 96-frame setting.

Key files used for context:

- `README.md`
- `MEMORY.md`
- `knowledge_files/log_archive/000_SESSION_INDEX.md`
- `knowledge_files/DSP_AUDIT.md`
- `knowledge_files/OSC_INTERP_AUDIT.md`
- `AUDIT_REFACTOR.md`
- `MEMORY_AUDIT.md`

---

## Session Narrative

### Phase 0 - Audit and Plan

The session began by reading the current project context and writing `AUDIT_REFACTOR.md`. The audit focused on:

- interrupt scheduling and high-priority CPU spikes,
- foreground service work that could be moved away from audio-critical windows,
- hardware reads, menu/display refresh, SD/filesystem polling, and sample-load behavior,
- remaining relevant items from `knowledge_files/DSP_AUDIT.md`,
- oscillator interpolation cost from `knowledge_files/OSC_INTERP_AUDIT.md`,
- memory and ITCM/SRAM/DTCM headroom after the refactor.

Audit conclusion: the audio architecture is still correct. DSP render should remain foreground/main-loop work and should not move into the DMA ISR. The main loop already renders all free audio queue slots with `while (audioCodec_queueFreeSlots() > 0)`, so the remaining CPU wins were not about fixing a one-block render bug. The pressure points were:

- high-priority service interrupts preempting foreground DSP,
- expensive foreground math happening more often than human UI response needs,
- oscillator interpolation multiplying normal oscillator render work,
- menu/storage completion bursts happening after audio starts,
- idle filesystem polling burning foreground cycles when no operation is active.

The audit recommended a staged implementation:

1. replace slider `powf()` taper with a LUT,
2. rate-limit idle filesystem polling,
3. move heavy TIM6 work out of the ISR,
4. lower TIM7 display service cadence/priority,
5. optionally guard 32-frame DSP subblocks with BASEPRI,
6. amortize post-load/global apply work,
7. keep SD polling in the main loop for now,
8. apply sound-neutral DSP cleanup only where measurable,
9. bound oscillator interpolation rather than removing the feature.

User decisions that shaped implementation:

- Use the full 4096-entry slider LUT, derived from `SLIDER_LOG_TAPER_DB`.
- Do not do the decimator full-rate bypass; always-on decimation must remain part of the CPU budget.
- Reduce UI servicing by roughly 50%, as imperceptibly as possible.
- Use 5kHz for display servicing.
- Try the refactors down through phase 3 item 10.
- Do not move SD polling to an ISR yet.
- Try SIMD/word buffer helpers.
- Try oscillator ITCM, then audit ITCM usage.
- Try filters/distortion in ITCM, but revert if hardware CPU monitor looks worse.
- Do DSP follow-up items 11-14; never do DSP proposal 15.
- For oscillator interpolation limiting: do A, do not do B, add the C budget in `config.h`, do D, skip E. Current active test setting is two slots.

### Phase 1 - Low-Risk CPU Wins

**Slider taper LUT**

`Core/Hardware/frontPanel/IO/adcPots.c` now builds a 4096-entry `float` LUT at `adc_init()`. The LUT is computed from `SLIDER_LOG_TAPER_DB` in `config.h`, so the dB taper remains configurable at compile time. `adc_checkPots()` now indexes the LUT instead of calling `powf()` in the main loop.

Implementation paths:

- `config.h`
- `Core/Hardware/frontPanel/IO/adcPots.c`
- `Core/Hardware/frontPanel/IO/adcPots.h`

Trade-off accepted: 16KB SRAM1 for the LUT. This was considered affordable; `MEMORY_AUDIT.md` later confirmed SRAM1 remains comfortable. The benefit is avoiding up to twelve `powf()` calls per foreground slider scan.

**Idle filesystem polling rate limit**

`Core/Hardware/SD/filesystem.c` now keeps active/busy operations polling every call, but when the public filesystem facade is idle it polls asyncfatfs only every 5ms. This preserves active SD transfer progress while reducing idle foreground CPU load.

Implementation path:

- `Core/Hardware/SD/filesystem.c`

Important rule preserved: asyncfatfs is still single-context. `afatfs_poll()` must not be called from both main loop and an ISR.

**Decimator bypass intentionally not implemented**

`AUDIT_REFACTOR.md` proposed an early return in `mixer_decimateBlock()` when effective rate is full-rate. User explicitly rejected this for now. Decimation remains always in the CPU budget and can still drop in at any time.

### Phase 2 - Interrupt and UI Servicing Reduction

**TIM6 refactor**

TIM6 used to run 1kHz high-priority service work, including shift-register exchange, PB jack detect, encoder-button debounce, endless-pot scanning, and counters. Session 023 reduced the ISR to:

- clear update flag,
- increment `time_sysTick`,
- increment `screensaver_timer`,
- increment a foreground service-due flag every other tick.

The heavier service work now runs in `timebase_serviceFrontPanel()` from the main loop:

- `dout_latch()`,
- `din_dout_exchange()`,
- PB4/PB6 output jack detect sampling,
- `encoder_tick()`,
- `endlessPots_tick()`.

This shifts the front-panel service cadence to about 500Hz and moves the expensive work out of interrupt context.

Implementation paths:

- `Core/Hardware/timebase.c`
- `Core/Hardware/timebase.h`
- `main.c`

Current TIM6 priority: 6.

**TIM7 LCD drain**

TIM7 LCD servicing changed from 10kHz to 5kHz and moved to low priority. The LCD queue design remains the head/tail SPSC queue from earlier sessions. TIM7 still self-stops when the queue is empty and restarts on enqueue.

Implementation paths:

- `Core/Hardware/frontPanel/lcd.c`
- `Core/Hardware/frontPanel/lcd.h`
- `Core/Hardware/timebase.c`

Current TIM7 priority: 7.

**BASEPRI DSP subblock guard**

Each 32-frame `mixer_calcNextSampleBlock()` call is wrapped in a narrow BASEPRI guard. The guard masks only low-priority service interrupts at priority 6 and lower. Timing-critical sources stay unmasked, including SysTick, TIM1 encoder capture, TIM3 sequencer owner, EXTI edge capture, USART3, USB priority if configured above the threshold, and audio DMA.

Implementation path:

- `main.c`

Important constraint: the guard covers one 32-frame DSP/control subblock, not a full 96-frame hardware DMA slot. It must not be replaced by global `cpsid i` around the whole render slot.

**Current Session 023 interrupt/service shape**

| Source | Role | Priority / cadence |
| --- | --- | --- |
| SysTick | canonical LXR tick | 4kHz, priority 0/effective highest |
| TIM1_CC | main encoder A/B capture | priority 1 |
| TIM3 | sequencer timing owner | 4kHz, priority 2 |
| EXTI4/EXTI9_5 | CLK/RST/OUT1 edge capture | priority 3 |
| DMA1 Stream4/7 | audio pack/flag clear | priority 4 |
| USART3 | DIN MIDI RX/TX | priority 5 |
| OTG_FS | USB MIDI | priority 5 |
| TIM6_DAC | counters + service flag only | 1kHz, priority 6 |
| TIM7 | LCD state machine | 5kHz when active, priority 7 |

### Phase 3 - Menu, Storage, and Load Bursts

**Global apply amortization**

`menu_sendAllGlobals()` can be a post-load burst if called after audio starts. Session 023 added a small foreground worker in `menu.c`. Boot-time globals still apply immediately before audio starts. Post-boot global/all-file load completion now sends a small number of globals per foreground pass and repaints/resets save parameters when the worker completes.

Implementation path:

- `Core/Menu/menu.c`

**Sample and loop load combined**

The Load page now exposes one sample-load operation: `Load: Samples`. Selecting it performs the old operations sequentially:

1. display sample upload status,
2. stop the sequencer and suspend audio,
3. install accepted WAVs from `/samples`,
4. display loop upload status,
5. append accepted looped WAVs from `/loops`,
6. resume audio once,
7. refresh the sample count and repaint.

The visible `SampLoop` menu option was removed. The underlying loop installer remains intact and is called by the combined operation.

Implementation paths:

- `Core/Menu/menu.c`
- `Core/Menu/menu.h`

**Modal load display cleanup**

The screen could freeze between sample and loop loading with partially rendered text. Session 023 added `lcd_waitForIdle()` and used it in `menu_setStorageMessage()` so modal sample/loop status screens physically drain through TIM7 before flash erase/program work blocks.

Implementation paths:

- `Core/Hardware/frontPanel/lcd.c`
- `Core/Hardware/frontPanel/lcd.h`
- `Core/Menu/menu.c`

Important constraint: `lcd_waitForIdle()` is only for rare modal paths that already suspend audio. It should not be used in normal menu repaint paths.

### Phase 4 - DSP Hot-Path Cleanup

**BufferTools packed helpers**

`bufferTool_addBuffersSaturating()` and `bufferTool_subBuffersSaturating()` now process two int16 samples per word with ARM packed saturating operations:

- `__QADD16`
- `__QSUB16`

Odd trailing samples use the existing scalar saturating helper. Clear helpers now use `memset()`.

Implementation path:

- `Core/DSPAudio/BufferTools.c`

The implementation uses memcpy-style word movement to stay safe with unaligned int16 buffers and strict aliasing/LTO.

**Mixer fused output helper**

The mixer now snapshots effective jack routing once per 32-frame block and combines several post-voice passes into one helper:

- slider gain interpolation,
- int16 voice output to signed-24 `sample_mx_t`,
- pan,
- route,
- saturating output add.

This preserves the voice synthesis path and slider smoothing while reducing memory traffic and repeated routing work.

Implementation path:

- `Core/DSPAudio/mixer.c`

Decimator remains separate and always active by design.

**Oscillator frequency cache**

`osc_setFreq()` now caches the last effective frequency and waveform. If `freq * pitchMod * modNodeValue` and waveform are unchanged, the oscillator skips:

- `log2f()` in wavetable table-index selection,
- phase increment recalculation,
- wavetable octave/table selection.

Exact float equality is used deliberately so pitch/modulation behavior does not change.

Implementation paths:

- `Core/DSPAudio/Oscillator.h`
- `Core/DSPAudio/Oscillator.c`

**User-sample metadata cache**

User-sample metadata is cached per oscillator:

- sample pointer,
- size,
- loop flag,
- sample index,
- `SampleMemory` generation.

`SampleMemory` increments its generation counter on refresh after sample/loop install. Oscillators refresh cached metadata when the sample index or generation changes.

Implementation paths:

- `Core/DSPAudio/Oscillator.h`
- `Core/DSPAudio/Oscillator.c`
- `Core/SampleRom/SampleMemory.c`
- `Core/SampleRom/SampleMemory.h`

This is sound-neutral and also prepares for the unfinished long-sample playback work.

### Phase 5 - Oscillator Interpolation Limits

`OSC_INTERP_AUDIT.md` showed that waveform interpolation could be extremely expensive. The previous block path could evaluate both neighboring waveforms through recursive one-sample dispatcher calls for every sample. One interpolating oscillator in a 32-frame block could therefore cause 64 one-sample oscillator dispatches, plus struct copies and blend work.

Implemented policy:

- Interpolation remains globally controlled by `PAR_OSC_WAVE_INTERP`.
- Interpolation is limited to main audible oscillator waveform targets:
  - Drum1 main oscillator,
  - Drum2 main oscillator,
  - Drum3 main oscillator,
  - Snare main oscillator,
  - Cymbal wave1,
  - HiHat wave1.
- FM/mod oscillator waveform targets are not interpolated by default.
- User-sample interpolation is retained.
- Per-block active interpolation budget is configurable as `OSC_WAVE_INTERP_MAX_ACTIVE`.
- Current active test config sets `OSC_WAVE_INTERP_MAX_ACTIVE=2`.
- Extra eligible targets snap to integer waveform IDs for that block.
- The scalar one-sample recursion path was replaced for block rendering by two full-block clone renders into DTCM scratch buffers, followed by a blend loop.

Implementation paths:

- `config.h`
- `Core/DSPAudio/modulationNode.c`
- `Core/DSPAudio/modulationNode.h`
- `Core/DSPAudio/Oscillator.c`
- `Core/DSPAudio/Oscillator.h`

Explicitly not implemented:

- Restricting waveform classes away from user samples. User samples remain eligible because interpolating user samples is a headline feature.
- Additional diagnostic hooks; existing hooks are considered sufficient.

### Phase 6 - ITCM Experiment

Session 023 added linker/startup support for ITCM hot-code placement:

- `STM32F765VIHx_FLASH.ld` defines a 16KB ITCM region and `.itcm` section.
- `Core/Src/startup_stm32f765xx.s` enables ITCM/DTCM interfaces and copies `.itcm` from flash before C code runs.
- `config.h` defines placement macros:
  - `INITCM`,
  - `INITCM_EFFECT`,
  - `INITCM_EFFECT_NOINLINE`.

Test results and final state:

- Oscillator ITCM placement is enabled: `ENABLE_OSC_INITCM_CODE=1`.
- Filter/distortion ITCM placement was tried but appeared to increase CPU use on the hardware CPU monitor.
- Filter/distortion ITCM placement was reverted to disabled: `ENABLE_EFFECT_INITCM_CODE=0`.
- The annotations remain in `ResonantFilter.c` and `distortion.c` so future A/B tests can re-enable them with one config switch.

Implementation paths:

- `config.h`
- `STM32F765VIHx_FLASH.ld`
- `Core/Src/startup_stm32f765xx.s`
- `Core/DSPAudio/Oscillator.c`
- `Core/DSPAudio/ResonantFilter.c`
- `Core/DSPAudio/distortion.c`

### Phase 7 - Encoder Direction-Change Fix

The main Gray-code encoder could get into a state where reversing direction required two clicks, and that offset persisted across menus. The pass found stale sub-detent residue in the Dannegger accumulator as the likely cause.

Session 023 added `enc_last_step` and clears only partial residue (`enc_delta` between -3 and +3) when the accepted transition direction reverses. Full pending counts are preserved.

Implementation path:

- `Core/Hardware/frontPanel/IO/encoder.c`

This should avoid spending the first opposite detent cancelling stale partial travel.

### Phase 8 - AUDIO_DMA_FRAMES Investigation

The session briefly investigated changing `AUDIO_DMA_FRAMES`:

- 64 frames increased interrupt cadence and reduced foreground slack enough to freeze/glitch during pattern-load testing.
- 128 frames would provide more slack but failed to link because `.dma_nocache` is currently limited to a 4KB MPU window.
- The stable session-end value remains `AUDIO_DMA_FRAMES=96`.

Exact 128-frame failure:

- `dma_buffer` size at 128: `128 * 8 * sizeof(int16_t) = 2048 B`.
- `dma_buffer2` size at 128: `2048 B`.
- The two audio DMA buffers alone consume `4096 B`.
- `adc_dma_buf[14]` also lives in `.dma_nocache`, adding 28 bytes plus alignment.
- The linker assertion correctly fails: `.dma_nocache` exceeds the 4KB MPU region.

To support 128 safely in a future session:

- increase MPU Region 1 in `Core/Hardware/clocks.c` from 4KB to 8KB,
- increase the linker assertion/window in `STM32F765VIHx_FLASH.ld`,
- pad/reserve the whole 8KB window so normal `.data` does not start inside non-cacheable memory,
- update `MEMORY_AUDIT.md` after rebuilding.

No 128-frame support was implemented this session.

---

## Audit Documents Used

### AUDIT_REFACTOR.md

`AUDIT_REFACTOR.md` began as the proposal for Session 023 and was marked up as implementation landed.

Implemented items from the audit:

- 4096-entry slider LUT derived from `SLIDER_LOG_TAPER_DB`.
- 5ms idle filesystem polling rate limit.
- TIM6 ISR reduced to counters plus foreground service flag.
- Front-panel service moved into `timebase_serviceFrontPanel()`.
- TIM7 LCD reduced to 5kHz and lowered in priority.
- BASEPRI guard around each 32-frame DSP subblock.
- Packed int16 saturating add/sub helpers.
- Mixer post-voice output helper fusion.
- Per-oscillator sample metadata cache with `SampleMemory` generation.
- `osc_setFreq()` effective frequency/waveform cache.
- Oscillator interpolation cap with `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current active test build.
- Oscillator interpolation full-block clone render path.
- ITCM placement support, final state oscillator-only.
- Amortized post-load/global apply worker.
- Combined sample/loop load menu operation.
- LCD idle drain before modal flash write stages.
- Encoder sub-detent residue clear on direction reversal.
- Memory audit in `MEMORY_AUDIT.md`.

Not implemented by decision:

- Full-rate decimator bypass.
- Moving SD polling into an ISR.
- Approximate nonlinear filter division or reciprocal math.
- Restricting user-sample interpolation.
- Additional oscillator interpolation diagnostic hooks.

DSP_AUDIT follow-up status recorded in the audit:

| DSP audit item | Session 023 status |
| --- | --- |
| Render all free audio slots | Already implemented; kept. |
| I-cache / D-cache / MPU | Already implemented; kept. |
| audioOutBuffer in DTCM | Already implemented; kept. |
| LTO and `-Ofast` for DSP | Already implemented; kept. |
| ResonantFilter hot double literals | Already mostly fixed; avoid regressions. |
| DrumVoice VLA | Already fixed; do not revert. |
| BufferTools reciprocal in gain interpolation | Already fixed; kept. |
| NVIC priority redesign | Partially implemented through TIM6/TIM7 reductions and priorities. |
| Full-rate decimator bypass | Explicitly not implemented. |
| SIMD buffer helpers | Implemented targeted helpers. |
| ITCM placement | Implemented support; final enabled state oscillator-only. |
| Larger output block size | Declined; `OUTPUT_DMA_SIZE` must remain 32. |

Additional DSP proposals from the audit:

- Item 11, cache oscillator frequency calculations: implemented.
- Item 12, cache user-sample metadata: implemented.
- Item 13, fuse mixer post-voice passes: implemented.
- Item 14, cache effective output routing per block: implemented.
- Item 15, do not approximate nonlinear filter division: respected.

Oscillator interpolation audit decisions:

- A, main-audible oscillators only: implemented.
- B, restrict waveform classes/user samples: not implemented by user decision.
- C, per-block active interpolation budget: implemented with current config value 2.
- D, replace recursive one-sample evaluation: implemented for block rendering.
- E, add diagnostics/pressure bypass: not implemented by user decision.

### MEMORY_AUDIT.md

`MEMORY_AUDIT.md` was written after the refactor and ITCM A/B work. The build used for the audit had oscillator ITCM enabled and effect ITCM disabled.

Build/audit commands recorded:

```sh
make clean
make -j4
make img
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-objdump -t build/lxr02.elf
arm-none-eabi-nm --print-size --size-sort build/lxr02.elf
```

Build result:

- `build/lxr02.elf` linked successfully.
- `build/LXRV2_lxr02.img` generated successfully.
- Existing warnings remained:
  - nano syscall stubs (`_close`, `_lseek`, `_read`, `_write`),
  - LTO serial compilation notice,
  - legacy asyncfatfs/USB warnings.
- No memory overflow occurred.

Section usage from the audit:

| Section | Size | Address | Notes |
| --- | ---: | ---: | --- |
| `.isr_vector` | 456 B | `0x08008000` | Vector table in application flash. |
| `.text` | 226,824 B | `0x080081c8` | Flash code and rodata. |
| `.itcm` | 3,808 B | `0x00000000` | Oscillator INITCM code copied from flash. |
| `.dma_nocache` | 3,100 B | `0x20020000` | DMA-visible SRAM1, 4KB MPU cap. |
| `.data` | 344 B | `0x20020c1c` | Initialized SRAM1 data. |
| `.bss` | 106,364 B | `0x20020d88` | Zero-init SRAM1 data. |
| `.dtcm` | 35,168 B | `0x20000000` | Initialized DTCM data. |
| `.dtcmz` | 6,092 B | `0x20008960` | Zero-init DTCM data. |

Region summary:

| Region | Capacity | Used | Free | Used |
| --- | ---: | ---: | ---: | ---: |
| ITCM | 16,384 B | 3,808 B | 12,576 B | 23.2% |
| DTCM | 131,072 B | 41,260 B | 89,812 B | 31.5% |
| `.dma_nocache` MPU window | 4,096 B | 3,100 B | 996 B | 75.7% |
| SRAM1 static sections | 376,832 B | 109,808 B | 267,024 B | 29.1% |

Stack and flash notes:

- `_estack` remains `0x20080000`.
- Static SRAM1 allocation ended at `_ebss = 0x2003acf4`.
- Distance from `_ebss` to stack top was about 283,404 B.
- Application flash region is 480KB (`0x08008000-0x0807ffff`).
- Loaded image ended at `_eflash_load = 0x08049168`.
- Image size was 266,600 B, about 54.2% of the app flash region.

ITCM details:

- `.itcm` VMA: `0x00000000`.
- `.itcm` size: `0x0ee0` / 3,808 B.
- Flash load address: `0x0803f7d0`.
- Linker stubs: 24 B inside `.itcm`.
- Confirmed ITCM symbols include oscillator block/render/cache paths:
  - `calcFmBlock.constprop.*`,
  - `calcSampleOscFmBlock.constprop.0`,
  - `osc_setFreq`,
  - `calcUserSampleOscFmBlock.constprop.0`,
  - `calcNextOscSampleFmBlock.constprop.0`,
  - `calcUserSampleOscBlock.constprop.0`,
  - `calcNextOscSampleBlock.constprop.*`.

DTCM details:

- DTCM total used: 41,260 B.
- Notable residents:
  - `transientData`: 26,460 B,
  - `sine_table`: 8,194 B,
  - `audioOutBuffer`: 1,536 B,
  - `audioOutBuffer2`: 1,536 B,
  - `voiceArray`: 1,536 B,
  - snare/cymbal/hat state: about 992 B combined,
  - `osc_interp_a` and `osc_interp_b`: 64 B each,
  - mixer decimation/routing/slider state and modulation state.
- Assessment: DTCM has ample headroom; CPU-only render buffers are safe here; DMA buffers must not move to DTCM.

SRAM1 details:

- Static SRAM1 section total: 109,792 B.
- Largest residents:
  - `seq_patternSet`: 50,528 B,
  - `slider_lut`: 16,384 B,
  - `sample_manifest`: 13,440 B,
  - `seq_tmpPattern`: 6,316 B,
  - `afatfs`: 4,556 B,
  - `USB_OTG_dev`: 1,524 B,
  - `usb_MidiMessages`: 2,048 B,
  - `parameterArray`: 1,824 B,
  - `sample_info_cache`: 1,440 B,
  - `install_info`: 1,440 B.
- Assessment: SRAM1 still has substantial static headroom. The full slider LUT is an intentional and affordable trade-off.

DMA non-cacheable window:

- Capacity: 4,096 B.
- Used: 3,100 B.
- Free: 996 B.
- Current residents:
  - `dma_buffer2`: 1,536 B,
  - `dma_buffer`: 1,536 B,
  - ADC DMA buffer and alignment/fill account for the remainder.
- Assessment: this is the tightest memory-adjacent resource. Future DMA-visible buffers need relocation, resizing, or another/enlarged MPU non-cacheable region.

Watch items from the memory audit:

1. The 4096-entry slider LUT intentionally spends 16KB SRAM1 to remove repeated foreground `powf()`.
2. Current ITCM state is oscillator-only; re-enable filter/distortion ITCM only if future measured targets benefit.
3. `.dma_nocache` has less than 1KB free and must be treated as constrained.
4. Stack reservation is still implicit; consider a linker assertion if SRAM growth becomes aggressive.

---

## Files Changed

Code and configuration:

- `config.h`: slider LUT comments; `AUDIO_DMA_FRAMES=96` documented as the stable setting; `OSC_WAVE_INTERP_MAX_ACTIVE=2`; ITCM placement switches documented.
- `main.c`: foreground service call order; BASEPRI guard around each 32-frame DSP subblock; comments.
- `Core/Hardware/timebase.c`: TIM6 reduced to counters/service flag; `timebase_serviceFrontPanel()` added; TIM6 priority 6; TIM7 handler remains delegate.
- `Core/Hardware/timebase.h`: foreground service API and timer documentation.
- `Core/Hardware/frontPanel/lcd.c`: TIM7 5kHz; priority 7; `lcd_waitForIdle()`.
- `Core/Hardware/frontPanel/lcd.h`: `lcd_waitForIdle()` declaration and comments.
- `Core/Hardware/frontPanel/IO/adcPots.c`: 4096-entry slider LUT; foreground lookup path.
- `Core/Hardware/SD/filesystem.c`: idle poll rate limit.
- `Core/DSPAudio/BufferTools.c`: packed `__QADD16`/`__QSUB16` helpers and memset clear.
- `Core/DSPAudio/mixer.c`: fused output helper; per-block routing cache.
- `Core/DSPAudio/Oscillator.c`: frequency cache, sample metadata cache use, block interpolation scratch/render path, INITCM annotations.
- `Core/DSPAudio/Oscillator.h`: oscillator cache fields.
- `Core/DSPAudio/modulationNode.c`: interpolation budget/generation state and eligible target filtering.
- `Core/DSPAudio/modulationNode.h`: interpolation control declarations as needed.
- `Core/SampleRom/SampleMemory.c`: sample generation counter and refresh invalidation.
- `Core/SampleRom/SampleMemory.h`: generation getter.
- `Core/Menu/menu.c`: global apply worker; combined sample/loop load; modal LCD drain.
- `Core/Menu/menu.h`: load/save enum/menu changes for removing visible `SampLoop`.
- `Core/Hardware/frontPanel/IO/encoder.c`: sub-detent residue clear on direction reversal.
- `Core/DSPAudio/ResonantFilter.c`: ITCM effect annotations remain but compile to normal functions with current config.
- `Core/DSPAudio/distortion.c`: ITCM effect annotations remain but compile to normal functions with current config.
- `STM32F765VIHx_FLASH.ld`: ITCM memory region and `.itcm` section; `.dma_nocache` remains 4KB.
- `Core/Src/startup_stm32f765xx.s`: ITCM/DTCM interface enable and `.itcm` copy.

Audit and project docs:

- `AUDIT_REFACTOR.md`: full Session 023 proposal, implementation progress, trade-offs, and risk notes.
- `MEMORY_AUDIT.md`: SRAM/ITCM/DTCM/DMA non-cache memory audit.
- `README.md`: updated for local working directory, Session 023 current state, sample/loop menu change, TIM6/TIM7 changes, and CPU refactor facts.
- `MEMORY.md`: updated for local working directory, Session 023 current state, current IRQ/service model, sample loading, and known issues.
- `knowledge_files/SESSION_HANDOFF_TEMPLATE.md`: updated to use working repository language.
- `knowledge_files/log_archive/000_SESSION_INDEX.md`: appended Session 023 row, summary, cross-session facts, and future append template.
- `knowledge_files/log_archive/023_SESSION_HANDOFF_LOG.md`: this file.

---

## End of Session Block

```
DATE: 2026-05-17
SESSION GOAL: Refactor/clean up code and improve CPU efficiency, especially interrupt scheduling, UI/display/filesystem service load, DSP hot paths, and oscillator interpolation budget.

COMPLETED:
- Wrote AUDIT_REFACTOR.md with scheduling, foreground service, DSP, and oscillator interpolation proposals.
- Implemented 4096-entry slider taper LUT derived from SLIDER_LOG_TAPER_DB.
- Rate-limited idle filesystem polling while preserving active operation polling.
- Reduced TIM6 ISR to counters + foreground service flag; moved shift-register exchange, PB jack detect, encoder-button debounce, and endless-pot scan to timebase_serviceFrontPanel().
- Reduced TIM7 LCD servicing from 10kHz to 5kHz and lowered it to priority 7.
- Added narrow BASEPRI protection around each 32-frame DSP subblock.
- Added targeted packed int16 BufferTools helpers.
- Fused mixer slider interpolation, int16-to-24-bit conversion, pan, routing, and output add in one per-voice helper.
- Cached effective output routing once per 32-frame block.
- Cached oscillator frequency setup and user-sample metadata.
- Added oscillator interpolation active-budget policy with current OSC_WAVE_INTERP_MAX_ACTIVE=2.
- Replaced oscillator interpolation block rendering with full-block clone renders plus blend.
- Added ITCM linker/startup support; final measured state is oscillator-only ITCM enabled, effect ITCM disabled.
- Amortized post-boot global apply work after globals/all-file load.
- Combined sample and loop loading into one visible Load: Samples operation.
- Added modal LCD idle wait so sample/loop status screens render cleanly before flash write stages.
- Fixed main encoder direction-change sub-detent residue.
- Wrote MEMORY_AUDIT.md with SRAM, DTCM, ITCM, .dma_nocache, stack, and flash usage.
- Updated README.md, MEMORY.md, SESSION_HANDOFF_TEMPLATE.md, and 000_SESSION_INDEX.md for Session 023 and local working repository wording.
- Added code comments around all Session 023 touch points.
- Investigated AUDIO_DMA_FRAMES=64 and 128; kept AUDIO_DMA_FRAMES=96 as the stable setting.

VERIFIED ON HARDWARE:
- Partially by user observation during the session: filter/distortion ITCM appeared to increase CPU monitor usage and was reverted; AUDIO_DMA_FRAMES=64 froze/glitched during pattern load; AUDIO_DMA_FRAMES=128 failed link due to the 4KB .dma_nocache MPU window.
- Build verification was recorded in MEMORY_AUDIT.md for the oscillator-only ITCM state.
- Full hardware regression pass for UI feel, MIDI timing, sample loading, and audio underruns remains recommended.

CHANGES THIS SESSION:
- config.h: AUDIO_DMA_FRAMES documented at 96; OSC_WAVE_INTERP_MAX_ACTIVE=2; slider LUT and ITCM switches documented.
- main.c: BASEPRI DSP guard and foreground service scheduling.
- Core/Hardware/timebase.c/h: TIM6 lightweight ISR, 500Hz foreground front-panel service, current priorities.
- Core/Hardware/frontPanel/lcd.c/h: 5kHz TIM7 drain, priority 7, lcd_waitForIdle().
- Core/Hardware/frontPanel/IO/adcPots.c: 4096-entry slider LUT.
- Core/Hardware/SD/filesystem.c: idle poll rate limiting.
- Core/DSPAudio/BufferTools.c: QADD16/QSUB16 helpers and memset clear.
- Core/DSPAudio/mixer.c: fused per-voice output helper and routing snapshot.
- Core/DSPAudio/Oscillator.c/h: oscillator frequency cache, sample cache, interpolation block path, INITCM placement.
- Core/DSPAudio/modulationNode.c/h: oscillator interpolation budget and target filtering.
- Core/SampleRom/SampleMemory.c/h: sample generation counter for DSP cache invalidation.
- Core/Menu/menu.c/h: global apply worker, combined sample/loop load, modal display drain.
- Core/Hardware/frontPanel/IO/encoder.c: direction-reversal residue handling.
- Core/DSPAudio/ResonantFilter.c and Core/DSPAudio/distortion.c: effect ITCM annotations kept but disabled by config.
- STM32F765VIHx_FLASH.ld and Core/Src/startup_stm32f765xx.s: ITCM section/copy support.
- AUDIT_REFACTOR.md and MEMORY_AUDIT.md: new/updated audit documentation.
- README.md, MEMORY.md, knowledge_files/SESSION_HANDOFF_TEMPLATE.md, knowledge_files/log_archive/000_SESSION_INDEX.md: updated project/session documentation.

KNOWN ISSUES INTRODUCED:
- None intentionally. Remaining risk is hardware regression risk from scheduling changes: UI/button/encoder feel, LCD latency, MIDI/clock timing under load, sample load flow, and audio underrun behavior need bench testing.
- The 4KB .dma_nocache window is now a clearly documented constraint; AUDIO_DMA_FRAMES=128 requires a deliberate 8KB MPU/linker redesign.

KNOWN ISSUES RESOLVED:
- Slider taper no longer performs repeated foreground powf() calls.
- TIM6 no longer performs heavy front-panel work in a high-priority ISR.
- LCD servicing no longer runs at 10kHz/high priority.
- Oscillator interpolation CPU cost is bounded by target filtering, the current two-slot budget, and block rendering.
- Sample/loop loading no longer exposes two menu options or reinitializes audio between the two stages.
- Sample/loop status screens no longer get stuck half-rendered between flash stages.
- Main encoder direction reversal should no longer require an extra click due to stale partial residue.

NEXT SESSION RECOMMENDED GOAL:
- Hardware regression pass for Session 023: CPU monitor comparison, audio underrun counter, Load: Samples flow, pattern load at AUDIO_DMA_FRAMES=96, encoder direction changes, LCD responsiveness, button/LED responsiveness, MIDI clock/DIN RX/TX, and sample interpolation behavior.
- After that, choose the next speedup target based on measured CPU monitor data. Likely candidates are filter coefficient/render hot spots, remaining mixer math, or more targeted oscillator profiling. Do not retry filter/distortion ITCM unless measured evidence supports it.

BLOCKERS:
- Hardware bench validation is needed before declaring the refactor fully proven.
- AUDIO_DMA_FRAMES=128 is blocked by the 4KB .dma_nocache MPU/linker region.

CRITICAL REMINDERS FOR NEXT SESSION:
- Working source is the local directory; future handoffs should use working repository status/path.
- Keep AUDIO_DMA_FRAMES=96 unless doing a deliberate DMA/MPU/linker experiment.
- OUTPUT_DMA_SIZE must stay 32; changing it changes canonical DSP/control cadence.
- DSP render stays in the main loop. Do not move it into DMA ISR.
- Do not implement the full-rate decimator bypass unless the owner reverses the Session 023 decision.
- Do not approximate nonlinear filter division/reciprocal math casually; DSP proposal 15 was explicitly rejected.
- OSC_WAVE_INTERP_MAX_ACTIVE is currently 2. User-sample interpolation remains enabled.
- Current ITCM state is oscillator-only: ENABLE_OSC_INITCM_CODE=1, ENABLE_EFFECT_INITCM_CODE=0.
- lcd_waitForIdle() is for rare modal paths that already suspend audio, not normal repaint paths.
- asyncfatfs polling must remain single-context.
- .dma_nocache is constrained: at AUDIO_DMA_FRAMES=128 the two audio DMA buffers alone fill 4KB before ADC DMA.
- Load: Samples now installs /samples then appends /loops; do not re-add a visible SampLoop menu entry unless explicitly requested.
```
