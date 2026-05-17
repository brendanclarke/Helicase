# LXR-02 Firmware Port — Project Context

This file is the working memory for Codex/Claude Code/LLM Agent sessions on this project.
Read it fully at the start of every session before touching any code.
Update it whenever something is confirmed, fixed, or decided.

---

## Quick Start

```
# Repository root is the working tree root (branch: LXR02Open-prime)

# Build
make && make img   →   build/LXRV2_lxr02.img

# Flash: copy LXRV2_lxr02.img to SD card root, hold main encoder, power on
```

**Current working source**: repository root, branch `LXR02Open-prime`.

**Session 023 note**: read `knowledge_files/log_archive/015_SESSION_HANDOFF_LOG.md` through
`knowledge_files/log_archive/023_SESSION_HANDOFF_LOG.md` before related work.
Session 019 adds TIM3 sequencer timing owner, interrupt-driven USART3, MidiRealtime timestamped ring, real CLK/RST jack backend, voice trigger pending ring, PAR_EXT_SYNC, CC1→MORPH, BAR1/BAR2 MIDI path, and corrects OUTPUT_DMA_SIZE to 32. Session 020 completes RV5-RV10 slider control as independent mixer-stage multipliers with per-block interpolation and configurable log taper.
Session 021 confirms OUT jack-detect mapping (OUT1L/OUT1R/OUT2L/OUT2R = PD6/PD7/PB4/PB6); after Session 023 PB4/PB6 are sampled by the TIM6-scheduled foreground service while PD6/PD7 remain EXTI9_5 edge-driven.
Session 022 introduces `sample_mx_t` (signed 24-bit in int32_t), widens mixer summing/output buffers/codec packer to carry true 24-bit audio, and documents the `dth` global menu option plan (not yet wired). Voice sync-blocks and distortion remain int16_t* (deferred).
Session 023 refactors CPU scheduling and DSP hot paths: TIM6 front-panel work is foreground-serviced at 500Hz, TIM7 LCD drain is 5kHz/priority 7, idle filesystem polling is rate-limited, slider log taper uses a 4096-entry LUT, oscillator interpolation is capped by `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current test build, oscillator-only ITCM is enabled, sample+loop loading is one menu command, and the main encoder direction-change residue bug is mitigated.

---

## Repository Layout

```
./
├── MEMORY.md                        ← this file
├── README.md                        ← confirmed hardware, known issues, critical reminders
├── main.c
├── config.h
├── Makefile
├── STM32F765VIHx_FLASH.ld
├── requirements.txt
├── tools/
│   └── build_lxrv2_img.py          ← packages ELF → LXRV2_lxr02.img
├── build/                           ← generated, not in VCS
├── knowledge_files/
│   ├── SESSION_HANDOFF_TEMPLATE.md ← template for writing new session handoff logs
│   ├── ENHANCED_FEATURES.md        ← future enhancement notes
│   ├── MEMORY_AUDIT.md             ← memory region audit notes
│   ├── DSP_AUDIT.md                ← DSP pipeline audit and hot-path notes
│   ├── OSC_INTERP_AUDIT.md         ← oscillator interpolation audit
│   ├── hardware_archive/
│   │   ├── HARDWARE_MAP.md         ← full confirmed pin table, IRQ numbers
│   │   ├── AVR_TO_F765_MIGRATION.md ← architectural notes, sequencer ISR design baseline
│   │   ├── FRONTPANEL_AUDIT.md     ← frontPanel_sendData() elimination audit
│   │   ├── SD_CARD_INVESTIGATION.md ← SD false-positive analysis (PA8, 74HC165)
│   │   └── XP_CONNECTOR_MAPS.md   ← ribbon cable pin mappings
│   └── log_archive/
│       ├── 000_SESSION_INDEX.md    ← index of all sessions with keyword lookup
│       ├── 001_SESSION_HANDOFF_LOG.md
│       ├── 002_SESSION_HANDOFF_LOG.md
│       ├── 003_SESSION_HANDOFF_LOG.md
│       ├── 004_SESSION_HANDOFF_LOG.md
│       ├── 005_SESSION_HANDOFF_LOG.md
│       ├── 006_SESSION_HANDOFF_LOG.md
│       ├── 007_SESSION_HANDOFF_LOG.md
│       ├── 008_SESSION_HANDOFF_LOG.md
│       ├── 009_SESSION_HANDOFF_LOG.md
│       ├── 010_SESSION_HANDOFF_LOG.md
│       ├── 011_SESSION_HANDOFF_LOG.md
│       ├── 012_SESSION_HANDOFF_LOG.md
│       ├── 013_SESSION_HANDOFF_LOG.md
│       ├── 014_SESSION_HANDOFF_LOG.md
│       ├── 015_SESSION_HANDOFF_LOG.md
│       ├── 016_SESSION_HANDOFF_LOG.md
│       ├── 017_SESSION_HANDOFF_LOG.md
│       ├── 018_SESSION_HANDOFF_LOG.md
│       ├── 019_SESSION_HANDOFF_LOG.md
│       ├── 020_SESSION_HANDOFF_LOG.md
│       ├── 021_SESSION_HANDOFF_LOG.md
│       ├── 022_SESSION_HANDOFF_LOG.md
│       └── 023_SESSION_HANDOFF_LOG.md
└── Core/
    ├── globals.h
    ├── datatypes.h
    ├── Src/
    │   └── startup_stm32f765xx.s
    ├── Hardware/
    │   ├── clocks.c/h               ← sysclk_init(), FPU enable via CPACR
    │   ├── timebase.c/h             ← SysTick 4kHz mainboard tick, TIM6 1kHz counters + 500Hz foreground service, TIM7 5kHz LCD drain
    │   ├── AudioCodecManager.c/h    ← consolidated audio: DMA ISRs, I2S/GPIO/DMA init, SPSC queue
    │   ├── triggerJacks.c/h         ← CLK OUT/IN, RST IN, OUT1 detect EXTI9_5
    │   ├── memtest.c/h              ← flash sector probe (boot-time, MEMTEST_ENABLED gate)
    │   ├── frontPanel/
    │   │   ├── buttonHandler.c/h    ← ISR-safe event ring, main-loop processEvents()
    │   │   ├── lcd.c/h              ← TIM7-driven async queue, 128-entry SPSC ring
    │   │   ├── ledHandler.c/h
    │   │   └── IO/
    │   │       ├── adcPots.c/h      ← sliders RV5-10, ADC1 DMA
    │   │       ├── din.c/h          ← 74HC165×5 buttons, SPI1
    │   │       ├── dout.c/h         ← 74HC595×5 LEDs, SPI1
    │   │       ├── encoder.c/h      ← SW42, TIM1 IC, Dannegger, accel + rebound suppression
    │   │       └── endlessPots.c/h  ← RV1-4, atan2 delta tracking
    │   ├── SD/
    │   │   ├── filesystem.c/h       ← public facade: typed async load/save/name/scan operations
    │   │   ├── kitBrowser.c/h       ← kit-only 128-slot gap-tolerant browser
    │   │   ├── SPI/
    │   │   │   ├── spi_sd.c/h       ← bit-bang SPI: PC12/PD2/PC8/PD0
    │   │   │   └── sd_routines.c/h  ← SD_init() only; blocking read/write superseded
    │   │   └── asyncfatfs/
    │   │       ├── asyncfatfs.c/h   ← Betaflight asyncfatfs (modified for LXR-02)
    │   │       ├── fat_standard.c/h
    │   │       ├── sdcard.h
    │   │       └── sdcard_lxr02.c/h ← SD block-device shim over bit-bang SPI
    │   └── USB/
    │       ├── OTG_Driver/
    │       ├── Device_Library/
    │       └── App/                 ← usb_manager, usb_midi_core, etc.
    ├── Menu/
    │   ├── menu.c/h                 ← full port, all pages, load/save UI
    │   ├── menuPages.h              ← 16-page × 8-subpage table
    │   ├── MenuText.h               ← all label strings
    │   ├── Cc2Text.c                ← modTargets[] 205 entries
    │   ├── CcNr2Text.h
    │   ├── copyClearTools.c/h       ← copy/clear tools; direct seq_* calls wired Session 15
    │   └── screensaver.c/h          ← screensaver with explicit LCD off/on phases
    ├── Preset/
    │   ├── ParameterArray.h/c       ← supersedes Parameters.h; NUM_PARAMS=273
    │   └── presetManager.c/h        ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── MIDI/
    │   ├── Uart.c/h                 ← USART3, 31250 baud, interrupt-driven dual FIFO (realtime + normal)
    │   ├── MidiRealtime.c/h         ← 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP
    │   ├── FIFO.c/h
    │   ├── MidiMessages.h           ← full mainboard version (MIDI_NRPN_* prefix)
    │   ├── MidiNoteNumbers.h
    │   ├── MidiParser.c/h
    │   ├── MidiVoiceControl.c/h
    │   ├── SeqStep.h
    │   ├── frontPanelParser.c/h     ← local dispatcher + sequencer/front-panel bridge
    │   └── valueShaper.h
    ├── SampleRom/
    │   ├── SampleMemory.c/h         ← sample flash metadata/runtime cache, 120 entries, loop flags
    │   └── sampleFlash.c/h          ← guarded F765 sector 6-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h       ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2) — Session 019
    │   ├── sequencer.c/h            ← original LXR sequencer source (driven by TIM3_IRQHandler)
    │   ├── EuklidGenerator.c/h      ← original LXR euclid generator source
    │   ├── SomData.c/h              ← original LXR SOM data tables
    │   ├── SomGenerator.c/h         ← original LXR SOM generator source
    │   ├── clockSync.c/h
    ├── DSPAudio/
    │   ├── random.c/h               ← F765 RNG port (PLL48CLK, bare register)
    │   └── [all DSP voice files]    ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h              ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```

### Where to look for things

| Question | File |
|----------|------|
| Which session introduced a fix? | `knowledge_files/log_archive/000_SESSION_INDEX.md` |
| Full details of a fix or decision? | `knowledge_files/log_archive/00x_SESSION_HANDOFF_LOG.md` |
| Confirmed pin assignments / IRQs? | `knowledge_files/hardware_archive/HARDWARE_MAP.md` |
| Sequencer / DSP architecture plans? | `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md` |
| Current known issues and reminders? | `README.md` |

---

## Project Goal

Port LXR 0.37 to the LXR-02 hardware (STM32F765VIH6). Original LXR: STM32F4 audio + ATmega644 AVR front panel. LXR-02: single STM32F765.

- This folder is the repository/codebase.
- `knowledge_files/LXR-master/` is read-only reference material only. Do not modify it.
- Only session logs under `knowledge_files/log_archive/` are expected to change inside `knowledge_files/`.
- **Original source reference**: `knowledge_files/LXR-master/` — AVR in `front/LxrAvr/`, STM32F4 in `mainboard/LxrStm32/src/`

---

## Hardware

### MCU: STM32F765VIH6, TFBGA100

- SYSCLK = 216MHz (HSE 16MHz, PLLM=16, PLLN=432, PLLP=2)
- PCLK1 = 54MHz (APB1/4) — TIM6, USART3, I2S2, I2S3
- PCLK2 = 108MHz (APB2/2) — SPI1, ADC
- PLL48CLK = 48MHz (PLLQ=9) — USB, RNG
- PLLI2S: N=271, R=2 → 135.5MHz → Fs=44108Hz
- Application origin: **0x08008000** — VTOR must be set at startup
- Stack top (SP): **0x20080000**
- DTCM 128KB (not DMA-accessible) + SRAM1 368KB + SRAM2 16KB

### Flash Sector Layout (single-bank, confirmed via memtest)

| Sector | Range | Size | Use |
|--------|-------|------|-----|
| 0 | 0x08000000–0x08007FFF | 32KB | LXRV2 Bootloader |
| 1–5 | 0x08008000–0x0807FFFF | — | Application |
| 6–11 | 0x08080000–0x081FFFFF | 6×256KB | Sample storage, implemented Session 18 |

**Erase floor: sector 6.** `sampleFlash.c` must hard-reject any erase below sector 6.

### Confirmed GPIO

| Peripheral | Pins | Notes |
|-----------|------|-------|
| LCD (4-bit parallel) | PE7-PE10 (DB4-7), PE11 (E), PE12 (RS) | TIM7 async driver |
| LEDs — 74HC595×5 | PA7 MOSI, PB3 SCK, PB2 LATCH | SPI1 |
| Buttons — 74HC165×5 | PA6 MISO, PB3 SCK, PB2 LATCH | SPI1, shared with LEDs |
| SW43 SHIFT/BAR1 | PB7 switch (active HIGH), PB8 LED | |
| Main encoder SW42 A/B | PE13/PE14 | AF1 TIM1_CH3/CH4, external 10kΩ pull-ups (R75/R76), NO internal pull-up |
| Main encoder SW42 SW | PE15 | Internal pull-up |
| Endless pots RV1-4 | PB0/1, PC4/5, PC2/3, PC0/1 | ADC1, atan2 delta tracking |
| Sliders RV5-10 | PA0-PA5 | ADC1 DMA (RV5↔RV6 label swap corrected in config.h) |
| DAC2 (CS4344, U24) | PB12 WS, PB13 CK, PB15 SD, PC6 MCK | I2S2, AF5 |
| DAC1 (CS4344, U23) | PA15 WS, PC10 CK, **PB5** SD, PC7 MCK | I2S3, AF6. **SD=PB5 not PC12** |
| SD card (bit-bang) | PC12 SCLK, PD2 MOSI, PC8 MISO, PD0 CS, PD1 DETECT | NOT SPI1. Hardware SPI remapping impossible on this board. |
| USB MIDI | PA11 D-, PA12 D+ | OTG_FS, ADUM3160 isolator, VBUS sensing disabled |
| MIDI DIN | PB10 TX, PB11 RX | USART3, 31250 baud |
| CLK OUT | PC13 | |
| CLK IN | PD4 | Active LOW via VT1, EXTI4 |
| RST IN | PD5 | Active LOW via VT2, EXTI5 |
| OUT1 L detect | PD6 | No plug=LOW, plug inserted=HIGH, EXTI9_5 both edges |
| OUT1 R detect | PD7 | No plug=LOW, plug inserted=HIGH, EXTI9_5 both edges |
| OUT2 L detect | PB4 | No plug=LOW, plug inserted=HIGH, sampled by TIM6-scheduled foreground service |
| OUT2 R detect | PB6 | No plug=LOW, plug inserted=HIGH, sampled by TIM6-scheduled foreground service |

### IRQ Assignments (current — Session 023)

| IRQ | Priority | Handler | Function |
|-----|----------|---------|----------|
| IRQ10 | 3 | EXTI4_IRQHandler | CLK IN — PD4 falling edge, push to trigger event ring |
| IRQ15 | 4 | DMA1_Stream4_IRQHandler | I2S2/DAC2 — audio refill master |
| IRQ23 | 3 | EXTI9_5_IRQHandler | RST IN (PD5) + OUT1 detect edges (PD6/PD7) |
| IRQ27 | 1 | TIM1_CC_IRQHandler | Main encoder A/B input capture |
| IRQ29 | 2 | TIM3_IRQHandler | 4kHz sequencer timing owner: processRealtimeEvents → triggerJacks_tick → seq_tick |
| IRQ39 | 5 | USART3_IRQHandler | MIDI DIN RX/TX; timestamps bytes with TIM2; realtime bytes → MidiRealtime ring |
| IRQ47 | 4 | DMA1_Stream7_IRQHandler | I2S3/DAC1 — audio slave (flags only) |
| IRQ54 | 6 | TIM6_DAC_IRQHandler | 1kHz — counters + foreground front-panel service flag |
| IRQ55 | 7 | TIM7_IRQHandler | 5kHz — LCD queue drain |
| IRQ67 | 5 | OTG_FS_IRQHandler | USB MIDI |

TIM6 now schedules `timebase_serviceFrontPanel()` from the foreground loop.
Shift-register exchange, PB4/PB6 jack detect, encoder-button debounce, and
endless-pot angle processing no longer run in the TIM6 ISR.

---

## Audio Pipeline (AudioCodecManager.c)

`audioCodec_init()` is the **single hardware entry point**.

**Main loop pattern:**
```c
if (audioCodec_queueFreeSlots() > 0) {
    // Fill one AUDIO_DMA_FRAMES hardware slot as three OUTPUT_DMA_SIZE blocks.
    for (frame = 0; frame < AUDIO_DMA_FRAMES; frame += OUTPUT_DMA_SIZE)
        mixer_calcNextSampleBlock(&buf[frame * 2], &buf2[frame * 2]);
    audioCodec_commitRenderBuffer();
}
```

**OUTPUT_DMA_SIZE = 32** is the effective LXR-master DSP/control block (confirmed correct in Session 019 — do NOT revert to 16).
**AUDIO_DMA_FRAMES = 96** is the hardware DMA half; render budget per queued hardware slot is **2.18ms** = 471,288 cycles at 216MHz.

**DSP render must remain in the main loop.** Moving it to the DMA ISR creates a hard 2.18ms ceiling — when DSP expands beyond the budget the system locks up with no graceful degradation. Main loop allows unlimited expansion with underruns as the graceful signal.

**SPSC queue**: 2-slot queue. `ready_head` is owned by ISR, `ready_tail` by main loop, and `ready_count` tracks free/full state for queueing and diagnostics.

**CPU-use widget**: `audioCodec_getQueueFreePercent()` uses DWT cycle-counter accounting of `ready_count < 2`. This is an audio queue-free pressure meter, not generic MCU utilization.

**24-bit output path (Session 022)**: `audioOutBuffer`/`audioOutBuffer2` are `sample_mx_t` (int32_t, signed-24 value in container). `pack_half()` emits a true 24-bit payload in both halfwords of each I2S frame. Do NOT re-add `LSW = 0` zeroing. Do NOT add a `>> 8` shift in `sampleMix_toS24()` — int16 voice values enter the mixer already scaled as `int16 << 8` via `bufferTool_convertInt16ToSampleMix()`; adding `>>8` at pack time was the loudness regression fixed in session 022.

**Zero startup underruns**: boot SD operations complete before `audioCodec_init()`.

### Internal DAC — Must Never Be Enabled

The STM32F765 has an internal 12-bit DAC on PA4 (DAC1_OUT) and PA5 (DAC2_OUT). These pins are also ADC1_IN4 and ADC1_IN5, used as slider inputs for RV6 and RV5. **The internal DAC must never be enabled.** Accidental DAC peripheral clock enable or pin mode change silently corrupts slider ADC readings.

`TIM6_DAC_IRQHandler` is the vector table name for IRQ54 because TIM6 and the internal DAC share an IRQ line by ST hardware design. **This does not mean the DAC is in use.** Audio is handled entirely by external CS4344 codecs via I2S2 and I2S3.

---

## SD Card Architecture (implemented Sessions 12 and 17)

**SD operations must never block the main loop or any ISR at priority ≤ 4.**

**Implemented solution**: asyncfatfs (Betaflight/Cleanflight library), fronted by `filesystem.c/h`. Ground-up FAT16/FAT32 reimplementation with polling-based non-blocking I/O. Replaces ChaN FatFS entirely. Decision made in Session 11, implemented in Session 12, and reorganized behind the filesystem facade in Session 17.

**Architecture:**
```
presetManager.c / kitBrowser.c
  → filesystem.c (typed operations: kit/morph/pattern/performance/all/globals)
    → asyncfatfs/asyncfatfs.c (afatfs_fopen/fread/fwrite/fclose/poll)
      → asyncfatfs/sdcard_lxr02.c (sector transfer FSM, 16 bytes/burst)
        → SPI/spi_sd.c (bit-bang SPI)
```

**Key design points:**
- `afatfs_poll()` is the single entry point for all SD background work. Called from main loop initially; can migrate to TIM5 ISR later if throughput is insufficient.
- `afatfs_poll()` must be called from **one context only** — never both main loop and ISR simultaneously.
- Session 023 rate-limits idle foreground polling to reduce CPU use. Active/busy operations and filesystem initialization still poll every pass.
- 8-sector LRU cache (4KB) between filesystem logic and SD card. Cache hits are free (no SPI traffic).
- `sdcard_lxr02.c` implements `sdcard_readBlock`/`sdcard_writeBlock`/`sdcard_poll` on top of `SPI/spi_sd.c`. Each `sdcard_poll()` call clocks a burst of 16 SPI bytes (~9µs). A 512-byte sector completes in 32 polls.
- `filesystem.c` serializes operations — one SD operation at a time. Request functions return immediately; completion is signalled via callback/status.
- `filesystem.c` owns the filetype registry and add-a-filetype checklist. Non-SD clients include `filesystem.h` only.
- Large pattern/performance/all files are streamed in bounded chunks and are not staged wholesale in RAM.
- `kitBrowser.c/h` intentionally remains kit-only; pattern/performance/all use typed name loading and direct slot handling.
- Boot path: synchronous polling loop before `audioCodec_init()` (audio not running, blocking OK).

**SD_init() from `SPI/sd_routines.c` still needed** at boot to bring card to SPI mode (CMD0/CMD1/CMD8/ACMD41). Called before `afatfs_init()`. The rest of sd_routines.c (SD_readSingleBlock, busy-wait loops) is superseded.

**Hardware SPI is impossible** — SD pins have no mapping to any free SPI peripheral on this board, and SPI5/6 are on unbonded TFBGA100 ports. See Session 10 log for full analysis.

**ISR migration path** (future): move `afatfs_poll()` from main loop to TIM5 ISR at priority 6, 10kHz. Everything at priority ≤ 5 preempts it. SD cards tolerate arbitrary SPI clock stretching from preemption. To migrate: init TIM5, move call site, remove main-loop call. No asyncfatfs code changes needed.

**Approaches confirmed wrong** (do not retry):
- Blocking SD calls in main loop — f_open blocks 1–50ms, kills audio
- "Chunking at FatFS-call granularity in main loop" — f_open is still one blocking call, insufficient
- Forking ChaN FatFS for async conversion — 20+ functions, 28 yield points, 6 levels of nesting, unacceptable risk
- NB_FatFS — C++ with heap alloc, lambdas, no FAT16, no _FS_TINY support, 9500 lines

---

## Sample Flash Loading (implemented Session 18)

User samples are installed with explicit modal operations from the Load page:

- `Load:[Samples ]` reads `/samples`, erases sectors 6-11, installs the accepted files, then reads `/loops`, preserves the normal samples, appends as many looped samples as fit, and skips the rest.
- The separate visible `SampLoop` menu item was removed in Session 023; the two existing loaders still run sequentially under the single Samples operation.
- Both loaders accept only mono PCM 16-bit 44.1kHz WAV files. Unsupported files are silently skipped.
- Directory entries are sorted lexicographic by the full long filename when LFN data is present, with ASCII case folded for sort. This is not natural numeric sort; e.g. `Loop 10.wav` sorts before `Loop 2.wav`.
- Installed sample menu labels are compact waveform names: `s01` through `s99`, then `sA0` upward.
- The full encoder-click parameter display shows the filename-derived 8-character display name. Stems longer than 8 characters are compressed as first 4 chars, CGRAM char `0x00`, last 3 chars, preserving case.
- `SampleInfo.size` is now `uint32_t`; the high bit is currently used as `SAMPLE_INFO_LOOP_FLAG`, leaving 31 bits for frame count.
- Metadata and display-name tables live at the top of sample flash; audio payload grows up from `0x08080000`.
- Flash writes use `sampleFlash.c/h`, which hard-rejects sectors below 6 and invalidates D-cache after erase/program.
- Audio is suspended before flash writes and fully reinitialized after. Hardware testing confirmed audio now comes back without reboot after sample load.

Sample flash map:

| Region | Range |
|--------|-------|
| App flash | `0x08008000-0x0807FFFF` |
| Sample data | `0x08080000-0x081FF69F` |
| `SampleInfo[120]` | `0x081FF6A0-0x081FFC3F` |
| Display names[120][8] | `0x081FFC40-0x081FFFFF` |

Important caveat: long samples are not fully solved. `SampleInfo.size` is 32-bit, but oscillator playback still uses the legacy `phase >> 17` index path. Treat long-sample support as unfinished until oscillator phase/index math is widened and hardware tested.

---

## Encoder (SW42)

- **Algorithm**: Dannegger difference — NOT LUT
- **Seed**: `last = new & 3` in `encode_init()` — NOT `(new+3)&3`
- **Divide**: round-toward-zero in `encode_read4()` — NOT `>>= 2`
- **Acceleration**: 8-entry `ts_dirs[]` timestamp buffer, 1×–4× linear multiplier, 100ms decay
- **Rebound suppression**: `ts_dirs[]` majority-direction check — do not remove
- `encode_read1/read2` permanently removed — do not re-add
- When moving `encode_read4()` to any interrupt context: TIM1 (priority 1) can
  preempt lower-priority service code and corrupt `enc_delta` mid-read. The
  current `cpsid/cpsie` guards are acceptable from foreground; an ISR caller
  should use a shadow-copy handoff instead.

---

## Display / Menu

- **LCD queue**: head/tail-only SPSC — do NOT reintroduce `lcd_q_count`
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte
- `buttonHandler_processEvents()`: `if` not `while` — intentional
- **Knob repaint**: `menu_knobs_dirty` + `menu_serviceKnobRepaint()` is RV1-4 only
- **endlessPots**: `atan2f(b, a)` — do NOT change argument order
- Saturation in `menu_encoderChangeParameter` / `menu_handleLoadSaveMenu`: int16 sum + clamp

---

## DSP / RNG Rules

- **Compiler**: `-O2` required.
- **FPU**: explicitly enabled in `sysclk_init()` via CPACR
- **VLAs**: forbidden in DSP voice files. Use `static int16_t buf[OUTPUT_DMA_SIZE]`. Snare/Cymbal fixed Session 8, HiHat fixed Session 12.
- **`GetRngValue()`**: returns `int16_t`. Explicit `(int16_t)` cast + `& 0x7FFF` mask at every call site.
- **LFO noise**: `lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f`
- **`RCC_AHB2ENR`**: address is `0x40023834` — NOT `0x40023830`
- **`RNG_CR`**: direct write `RNG_CR = RNG_CR_RNGEN` — NOT `|=`
- **DTCM**: not DMA-accessible

---

## Boot / Init Order (do not reorder)

```c
EXTI_IMR = 0;          // MUST be first
sysclk_init();
lcd_init();            // MUST be before lcd_tim7_init()
lcd_tim7_init();
encode_init(); din_init(); dout_init(); adc_init(); endlessPots_init();
triggerJacks_init();
dsp_init();
seq_init();            // seeds pattern/sub-step defaults; required before sequencer use
euklid_init();
som_init();
initMidiUart(); usb_init();
filesystem_initCardAndMountBlocking(); // card SPI mode + afatfs mount, pre-audio
menu_init();           // calls memset on parameter_values — do NOT also memset in main()
// Synchronous kit scan via filesystem_requestScanKits + polling
// Synchronous kit load via preset_loadDrumset + polling + menu_pollPresetStatus
// Synchronous globals load via preset_loadGlobals + polling + menu_pollPresetStatus
audioCodec_init();     // single audio entry point — AFTER all SD boot ops
sequencerTimer_init(); // TIM3 4kHz sequencer owner — AFTER audioCodec_init()
// main loop: filesystem_tick() + menu_pollPresetStatus() every iteration
// main loop: midi_service() for DIN/USB drain + flush
// main loop: seq_ledState_process() (NOT seq_tick — TIM3 owns that)
```

### Sequencer / PATGEN Reminders
- Reverse sequencer `SEQ_CC` feedback must use `seq_notifyFront()`, not `frontPanel_sendData()`, because original two-chip command values collide by direction.
- `seq_ledState_process()` drains sequencer LED events in the main loop. Do not move it to an ISR without auditing `seq_patternSet` and LED RMW races.
- `BUTTON_TIMEOUT` is milliseconds on this port (`500u`), not original AVR ticks (`38 * 13.107ms`).
- `EuklidGenerator.c` matches original LXR. PATGEN distribution depends on `__CLZ(0) == 32`; do not replace the shim with `__builtin_clz()`.
- `seq_tick()` is owned by `TIM3_IRQHandler`. **Do NOT add seq_tick() back to the main loop.**
- `midiParser_processRealtimeEvents()` and `triggerJacks_tick()` are also owned by TIM3. Do NOT call them from the main loop.
- `voiceControl_processPending()` is called only inside `audio_check_and_render()` before `mixer_calcNextSampleBlock()`. Do NOT call from ISR context.
- DIN TX FIFO inserts and USB MIDI writes from foreground code must use short critical-section wrappers to avoid corruption from TIM3 clock/note output.

### MIDI / Clock Reminders (Session 019)
- TIM2 is a shared free-running 1 µs counter. Do NOT reset it on each pulse. Use unsigned subtraction for deltas.
- TIM5 is free and reserved for future `afatfs_poll()` ISR migration. Do NOT use it for anything else.
- PAR_EXT_SYNC values in order: `off` / `usb` / `din` / `pls` / `aut`. PAR_BPM minimum is 1; value 0 no longer means external sync.
- CC1 on the global MIDI channel controls MORPH (`value << 1`, 0..254). This is handled in the incoming channel-MIDI path, not in `midiParser_ccHandler()`. Do not add CC1 to the CC handler.
- BAR1/BAR2 use `midiParser_playVoiceMidiNote(voice, vel)`. Do NOT revert to direct `voiceControl_noteOn/Off()`.
- Default voice MIDI notes: Drum1=36 … Drum7=42. Overridden by CC2_MIDI_NOTE per-voice setting.
- RST IN semantics: active-low run/reset gate. Low = stop+reset sequencer; release = start. Do not change this without documenting.
- MIDI realtime bytes (0xF8/FA/FB/FC) are routed to the MidiRealtime ring in the USART3 ISR and must NOT disturb the channel-message running-status parser state.

---

### Morph / Endless-Pot Reminders
- `preset_morph()` is an original-LXR-style front-panel CC dump. It is rate-limited by `preset_morphTick()`, but still uses `frontPanel_sendData()` and can record automation when sequencer record is armed.
- Do not add a morph skip cache. The request/pass generation scheduler must send a full final pass at the latest morph value.
- Morph skips index 127 and mod-target ranges. MorphKit load now writes `parameters2[]`; MorphKit save writes interpolated values except mod-target ranges.
- RV1-RV4 are analog endless pots, not the digital Gray-code encoder. The driver uses raw A/B snapshot baselines, `ENDLESS_POT_DEADZONE = 20`, `ENDLESS_POT_TIMEOUT_MS = 5000`, and `ENDLESS_POT_DELTA_TIMEOUT_MS = 20`.
- Only `PAR_MORPH` gets endless-pot double angular speed. Do not apply this to all `DTYPE_0B255`; BPM drift exposed that as too broad.

---

## Known Issues (as of Session 023)

### Resolved / Changed in Session 023
- CPU scheduling refactor completed: TIM6 keeps only 1ms counters and a foreground service due flag; shift-register exchange, PB jack detect, encoder-button debounce, and endless-pot scanning run from `timebase_serviceFrontPanel()` at about 500Hz.
- LCD servicing reduced to 5kHz at priority 7. `lcd_waitForIdle()` is used only in modal sample/loop load screens after audio has been suspended, so status text fully renders before flash work blocks.
- Slider taper mapping now uses a 4096-entry boot LUT derived from `SLIDER_LOG_TAPER_DB`, removing repeated foreground `powf()` calls.
- Oscillator interpolation is bounded by `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current test build and limited to audible oscillator waveform targets; user-sample interpolation remains enabled.
- Oscillator-only ITCM is enabled. Filter/distortion ITCM annotations remain present but disabled through `ENABLE_EFFECT_INITCM_CODE=0` after hardware CPU monitor testing looked worse.
- `Load:[Samples ]` now runs `/samples` installation then `/loops` append sequentially; the visible `SampLoop` menu entry was removed.
- Main encoder direction reversals clear only sub-detent residue, avoiding the persistent two-click feel after fast direction changes.

### Resolved in Session 022
- ~~24-bit DMA frame LSW forced to 0x0000~~ — **RESOLVED**. `pack_half()` now emits true signed-24 payload in both halfwords. `sample_mx_t` (int32_t) carries signed-24 audio; int16 voice values enter mixer as `int16<<8` via `bufferTool_convertInt16ToSampleMix()`.

### Pending from Session 022 (not resolved)
- **dth global menu option not yet wired** — complete plan in DITHER_AUDIT.md Steps 1–8. Prerequisite infrastructure (`sample_mx_t` path) is now in place. Next step: PAR_16BIT_DITHER enum + menu text + menuPages.h slot + dtype/default + mixer gate.
- **Voice sync-block and distortion widening deferred** — DrumVoice/Snare/CymbalVoice/HiHat/distortion remain int16_t* for now. Pinned plan in DITHER_AUDIT.md. Revisit when voice/distortion gain staging can be validated after widening.
- **Hardware listening test for 24-bit path needed** — loudness regression was fixed analytically (removed extra >>8); hardware confirmation still required.

### Resolved in Session 16
- ~~preset_morph() stub / burst risk~~ — **RESOLVED**. Morph now uses original interpolation and a one-parameter-per-main-loop worker.
- ~~preset_sendModTarget() fall-through bug~~ — **RESOLVED**. `CC_VELO_TARGET` now breaks before `CC_LFO_TARGET`.
- ~~Morph return-to-zero could leave late DSP parameters stale~~ — **RESOLVED**. No skip cache; request/pass generations guarantee a full final pass at latest morph value.
- ~~RV1-RV4 page-change and idle ghost edits~~ — **RESOLVED** by raw A/B snapshots, page-change `snapshotAll()`, post-delta rebaseline, and pre-delta false-start cancellation.
- ~~Global BPM drift from broad endless-pot double-speed rule~~ — **RESOLVED**. Double angular speed now applies only to `PAR_MORPH`.

### Resolved in Session 17
- ~~Preset save/load pattern/all/performance stubs~~ — **RESOLVED**. Kit, MorphKit, Pattern, Performance, All, and Globals now route through typed async filesystem operations.
- ~~Morph-kit load/save not connected~~ — **RESOLVED**. Morph load writes `parameters2[]`; morph save writes interpolated values with mod-target exceptions.
- ~~Slow envelopes from widened DSP block~~ — **RESOLVED**. `OUTPUT_DMA_SIZE` matches the effective LXR-master block size of 32 frames; `AUDIO_DMA_FRAMES` remains 96 for hardware DMA.
- ~~Sequencer/mainboard tick drift from reference~~ — **RESOLVED**. `systick_ticks` is 4kHz again; TIM6 `time_sysTick` remains 1kHz for UI/service timing.
- ~~Screensaver display glitches~~ — **RESOLVED** with explicit LCD off/on phases and clear-on-exit.
- ~~Load-page empty slots and fast-spin name/display races~~ — **RESOLVED** by typed async request tagging and whole-frame LCD queue preflight.

### Resolved in Session 019
- ~~USART3 RX not configured~~ — **RESOLVED**. Interrupt-driven RX/TX, dual TX FIFO, non-blocking.
- ~~DIN TX polled/blocking~~ — **RESOLVED**. TXE interrupt drains realtime FIFO before normal FIFO.
- ~~USB MIDI RX/TX not serviced~~ — **RESOLVED**. midi_service() in main loop.
- ~~BAR1/BAR2 DSP race~~ — **RESOLVED**. Voice trigger pending ring + audio-boundary drain.
- ~~BAR1/BAR2 not through MIDI recording path~~ — **RESOLVED**. midiParser_playVoiceMidiNote() with assigned/default note numbers.
- ~~PD3 EXTI3 diagnostic in production~~ — **RESOLVED**. Real PC13/PD4/PD5 backend.
- ~~EXTI4/EXTI9_5 pointing at Default_Handler~~ — **RESOLVED**. IRQ10/IRQ23 wired.
- ~~seq_tick() in main loop~~ — **RESOLVED**. TIM3 4kHz ISR owns it.
- ~~PAR_BPM=0 external sync toggle~~ — **RESOLVED**. PAR_EXT_SYNC global (SyncInpt).
- ~~CC1 MORPH double-fire~~ — **RESOLVED**. CC1→MORPH in incoming channel path only.
- ~~OUTPUT_DMA_SIZE=16 (2× EG/LFO rate)~~ — **RESOLVED**. Corrected to 32.

### Resolved in Session 020
- ~~Slider-to-parameter mapping not designed~~ — **RESOLVED**. RV5-RV10 now feed dedicated `slider_vol[]` gains and are applied as independent post-voice multipliers in `mixer.c` (not as base `voice.vol` writes).
- ~~Slider zipper from block-edge gain jumps~~ — **MITIGATED**. Per-block interpolation (`last_gain -> current_gain`) added in mixer; log taper mapping added in adc path (`SLIDER_LOG_TAPER_DB`).

### Resolved in Session 18
- ~~SampleMemory.c no-op stub~~ — **RESOLVED**. SampleMemory now validates/caches flash metadata and display names for up to 120 installed samples.
- ~~Sample flash region only proposed~~ — **RESOLVED**. Linker caps app flash at `0x0807FFFF`; sectors 6-11 are reserved for sample storage.
- ~~Load:[Samples] no-op~~ — **RESOLVED**. `/samples` full reinstall and `/loops` append-loop installer are wired through the Load page.
- ~~Audio resume after modal sample writes failed~~ — **RESOLVED**. `audioCodec_suspend()`/`audioCodec_resume()` now fully stop/reset/restart DMA, I2S, and PLLI2S.

### Resolved in Session 15
- ~~Sequencer step buttons unreliable / no sequenced voices~~ — **RESOLVED**. `BUTTON_TIMEOUT` corrected to 500ms and missing `seq_init()` added at boot.
- ~~Sequencer tempo 4x slow~~ — **RESOLVED** for gross BPM in Session 15; Session 17 restored `systick_ticks` to the original 4kHz LXR mainboard tick while UI millisecond timing stays on `time_sysTick`.
- ~~PATGEN/Euklid writes steps but LEDs do not update~~ — **RESOLVED**. Visible generated main-step LEDs refresh after steps/rotation changes.
- ~~PATGEN/Euklid generated steps front-stacked~~ — **RESOLVED**. `__CLZ` shim now emits ARM `clz`; `__CLZ(0)` returns 32 as original Euklid expects.
- ~~copyClearTools.c frontPanel calls commented out~~ — **RESOLVED**. Direct `seq_clear*` / `seq_copy*` calls wired.
- Euclid and SOM parser backends are wired. Trigger backend remains stubbed.

### Resolved in Session 14
- ~~buttonHandler/menu/LED audit connectivity gaps~~ — **RESOLVED** for audit-defined paths. Connections documented in `BUTTONHANDLER_MENU_AUDIT_RESULTS.md` and `LED_AUDIT_SUMMARY.md`.

### Resolved in Session 13
- ~~PAR_VOICE_LFO1-6 not reaching modulation targets during kit load~~ — **RESOLVED**. `preset_sendModTarget()` calls `modNode_setDestination()` directly, bypassing frontPanel_sendData roundabout.

### Resolved in Session 12
- ~~SD card operations block the main loop~~ — **RESOLVED** by asyncfatfs.
- ~~HiHat VLA stack corruption~~ — **RESOLVED**. Static arrays.
- ~~preset_morph() index 127 memory corruption~~ — **RESOLVED**. Index 127 skipped.

### High Priority
1. **No hi-hat at startup** — boot kit hi-hat is silent; loading any other kit activates it. DSP init ordering issue. VLA bug that masked this is now fixed.
2. ~~Trigger backend still stubbed~~ — **RESOLVED in Session 019 Phase 5**. PC13 CLK OUT, PD4 CLK IN, PD5 RST IN, and trigger PPQ menu wiring are implemented; hardware bench validation still needed.
3. ~~BAR1/BAR2 race condition~~ — **RESOLVED in Session 019 Phase 7**. All voice triggers deferred through voiceControl pending ring; drained at audio boundary.
4. **Long sample playback is not 32-bit clean yet** — `SampleInfo.size` is `uint32_t`, but `Oscillator.c` still derives the address index with the legacy `phase >> 17` path.
5. **MIDI/clock/jack bring-up only** — All Session 019 features are build-verified only. Hardware bench testing required before claiming correctness.

### Medium Priority
5. **ResonantFilter.c double literals** — lines 141, 167: `0.5*in` and `1.0 - f_lp2` cause software double emulation in SVF_calcBlockZDF hot loop. Change to `0.5f` and `1.0f`.
6. **DrumVoice.c VLA** — line 228: `int16_t modBuf[size]` still present, should be static.
7. **BufferTools.c float division** — line 120: `i/(size-1.f)` per sample in hot loop.
8. ~~TIM2 not initialised~~ — **RESOLVED in Session 019**. TIM2 is the shared 1 MHz free-running timestamp source. Do NOT reset on pulse. Do NOT use for SD ISR (TIM5 reserved for that).
9. ~~MidiParser RX not connected~~ — **RESOLVED in Session 019**. Full MIDI in/out including clock, sync, CC1→MORPH, and BAR1/BAR2 MIDI path implemented. Hardware validation pending.
14. Final RV1-RV4 endless-pot noise fix needs long idle hardware soak, especially on global BPM page.
15. Synced LFO tempo still needs audit/fix: current code path has used a hardcoded 130 BPM instead of `seq_getBpm()`.

### Lower Priority
16. Sample loader naming/display/order needs hardware soak after Session 18 LFN changes. Sort is whole-filename lexicographic with ASCII case folded; display names preserve case.
17. SELECT_1 LED dark at boot — enhanced-firmware-only fix
18. ~~Sequencer needs dedicated TIM ISR~~ — **RESOLVED in Session 019**. TIM3 owns sequencer timing.

---

## Failed Approaches — Do Not Retry

- **TIM7 idle gating**: wakeup race → freeze. Session 6.
- **Broad repaint coalescing**: freezes and lag. Session 6.
- **`val >>= 2` in `encode_read4()`**: asymmetric floor divide. Session 7.
- **`last = (new+3)&3` encoder seed**: wrong direction click. Session 4/5.
- **`RNG_CR |= RNG_CR_RNGEN`**: RMW on unpowered peripheral → stall. Session 8.
- **`ready_count` shared RMW in audio queue**: non-atomic. Session 8.
- **VLAs in DSP voice files**: silent stack overflow. Session 8.
- **DSP render in DMA ISR**: hard 2.18ms ceiling, no graceful degradation, system locks on overrun. Session 10.
- **Blocking SD calls in main loop**: f_open blocks 1–50ms, kills audio render. Session 10.
- **SD chunking at FatFS-call granularity in main loop**: f_open is still one blocking call, insufficient. Session 10.
- **Forking ChaN FatFS for async conversion**: 20+ functions need state machine conversion, 28 yield points, 6 levels of call nesting — unacceptable risk and complexity. Session 11.
- **NB_FatFS library**: C++ with heap allocation (new/delete), lambdas, global voidPtr for context, no _FS_TINY support, no FAT16, 9500 lines, designed for hardware DMA callbacks. Session 11.
- **preset_morph() sending index 127**: `frontPanel_sendData(MIDI_CC, 127, val)` → `(127+1) & 0x7f = 0` → `paramNr = 0 - 1 = 65535` → wild write to `midiParser_originalCcValues[65536]`. Memory corruption. Must skip index 127. Session 12.
- **VLAs in HiHat_calcSyncBlock**: `int16_t mod1[size], mod2[size]` — silent stack corruption with -O2. Must use static arrays. Session 12.
- **SD_sendCommand() from sdcard_lxr02.c**: Deasserts CS after R1 response — breaks data transfer. Must use send_cmd_keep_cs() instead. Session 12.
- **Two back-to-back filesystem requests**: storage FSM is single-operation. Must wait for completion or ack. Session 12/17.
- **frontPanel_sendData for CC_VELO_TARGET/CC_LFO_TARGET during kit load**: Routing through frontPanelParser → midiParser_ccHandler doesn't properly establish modulation targets in the merged single-MCU context. Use `preset_sendModTarget()` instead. Session 13.
- **Morph skip cache**: can skip necessary DSP restores after returning to morph 0. Morph must send a complete final pass at the latest value. Session 16.
- **Endless-pot double speed for all `DTYPE_0B255`**: made global BPM drift more visible. Only `PAR_MORPH` gets double angular speed. Session 16.

---

## Toolchain

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2
```

Image format: `[8B "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
Boot: hold main encoder button while powering on.
