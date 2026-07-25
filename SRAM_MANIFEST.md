# SRAM manifest

Generated from current `build/lxr02.elf` on 2026-07-25 after Pattern storage
minimization, slider-LUT reduction, tagged instrument slots, and moving
`transientData` to FLASH. Values are from `arm-none-eabi-size -A`,
`arm-none-eabi-nm -S --size-sort`, and the linked map—not source estimates.

The durable reference mirror is
`knowledge_files/specification_reference/SRAM_MANIFEST.md`; update both after
an approved memory-changing build.

## Reservation policy

Free DTCM, including the 26,460 B released by the transient-ROM move, is for
future delay-line buffers only. Free normal SRAM1 is for future Pattern data
only. Neither is general feature headroom. Before adding or enlarging a RAM
allocation, disclose its exact bytes, region, lifetime, and owner and obtain
user acknowledgement.

## Static allocated RAM

| Region | Capacity | Static use | Remaining / reservation |
| --- | ---: | ---: | --- |
| DTCM (`.dtcm` + `.dtcmz`) | 131,072 B | 12,280 B | 118,792 B — delay lines only |
| SRAM1 DMA/no-cache | included | 3,100 B | included in SRAM1 |
| SRAM1 normal (`.data` + `.bss`) | included | 63,680 B | included in SRAM1 |
| **SRAM1 total** | **376,832 B** | **66,780 B** | **310,052 B — Pattern only** |
| **All static allocated RAM** | — | **79,060 B** | — |

The image contains 404 B initialized SRAM1 data and 69,948 B zero-initialized
data: 3,100 B DMA/no-cache, 63,276 B normal SRAM1 `.bss`, and 3,572 B DTCM
`.dtcmz`. Initialized `.dtcm` is read-only table storage but consumes DTCM.

## Linked sections

| Section | Address | Size | Region | Contents |
| --- | ---: | ---: | ---| --- |
| `.text` | `0x080081c8` | 338,808 B | FLASH | Code and read-only data, including `transientData` |
| `.itcm` | `0x00000000` | 3,768 B | ITCM | Hot code copied from FLASH |
| `.dtcm` | `0x20000000` | 8,708 B | DTCM | Immutable DSP lookup tables |
| `.dtcmz` | `0x20002204` | 3,572 B | DTCM | DSP/audio working buffers |
| `.dma_nocache` | `0x20020000` | 3,100 B | SRAM1 | DMA audio/ADC buffers |
| `.data` | `0x20020c1c` | 404 B | SRAM1 | Initialized globals |
| `.bss` | `0x20020db0` | 63,276 B | SRAM1 | Zero-initialized globals |

The FLASH load image ends at `0x0805df90`, safely before the sample boundary
`0x08080000`. `build/lxr02.bin` is 352,144 B.

## Major owners

| Symbol/group | Size | Region | Purpose |
| --- | ---: | --- | --- |
| `scenes` | 20,992 B | SRAM1 | Sixteen `scene_t`; Pattern payload is 16 x 112 B |
| `fs_list_cache_name` | 9,000 B | SRAM1 | Shared library-name/HCNAMES cache |
| `afatfs` | 7,344 B | SRAM1 | Async FAT state |
| `runtime_slots` | 7,056 B | SRAM1 | Six tagged 1,176-B engine slots |
| `slider_lut` | 4,096 B | SRAM1 | 1,024 native float nodes; raw `>> 2`, no interpolation |
| `fs_stage_workspace` | 2,048 B | SRAM1 | Aligned Kit/Instrument/Scene staging |
| `usb_MidiMessages` | 2,048 B | SRAM1 | USB MIDI storage |
| `USB_OTG_dev` | 1,524 B | SRAM1 | USB device state |
| `sample_info_cache` | 1,440 B | SRAM1 | Sample information cache |
| `sample_name_cache` | 1,080 B | SRAM1 | Sample-name cache |
| `transientData` | 26,460 B | FLASH at `0x08053264` | Immutable PCM ROM; no RAM shadow |
| `sine_table` | 8,194 B | DTCM | Sine lookup table |
| `squareRootLut` | 512 B | DTCM | Mixer pan-gain LUT |
| `audioOutBuffer` + `audioOutBuffer2` | 3,072 B | DTCM | CPU DSP output buffers |
| `dma_buffer` + `dma_buffer2` | 3,072 B | SRAM1 DMA | DMA audio buffers |
| `adc_dma_buf` | 28 B | SRAM1 DMA | ADC conversion buffer |

Remaining normal SRAM1 is distributed across filesystem operations, menu/UI,
MIDI rings, sequencer state, modulation metadata, and small driver records.

## Checkpoints

- Pattern: no `Step[7][128]` symbol is linked; only the 1,792-B resident bitmap
  payload remains.
- Slider: 1,024-float LUT is 4,096 B; attenuator resolution changed only.
- Runtime: exactly one `runtime_slots` owner links; no old engine pool/global
  symbol links.
- Transient: DTCM `.dtcm` is 8,708 B, exactly 26,460 B lower after moving the
  PCM table to FLASH.

## Verification

```sh
make -j2
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
```
