# SRAM manifest

Generated from the current `build/lxr02.elf` on 2026-07-25 after the Session
043 bitmap Pattern-storage minimization, 1,024-entry slider-LUT reduction,
tagged instrument-runtime migration, and 26,460-B `transientData` move from
DTCM to internal FLASH. This is a fresh linked-image inventory: sizes come
from `arm-none-eabi-size -A` and `arm-none-eabi-nm -S --size-sort`, not source
estimates.

## Allocation policy

No listed free capacity is general headroom. DTCM free capacity, including the
26,460 B released by the transient-ROM move, is reserved exclusively for
future delay-line buffers. Normal SRAM1 free capacity is reserved exclusively
for future Pattern data. Any additional or enlarged RAM allocation requires an
explicit byte count, region, lifetime, owner, and user acknowledgement before
implementation.

## Static allocated-RAM summary

| Region | Linker origin | Capacity | Static use | Capacity remaining / reservation |
| --- | ---: | ---: | ---: | --- |
| DTCM (`.dtcm` + `.dtcmz`) | `0x20000000` | 131,072 B | 12,280 B | 118,792 B — future delay-line buffers only |
| SRAM1 DMA/no-cache | `0x20020000` | included below | 3,100 B | included in SRAM1 total |
| SRAM1 normal (`.data` + `.bss`) | `0x20020c1c` | included below | 63,680 B | included in SRAM1 total |
| **SRAM1 total** | `0x20020000` | **376,832 B** | **66,780 B** | **310,052 B — future Pattern data only** |
| **All static allocated RAM** | — | — | **79,060 B** | — |

The image contains 404 B of initialized SRAM1 data and 69,948 B of
zero-initialized data: 3,100 B in `.dma_nocache`, 63,276 B in normal SRAM1
`.bss`, and 3,572 B in DTCM `.dtcmz`. The initialized DTCM `.dtcm` section is
read-only table storage at runtime but still consumes 8,708 B of DTCM capacity.

## Linker sections

| Section | Address | Size | Region | Contents |
| --- | ---: | ---: | ---| --- |
| `.text` | `0x080081c8` | 338,808 B | FLASH | Firmware code and ordinary read-only data, including `transientData` |
| `.itcm` | `0x00000000` | 3,768 B | ITCM | Hot code copied from FLASH at reset |
| `.dtcm` | `0x20000000` | 8,708 B | DTCM | Fast immutable DSP lookup tables |
| `.dtcmz` | `0x20002204` | 3,572 B | DTCM | Zero-initialized DSP/audio working buffers |
| `.dma_nocache` | `0x20020000` | 3,100 B | SRAM1 | DMA audio/ADC buffers |
| `.data` | `0x20020c1c` | 404 B | SRAM1 | Initialized writable globals |
| `.bss` | `0x20020db0` | 63,276 B | SRAM1 | Normal zero-initialized globals |

The final FLASH load image ends at `0x0805df90`, safely before the reserved
sample-FLASH boundary `0x08080000`. `build/lxr02.bin` is 352,144 B.

## Primary SRAM1 owners

| Symbol | Size | Purpose |
| --- | ---: | --- |
| `scenes` | 20,992 B | 16 resident `scene_t` values; each holds one 112-B bitmap `PatternSet` |
| `fs_list_cache_name` | 9,000 B | Shared library-name/HCNAMES cache |
| `afatfs` | 7,344 B | Async FAT filesystem state |
| `runtime_slots` | 7,056 B | Six tagged engine slots, 1,176 B reserve each |
| `sample_info_cache` | 1,440 B | Sample-information cache |
| `sample_name_cache` | 1,080 B | Sample-name cache |
| `USB_OTG_dev` | 1,524 B | USB device state |
| `fs_stage_workspace` | 2,048 B | Aligned Kit/Instrument/Scene staging workspace |
| `usb_MidiMessages` | 2,048 B | USB MIDI message storage |
| `slider_lut` | 4,096 B | 1,024 native `float` attenuator nodes; lookup is `raw >> 2`, without interpolation |
| `parameter_values` | 384 B | Legacy MIDI parameter cells |
| `midiParser_originalCcValues` | 255 B | Legacy MIDI CC baseline cells |
| `velocityModulators` | 264 B | One velocity modulation node per instrument slot |
| `lfo_descriptor_targets` | 192 B | Per-slot LFO descriptor target adapters |
| `lcd_queue` | 384 B | LCD command queue |
| `staging_buf` | 512 B | Audio/sample staging scratch |

The remaining normal SRAM1 state is intentionally distributed across
filesystem-operation records, menu/UI state, MIDI rings, sequencer state,
modulation metadata and small driver records. Every individual remaining
writable symbol is 255 B or smaller; the section totals above include all of
them.

## DTCM, DMA, and FLASH-ROM owners

| Symbol/group | Size | Region | Purpose |
| --- | ---: | --- | --- |
| `transientData` | 26,460 B | FLASH `.text` at `0x08053264` | Immutable transient PCM ROM; no DTCM/SRAM shadow |
| `sine_table` | 8,194 B | DTCM `.dtcm` | Sine lookup table |
| `squareRootLut` | 512 B | DTCM `.dtcm` | Mixer pan-gain lookup table |
| `audioOutBuffer` + `audioOutBuffer2` | 3,072 B | DTCM `.dtcmz` | DSP output working buffers |
| `dma_buffer` + `dma_buffer2` | 3,072 B | SRAM1 `.dma_nocache` | DMA audio buffers |
| `adc_dma_buf` | 28 B | SRAM1 `.dma_nocache` | Shared ADC conversion buffer |

## Storage-reduction checkpoints

| Change | Current linked result |
| --- | --- |
| Pattern representation | `scenes` is 20,992 B; pattern payload is 16 x 112 B = 1,792 B. No `Step[7][128]` symbol is linked. |
| Slider LUT | `slider_lut` is 4,096 B: 1,024 `float` values, four ADC codes per non-interpolated node. |
| Instrument runtime ownership | Exactly one `runtime_slots` symbol is linked at 7,056 B. No native drum/snare/cymbal/hat object or per-engine expansion pool is linked. |
| Transient PCM ROM | `transientData` is 26,460 B at `0x08053264` in FLASH. DTCM `.dtcm` is 8,708 B, down exactly 26,460 B from the preceding image. |

## Verification commands

```sh
make -j2
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
```

For the current image, conventional `arm-none-eabi-size` reports `text=351,740
B`, `data=404 B`, and `bss=69,948 B`. The latter is the combined zero-init
total across memory regions; `size -A` provides the section split above.

## 2026-07-27 Bank Load / command-UI implementation note

`menu_loadSaveCommandActive` is one normal-SRAM1 `.bss` byte owned by Menu for
the lifetime of an accepted OK/OW command. It controls only `...` rendering,
cursor suppression, and the one terminal type-row reset; it retains neither a
payload nor a browser name. The compiler packed this byte into existing layout
padding in the linked image used for this implementation check.

Bank Load reuses its existing operation scratch and does not add a Bank-child
cache, Scene stage, Instrument image, or LFO state. The runtime recursive
Bank-tree quarantine was removed from the active build; selected children are
validated by the existing shared Scene parser before atomic commit. The linked
implementation check reported `text=351,788 B`, `data=400 B`, and `bss=69,948
B`. These linked totals, rather than source-field estimates, are authoritative.
