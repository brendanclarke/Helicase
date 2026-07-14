# MEMORY_AUDIT.md

Session 032 note: this audit was moved into `specification_reference/` as a
historical memory snapshot. The section sizes and largest-symbol list are from
Session 023 and predate the SceneData/instrument-descriptor refactor. Treat old
names such as `seq_patternSet`, `seq_tmpPattern`, and `parameterArray` as
historical anchors, not current ownership names. Re-run the size/nm commands
before making new SRAM/flash decisions; for current filesystem, instrument
storage, and menu ownership, read `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.

Session 036 note: asyncfatfs gained VFAT LFN creation/open helpers,
case-preserving short-name behavior, exact-case LFN matching, and object
iteration after this memory snapshot. The `afatfs` size listed below is
historical and must be remeasured before any SRAM decision involving the
filesystem layer.

Session 037 note: Morph Save menu promotion changed Menu reachability but not
the historical memory numbers below. Load/Save type cycling now treats KitMrp
as the promoted row immediately after Kit, and nested Instrument Save exposes
its top `Save:[Type]` row as a visible cursor target so normal Instrument Save
can be switched to `TypeMrp` before editing the filename. Re-run the memory
audit before using these UI changes to infer code-size or SRAM impact.

Session 023 memory audit after refactor implementation. Current snapshot has
oscillator ITCM placement enabled and filter/distortion ITCM placement disabled
for CPU monitor A/B testing.
Updated: 2026-05-17.

Build and audit commands used:

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
- `build/LXRV2_lxr02.img` was generated successfully.
- Existing warnings remain: nano syscall stubs (`_close`, `_lseek`, `_read`,
  `_write`), LTO serial compilation notice, and clean-build legacy warnings in
  asyncfatfs/USB code. No link failure or memory overflow occurred.

## Section Usage

From `arm-none-eabi-size -A build/lxr02.elf`:

| Section | Size | Address | Notes |
| --- | ---: | ---: | --- |
| `.isr_vector` | 456 B | `0x08008000` | Vector table in application flash. |
| `.text` | 226,824 B | `0x080081c8` | Flash-resident code and rodata. |
| `.itcm` | 3,808 B | `0x00000000` | Oscillator INITCM code copied from flash at startup. |
| `.dma_nocache` | 3,100 B | `0x20020000` | DMA-visible SRAM1, MPU non-cacheable, 4 KB cap. |
| `.data` | 344 B | `0x20020c1c` | Initialized SRAM1 data. |
| `.bss` | 106,364 B | `0x20020d88` | Zero-init SRAM1 data. |
| `.dtcm` | 35,168 B | `0x20000000` | Initialized DTCM data. |
| `.dtcmz` | 6,092 B | `0x20008960` | Zero-init DTCM data. |

## Region Summary

| Region | Capacity | Used | Free | Used % |
| --- | ---: | ---: | ---: | ---: |
| ITCM | 16,384 B | 3,808 B | 12,576 B | 23.2% |
| DTCM | 131,072 B | 41,260 B | 89,812 B | 31.5% |
| `.dma_nocache` MPU window | 4,096 B | 3,100 B | 996 B | 75.7% |
| SRAM1 static sections | 376,832 B | 109,808 B | 267,024 B | 29.1% |

Stack note:

- `_estack` remains `0x20080000`.
- Static SRAM1 allocation currently ends at `_ebss = 0x2003acf4`.
- Distance from `_ebss` to the stack top is about 283,404 B. This is collision
  headroom, not a formal reserved stack allocation.

Flash note:

- Application flash region is 480 KB (`0x08008000-0x0807ffff`).
- Current loaded image ends at `_eflash_load = 0x08049168`, for 266,600 B used
  from the app flash region, about 54.2%.
- `build/LXRV2_lxr02.img` payload size is 266,600 B.

## ITCM Details

The `.itcm` section is present with oscillator-only placement:

- VMA: `0x00000000`
- Size: `0x0ee0` / 3,808 B
- Flash load address: `0x0803f7d0`
- Linker stubs: 24 B inside `.itcm`

Confirmed ITCM symbols from `objdump -t` include:

| Symbol | Size | Notes |
| --- | ---: | --- |
| `calcFmBlock.constprop.*` | 184-192 B | FM oscillator table block paths. |
| `calcSampleOscFmBlock.constprop.0` | 196 B | Sample FM oscillator block path. |
| `osc_setFreq` | 236 B | Oscillator frequency setup/cache path. |
| `calcUserSampleOscFmBlock.constprop.0` | 444 B | User sample FM block path. |
| `calcNextOscSampleFmBlock.constprop.0` | 440 B | FM oscillator block path. |
| `calcUserSampleOscBlock.constprop.0` | 396 B | User sample block path. |
| `calcNextOscSampleBlock.constprop.*` | 824-872 B | Oscillator block paths folded by LTO. |

Implementation paths:

- `config.h`: `ENABLE_OSC_INITCM_CODE=1`, `ENABLE_EFFECT_INITCM_CODE=0`.
- `STM32F765VIHx_FLASH.ld`: `ITCM` region and `.itcm` section.
- `Core/Src/startup_stm32f765xx.s`: startup copy from `_siitcm` to
  `_sitcm.._eitcm`, with explicit ITCM/DTCM interface enable before copying.
- `Core/DSPAudio/Oscillator.c`: selected oscillator render/frequency functions
  marked `INITCM`.
- `Core/DSPAudio/ResonantFilter.c` and `Core/DSPAudio/distortion.c`: effect
  placement annotations compile to ordinary functions while
  `ENABLE_EFFECT_INITCM_CODE=0`.

Assessment:

- This build isolates oscillator ITCM from the previously worse
  filter/distortion ITCM test.
- Re-enable effect placement with `#define ENABLE_EFFECT_INITCM_CODE 1` only
  if a future measured target benefits from it.

## DTCM Details

DTCM total used: 41,260 B.

Notable DTCM residents:

- `transientData`: 26,460 B.
- `sine_table`: 8,194 B.
- `audioOutBuffer` and `audioOutBuffer2`: 1,536 B each.
- `voiceArray`: 1,536 B.
- `snareVoice`, `cymbalVoice`, `hatVoice`: about 992 B combined.
- `osc_interp_a` and `osc_interp_b`: 64 B each.
- Mixer decimation/routing/slider state and modulation state.

Assessment:

- DTCM has ample headroom.
- CPU-only audio render buffers are safe here.
- Do not move DMA-visible buffers into DTCM.

## SRAM1 Details

Static SRAM1 section total: 109,792 B.

Largest SRAM1 residents from `nm --size-sort`:

| Symbol | Size | Notes |
| --- | ---: | --- |
| `seq_patternSet` | 50,528 B | Main sequencer pattern store. |
| `slider_lut` | 16,384 B | 4096-entry slider taper LUT. |
| `sample_manifest` | 13,440 B | Sample install manifest/state. |
| `seq_tmpPattern` | 6,316 B | Active pattern temp/swap state. |
| `afatfs` | 4,556 B | asyncfatfs state/cache. |
| `USB_OTG_dev` | 1,524 B | USB core state. |
| `usb_MidiMessages` | 2,048 B | USB MIDI buffering. |
| `parameterArray` | 1,824 B | Parameter metadata. |
| `sample_info_cache` | 1,440 B | Runtime sample metadata cache. |
| `install_info` | 1,440 B | Sample install metadata staging. |

Assessment:

- SRAM1 still has substantial static headroom.
- The full slider LUT remains an intentional and affordable SRAM1 trade-off.
- The tightest SRAM-adjacent resource is the 4 KB `.dma_nocache` MPU window.

## DMA Non-Cacheable Window

`.dma_nocache` usage:

- Capacity: 4,096 B.
- Used: 3,100 B.
- Free: 996 B.

Current residents:

- `dma_buffer2`: 1,536 B.
- `dma_buffer`: 1,536 B.
- ADC DMA buffer and alignment/fill account for the remaining bytes.

Assessment:

- This region is the closest to its cap.
- Any future DMA-visible buffer addition should either shrink/relocate existing
  residents or add another MPU non-cacheable region.

## Watch Items

1. The 4096-entry slider LUT is a deliberate 16 KB SRAM1 trade-off. It removes
   repeated foreground `powf()` calls while preserving the config-driven taper.
2. ITCM is oscillator-only in the current test build. LTO cloning/inlining
   effects should be measured before placing filters or distortion there again.
3. `.dma_nocache` has less than 1 KB free. Treat it as constrained.
4. The linker still leaves stack reservation implicit. Consider adding a map
   assertion for minimum stack headroom if future SRAM growth becomes
   aggressive.
