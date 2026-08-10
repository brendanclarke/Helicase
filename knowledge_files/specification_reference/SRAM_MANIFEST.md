# SRAM manifest

The detailed section/symbol inventory below was generated at rollback baseline
`c9807fa` on 2026-08-10, after Session 046 boot/HCNAMES diagnostics and the
AutoSave scalar writer/trace work. Session 047 adds the logging-only 64-byte
`fs_hcprms_boot_capsule`; alignment/layout makes the observed logging-on BSS
increase 80 bytes. The current Session 047 `make` result is `text=374,044`,
`data=396`, `bss=79,076`; a forced logging-off build is `text=367,372`,
`data=396`, `bss=78,444`. The normal logging-on image was rebuilt afterward.
This remains a linked-image inventory: sizes come
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
| SRAM1 normal (`.data` + `.bss`) | `0x20020c1c` | included below | 72,724 B | included in SRAM1 total |
| **SRAM1 total** | `0x20020000` | **376,832 B** | **75,824 B** | **301,008 B — future Pattern data only** |
| **All static allocated RAM** | — | — | **88,104 B** | — |

The image contains 400 B of initialized SRAM1 data and 78,996 B of
zero-initialized data: 3,100 B in `.dma_nocache`, 72,324 B in normal SRAM1
`.bss`, and 3,572 B in DTCM `.dtcmz`. The initialized DTCM `.dtcm` section is
read-only table storage at runtime but still consumes 8,708 B of DTCM capacity.

## Linker sections

| Section | Address | Size | Region | Contents |
| --- | ---: | ---: | ---| --- |
| `.text` | `0x080081c8` | 359,976 B | FLASH | Firmware code and ordinary read-only data, including `transientData` |
| `.itcm` | `0x00000000` | 3,768 B | ITCM | Hot code copied from FLASH at reset |
| `.dtcm` | `0x20000000` | 8,708 B | DTCM | Fast immutable DSP lookup tables |
| `.dtcmz` | `0x20002204` | 3,572 B | DTCM | Zero-initialized DSP/audio working buffers |
| `.dma_nocache` | `0x20020000` | 3,100 B | SRAM1 | DMA audio/ADC buffers |
| `.data` | `0x20020c1c` | 400 B | SRAM1 | Initialized writable globals |
| `.bss` | `0x20020db0` | 72,324 B | SRAM1 | Normal zero-initialized globals |

The final FLASH load image ends at `0x0806323c`, safely before the reserved
sample-FLASH boundary `0x08080000`. `build/lxr02.bin` is 373,308 B.

## Primary SRAM1 owners

| Symbol | Size | Purpose |
| --- | ---: | --- |
| `scenes` | 20,992 B | 16 resident `scene_t` values; each holds one 112-B bitmap `PatternSet` |
| `fs_list_cache_name` | 9,000 B | Shared library-name/HCNAMES cache |
| `fs_resident_source` | 258 B | Persistent 129-row HCNAMES provenance register; approved source cache, pending the next linked-size regeneration |
| `afatfs` | 7,344 B | Async FAT filesystem state |
| `runtime_slots` | 7,056 B | Six tagged engine slots, 1,176 B reserve each |
| `sample_info_cache` | 1,440 B | Sample-information cache |
| `sample_name_cache` | 1,080 B | Sample-name cache |
| `USB_OTG_dev` | 1,524 B | USB device state |
| `fs_stage_workspace` | 2,048 B | Aligned Kit/Instrument/Scene staging workspace |
| `autosave_dirty_mask` | 3,856 B | Sole canonical AutoSave mutation mask |
| `fs_autosave_parameter_cache` | 4,608 B | Dedicated bounded AutoSave patch offsets/values |
| `autosave_trace_records` | 512 B | `DEV_MODE_LOGGING`-only 64-by-8-byte lifecycle ring |
| `fs_hcprms_boot_capsule` | 64 B | `DEV_MODE_LOGGING`-only frozen eight-record ASENSURE timeout snapshot; owned by `filesystem.c` for one boot attempt |
| `usb_MidiMessages` | 2,048 B | USB MIDI message storage |
| `slider_lut` | 4,096 B | 1,024 native `float` attenuator nodes; lookup is `raw >> 2`, without interpolation |
| `parameter_values` | 384 B | Legacy MIDI parameter cells |
| `midiParser_originalCcValues` | 255 B | Legacy MIDI CC baseline cells |
| `velocityModulators` | 264 B | One velocity modulation node per instrument slot |
| `lfo_descriptor_targets` | 192 B | Per-slot LFO descriptor target adapters |
| `lcd_queue` | 384 B | LCD command queue |
| `staging_buf` | 512 B | Shared filesystem stream/serialization scratch, including one trace batch |

The remaining normal SRAM1 state is intentionally distributed across
filesystem-operation records, Menu/UI state, MIDI rings, sequencer state,
modulation metadata, and small driver records. The section totals above include
all of them.

## DTCM, DMA, and FLASH-ROM owners

| Symbol/group | Size | Region | Purpose |
| --- | ---: | --- | --- |
| `transientData` | 26,460 B | FLASH `.text` at `0x080534f4` | Immutable transient PCM ROM; no DTCM/SRAM shadow |
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
| Transient PCM ROM | `transientData` is 26,460 B at `0x080534f4` in FLASH. DTCM `.dtcm` is 8,708 B, down exactly 26,460 B from the preceding image. |

## Verification commands

```sh
make -j2
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
```

For the current image, conventional `arm-none-eabi-size` reports `text=372,908
B`, `data=400 B`, and `bss=78,996 B`. The latter is the combined zero-init
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

## 2026-07-28 Session 044 final memory note

The boot Scene type/LFO repair reuses the existing Scene and Instrument apply
cursors. The Bank preview fix reuses `menu_storageBusy`; the final Scene/Bank
index helper derives its kind from locked Menu state; and the SD pacing helper
holds foreground time only. None adds a cache, stage, retained boot coordinate,
or modulation image.

The final image remains at `bss=69,948 B`; initialized `.data` is four bytes
smaller than the Session 043 inventory, so total static SRAM1 use is 66,776 B.
The temporary session allowance for up to 32 bytes of unanticipated growth did
not authorize reuse of any reserved capacity and no such growth was consumed.

## 2026-08-10 Session 046 rollback-baseline note

The `c9807fa` image adds the Session 045 AutoSave mask and 4,608-byte patch
cache, plus the Session 046 logging-only AutoSave trace. With
`DEV_MODE_LOGGING == 1`, trace-specific static storage is exactly 520 bytes:
512 bytes of records, six bytes of cursor/drop state, and a two-byte flush
cadence. The approved condition is binding: a logging-off build must omit the
ring/cadence and perform no trace-file I/O.

## 2026-08-10 HCPRMS boot-lock diagnostic allocation

`fs_hcprms_boot_capsule` is exactly **64 bytes** of normal SRAM1 `.bss` when
`DEV_MODE_LOGGING == 1`: eight fixed eight-byte records. It is owned solely by
`filesystem.c`, exists only for the current boot attempt, receives RAM-only
copies while `FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` is active, and is frozen
before timeout recovery destroys AsyncFATFS/SD state. It is omitted entirely
when logging is off; the AsyncFATFS and SD snapshot getters allocate no storage
and retain no pointers. The logging-on build checked for this change reports
`data=396 B`, `bss=79,076 B`; the linked 80-byte `.bss` increase includes the
64-byte owner plus unavoidable object-layout/alignment movement. No DTCM, DMA,
name-cache, AutoSave mask, or record-sized allocation is added.

The SRAM1 increase from the Session 044 snapshot is 9,048 bytes. The principal
new owners account for 8,984 bytes (3,856-byte canonical mask, 4,608-byte patch
cache, and 520-byte trace); the remaining 64 bytes are other linked
filesystem/AutoSave state and layout effects. No second dirty mask or complete
record-sized SRAM image is linked. DTCM remains unchanged at 12,280 bytes and
retains its delay-line-only remainder reservation.
