# SRAM manifest

Current static-memory reference for the STM32F765VIH6 firmware. The baseline is
the clean Session 069 Pass 1 link at commit `1f7a772` (2026-09-20), recorded in
`S069_ATS_PAT_BOUNDED_PASS1_IMPLEMENT.md`. The subsequent commit `55fe250`
changes only a planning document. This checkout has no `build/lxr02.elf` or
ARM toolchain, so the linked totals below are taken from that recorded build;
section sizes are reconciled with its `size` result and the current source.
Per-symbol sizes below come from fixed source geometry or the earlier linked
symbol inventory. Rebuild before treating them as a new link measurement.

Configuration: `DEV_MODE_LOGGING=1`, `DEV_LOGGING_IWDG=0`,
`DEV_STALL_DETECTION=1`, `AUTOSAVE_TRACE_RECORD_COUNT=2048`,
`PAT_TRACE_RECORD_COUNT=32`, and `PAT_STACK_SIZE=256` in `config.h`.

## Static RAM ledger

| Region and section | Start | Capacity | Static bytes | Capacity after static bytes |
| --- | ---: | ---: | ---: | ---: |
| SRAM1 `.dma_nocache` | `0x20020000` | Part of SRAM1 | 3,100 | — |
| SRAM1 `.data` | `0x20020c1c` | Part of SRAM1 | 404 | — |
| SRAM1 `.bss` | `0x20020db0` | Part of SRAM1 | 285,052 | — |
| SRAM1 normal (`.data` + `.bss`) | `0x20020c1c` | Part of SRAM1 | 285,456 | — |
| **SRAM1 total** | `0x20020000` | **376,832** | **288,556** | **88,276** |
| DTCM `.dtcm` | `0x20000000` | Part of DTCM | 8,708 | — |
| DTCM `.dtcmz` | `0x20002204` | Part of DTCM | 3,572 | — |
| **DTCM total** | `0x20000000` | **131,072** | **12,280** | **118,792** |
| ITCM `.itcm` executable code | `0x00000000` | 16,384 | 3,768 | 12,616 |
| SRAM2 `.devwdg_noinit` | `0x2007c000` | 16,384 | **0** | See stack note |

Static **data** RAM is 300,836 B (SRAM1 + DTCM); including ITCM code, linked
RAM sections occupy 304,604 B. The conventional `arm-none-eabi-size` result is
`text=449,476`, `data=404`, `bss=291,724`: its `bss` combines SRAM1 `.bss`
(285,052), SRAM1 `.dma_nocache` (3,100), and DTCM `.dtcmz` (3,572). DTCM
`.dtcm` contains initialized lookup data copied from FLASH; it consumes DTCM
even though the tables are read-only at runtime.

The linker sets `_estack=0x20080000`, the **top of SRAM2**. The stack grows
downward and has no fixed linker reservation or measured high-water mark; it
is absent from all static totals. SRAM2's 16,384 B therefore cannot be read
as available feature RAM. The disabled IWDG feature would place a 12 B
cross-reset capsule at SRAM2's base when both `DEV_MODE_LOGGING` and
`DEV_LOGGING_IWDG` are enabled. It occupies **0 B in this configuration**.
The linker caps that section at 32 B.

## Resident SRAM1 owners

All sizes are bytes. Objects below have firmware lifetime unless a shorter
*useful-content* lifetime is stated; clearing or reusing an object does not
release its linked storage. The section ledger above includes every linked
static byte, including alignment and smaller control/driver variables omitted
from this owner map.

| Owner / object | Bytes | Allocation and use |
| --- | ---: | --- |
| `SceneData.c`: `scenes` | 19,200 | Sixteen resident Scene records; Pattern regions are separate. |
| `PatternData.c`: `pat_regions` | 168,304 | Sixteen packed regions of 10,519 B: each has 1,792 B step addresses, 8,192 B pool, 512 B bitmap, and 23 B Pattern/track settings. |
| `PatternData.c`: `pat_autosave_snapshot` | 10,519 | One Scene-sized snapshot for an in-flight Pattern AutoSave. |
| `PatternStackService.c`: `reservation_image` | 512 | One non-persisted bit image for the current service Scene's trailing pool reservations; three separate one-byte policy/rebuild flags accompany it. |
| `PatternStackService.c`: `service_queue` | 256 | Sixty-four 32-bit mutation entries; cursors and repair/handover state are additional small SRAM1 objects. |
| `Autosave.c`: `autosave_dirty_mask` | 3,856 | Sole canonical scalar dirty-bit mask. |
| `Autosave.c`: Pattern dirty masks | 4 | Two 16-bit Scene masks: semantic and non-semantic relocation work. |
| `Autosave.c`: `autosave_dirty_count`, `autosave_last_pattern_semantic_us` | 6 | Exact scalar dirty-bit count and latest semantic Pattern edit timestamp. |
| `filesystem.c`: `fs_pattern_generation`, `fs_pattern_drain_scene`, `fs_pattern_first_dirty_us`, `fs_pattern_scene_cursor` | 70 | Sixteen Pattern generation baselines, drain selector, first-dirty timestamp, and fair Scene cursor. |
| `filesystem.c`: `fs_autosave_parameter_cache` | 4,608 | Bounded scalar AutoSave patch offsets and values. |
| `filesystem.c`: `fs_stage_workspace` | 2,048 | One union shared by Kit, Instrument, Scene, AutoSave writer, and HCNAMES regeneration staging. Union members are not additive. |
| `filesystem.c`: `staging_buf` | 512 | Shared streaming and trace-batch buffer. |
| `filesystem.c`: `fs_list_cache_name` | 9,000 | One 1,000 × 9 browser/index name cache. |
| `filesystem.c`: `hcnames_name_mirror`, `fs_resident_source` | 1,595 | Separate 145 × 9 HCNAMES names and 145 × 2 provenance sources. |
| `filesystem.c`: `fs_identity_name`, `fs_identity_valid_mask` | 74 | Eight × 9 Scene/Kit/Instrument identity strings plus a 16-bit validity mask; the Bank's nine-byte name is held separately by BankData. |
| `filesystem.c`: `op_bank_child_scratch` | 144 | One union: 16 × 9 Bank-child names or 16 × 6 boot-reader Instrument types, with disjoint lifetimes. |
| `asyncfatfs.c`: `afatfs` | 6,984 | FAT state, caches, and five file handles in one owner. |
| `InstrumentManager.c`: `runtime_slots` | 7,056 | Six tagged 1,176 B engine slots; no parallel native engine array. |
| `adcPots.c`: `slider_lut` | 4,096 | 1,024 `float` slider conversion values. |
| `SampleMemory.c`: resident and install caches | 5,040 | 1,440 B `sample_info_cache`, 1,080 B `sample_name_cache`, 120 B loop flags, 1,440 B `install_info`, and 960 B `install_names`. |
| `usb_manager.c`: `USB_OTG_dev` | 1,524 | USB core/device handle. |
| `usb_midi_core.c`: `usb_MidiMessages` | 2,048 | USB MIDI input ring. |
| `sequencer.c`: pending automation + dirty bits | 560 | 128 four-byte pending records (512 B) and six per-voice 64-bit dirty maps (48 B). |
| `InstrumentManager.c`: `lfo_descriptor_targets` | 192 | Twelve descriptor LFO adapters for six slots and two target pairs. |
| `menu.c`: `parameter_values` | 384 | Legacy Menu/MIDI parameter cells. |
| `MidiParser.c`: `midiParser_originalCcValues` | 255 | Legacy MIDI CC baseline cells. |
| `buttonHandler.c`: `evt_ring` | 64 | Sixty-four one-byte front-panel events; producer/consumer and overflow state are additional bytes. |
| `lcd.c`: `lcd_queue` | 384 | LCD command queue. |

Other SRAM1 state comprises filesystem operation cursors and text buffers,
HCNAMES/boot control fields, Menu and front-panel state, sequencer/MIDI state,
modulation metadata, USB/driver records, and section padding. Notable small
owners are `menu_pendingPageSwitch` (1 B), `fs_boot_latch` (6 B linked),
`fs_boot_winner` (12 B linked), and `drumset_apply_stall_ticks` (2 B).
No Pattern storage is embedded in `scene_t`; `PAT_STACK_SIZE=256` reserves
8,192 B of pool per Scene while the 512 B bitmap covers the full address range.

## Conditional diagnostic SRAM1

These objects are present in this logging-on build and are compiled out with
their producers when `DEV_MODE_LOGGING=0`. A logging-off section total must be
measured from a clean rebuild; subtracting this table from the logging-on
total would miss alignment and other compile-time changes.

| Owner / object | Bytes | Use |
| --- | ---: | --- |
| `AutosaveTrace.c`: `autosave_trace_records` | 16,384 | Temporary 2,048 × 8 record ring. |
| `AutosaveTrace.c`: three 16-bit cursors/counter | 6 | Ring publication, flush, and dropped-record state. |
| `PatternTrace.c`: `pattern_trace_records` | 256 | 32 × 8 Pattern/automation diagnostic ring. |
| `PatternTrace.c`: three 16-bit cursors/counter | 6 | Pattern trace publication, flush, and dropped-record state. |
| `filesystem.c`: `fs_hcprms_boot_capsule` | 64 | Eight × 8 B frozen boot-ensure failure records; useful for one boot attempt. |
| `filesystem.c`: trace flush cadence and witness state | 3 | `fs_autosave_trace_next_due_tick` and `fs_trace_suppress_witness`; other logging control is included in the section total. |
| `buttonHandler.c`: `evt_drop_count` | 1 | Saturating front-panel overflow witness. |

`DEV_STALL_DETECTION=1` also retains its separate phase/tick detector state;
its condition is `DEV_STALL_DETECTION`, rather than `DEV_MODE_LOGGING` alone.

## DTCM, DMA, FLASH, and allocation rule

| Object | Bytes | Placement |
| --- | ---: | --- |
| `sine_table`, `squareRootLut` | 8,194 + 512 | DTCM `.dtcm`; the section's other 2 B are alignment. |
| `audioOutBuffer`, `audioOutBuffer2` | 3,072 combined | DTCM `.dtcmz`; oscillator interpolation buffers and other DSP state account for the remainder. |
| `velocityModulators` | 264 | DTCM `.dtcmz`; six modulation nodes. |
| `osc_interp_a`, `osc_interp_b` | 128 combined | DTCM `.dtcmz`; two 32-sample interpolation buffers. |
| `dma_buffer`, `dma_buffer2`, `adc_dma_buf` | 3,072 + 28 | SRAM1 `.dma_nocache`; the entire section must stay within the linker's 4,096 B MPU limit. |
| `transientData` | 26,460 | FLASH `.text` constant PCM; no DTCM/SRAM shadow. |

The last link's 449,880 B firmware payload (449,476 B `text` + 404 B `data`)
and 449,896 B packaged image fit before the `0x08080000` sample-FLASH
boundary. FLASH and sample-FLASH capacity are not SRAM headroom.

**Reservation policy:** free DTCM, including capacity released by moving
`transientData` to FLASH, is reserved exclusively for future delay-line
buffers. Free normal SRAM1 is reserved exclusively for future Pattern data.
Before adding or enlarging retained RAM, identify the exact byte count,
region, lifetime, and owner and obtain the user's acknowledgement. This
includes globals, static storage, pools/unions, DMA buffers, linker sections,
and material stack-budget increases. Releasing RAM does not authorize its
reuse by another subsystem. Logging allocations require the corresponding
logging code to be enabled and must disappear from a logging-off build.

## Reproduce a linked inventory

After any header or configuration edit, run a **clean** build: the Makefile
does not track header dependencies. Then inspect sections and named symbols:

```sh
make clean && make && make img
arm-none-eabi-size build/lxr02.elf
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
```

Use the section totals for capacity accounting; individual C declarations
can be removed, merged, or padded by optimization and linker alignment.
Historical Session 057–068 deltas remain in Git history and the corresponding
session handoff logs; they are not current allocation totals.
