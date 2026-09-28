# SRAM manifest

Current static-memory reference for the STM32F765VIH6 firmware. The baseline is
the clean Session 069 Pass 1 link at commit `1f7a772` (2026-09-20), recorded in
`S069_ATS_PAT_BOUNDED_PASS1_IMPLEMENT.md`, with Session 070 and Session 071
deltas applied. Session 071 final build: `text=456,748`, `data=416`,
`bss=291,900`. Session 071 added +86 bytes bss (+30 BankData per-Scene mask,
+32 `op_bankset_state` staging, +12 audio-out step-override table, +12
FX-send step-override table). Step 1 updates below are the first clean-link
measurements after the arena and `FxBuffer` changes; the exact tool output is
recorded in `S072_ST1_IMPLEMENTATION.md` §22. Rebuild after later
header/configuration edits before treating any linked total as current. The
S072 Step 4 clean link is recorded below: `text=463,552`, `data=416`,
`bss=425,936`, with `scenes=25,952` (`0x6560`). The S072 Step 5 production
clean link is `text=465,352`, `data=416`, `bss=425,936`; its flash image uses
465,768 B and its DTCM ledger is recorded below. Step 6 adds 16 Effect
HCNAMES rows, the `.fx` parser/writer and boot reader, and the approved SRAM1
expansion described below. The current ST6 production link is `text=470,208`,
`data=408`, `bss=426,128`; its generated flash payload is 470,616 B. Step 7 adds the
14-byte `menuEffects` page-state owner; the measured ST7 production link is
`text=475,592`, `data=416`, `bss=426,144`, with a 476,008-byte flash payload.
The linker-visible SRAM1 increase rounds to 16 bytes; the page-state object is
14 bytes. Step 8 adds 8 bytes to `effects_state`, one SRAM1 FX-event latch,
and 7 bytes of `menuEffects` hold/LED state. The current ST8 production link
is `text=478,720`, `data=416`, `bss=426,160`, with a 479,136-byte flash
payload. Step 5 adds the
76-byte `effects_state` in SRAM1 (expanded to 84 bytes by Step 8), the
76-byte `effects_runtime` in DTCM, and the one-byte diagnostic registry result;
exact section totals are recorded below.

Configuration: `DEV_MODE_LOGGING=1`, `DEV_LOGGING_IWDG=0`,
`DEV_STALL_DETECTION=1`, `AUTOSAVE_TRACE_RECORD_COUNT=2048`,
`PAT_TRACE_RECORD_COUNT=32`, and `PAT_STACK_SIZE=256` in `config.h`.

## Static RAM ledger

| Region and section | Start | Capacity | Static bytes | Capacity after static bytes |
| --- | ---: | ---: | ---: | ---: |
| SRAM1 `.dma_nocache` | `0x20020000` | Part of SRAM1 | 3,100 | — |
| SRAM1 `.data` | `0x20020c1c` | Part of SRAM1 | 416 | — |
| SRAM1 `.bss` | `0x20020dc0` | Part of SRAM1 | 292,500 | — |
| SRAM1 normal (`.data` + `.bss`) | `0x20020c1c` | Part of SRAM1 | 292,916 | — |
| **SRAM1 total** | `0x20020000` | **376,832** | **296,016** | **80,816** |
| DTCM `.dtcm` | `0x20000000` | Part of DTCM | 512 | — |
| DTCM `.dtcmz` | `0x20000200` | Part of DTCM | 3,936 | — |
| DTCM `.dtcm_fxbuf` | `0x20001160` | Part of DTCM | 126,624 | 0 (reserved arena) |
| **DTCM total** | `0x20000000` | **131,072** | **131,072** | **0** |
| ITCM `.itcm` executable code | `0x00000000` | 16,384 | 3,768 | 12,616 |
| SRAM2 `.devwdg_noinit` | `0x2007c000` | 16,384 | **0** | See stack note |

Static **data** RAM is 427,088 B (SRAM1 + DTCM, including the NOLOAD arena);
including ITCM code, linked RAM sections occupy 430,856 B. The conventional
`arm-none-eabi-size` `bss` column includes the 126,624-byte NOLOAD arena and
must not be interpreted as new SRAM1 use. Use the section ledger and the
separate `tools/link_budget.py` report for DTCM arena accounting. `.dtcm`
contains only the 512-byte `squareRootLut`; `sine_table` is now ordinary flash
`.rodata` and is no longer copied to DTCM.

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
| `SceneData.c`: `scenes` | 25,952 | Sixteen resident Scene records, 1,622 B each: 41 B settings, one alignment byte, 420 B Scene-owned Effect record, and the existing Kit; Pattern regions are separate. |
| `PatternData.c`: `pat_regions` | 168,304 | Sixteen packed regions of 10,519 B: each has 1,792 B step addresses, 8,192 B pool, 512 B bitmap, and 23 B Pattern/track settings. |
| `PatternData.c`: `pat_autosave_snapshot` | 10,519 | One Scene-sized snapshot for an in-flight Pattern AutoSave. |
| `PatternStackService.c`: `reservation_image` | 512 | One non-persisted bit image for the current service Scene's trailing pool reservations; three separate one-byte policy/rebuild flags accompany it. |
| `PatternStackService.c`: `service_queue` | 256 | Sixty-four 32-bit mutation entries; cursors and repair/handover state are additional small SRAM1 objects. |
| `Autosave.c`: `autosave_dirty_mask` | 3,856 | Sole canonical scalar dirty-bit mask. |
| `Autosave.c`: Pattern dirty masks | 4 | Two 16-bit Scene masks: semantic and non-semantic relocation work. |
| `Autosave.c`: `autosave_dirty_count`, `autosave_last_pattern_semantic_us` | 6 | Exact scalar dirty-bit count and latest semantic Pattern edit timestamp. |
| `filesystem.c`: `fs_pattern_generation`, `fs_pattern_drain_scene`, `fs_pattern_first_dirty_us`, `fs_pattern_scene_cursor` | 70 | Sixteen Pattern generation baselines, drain selector, first-dirty timestamp, and fair Scene cursor. |
| `filesystem.c`: `fs_autosave_parameter_cache` | 4,608 | Bounded scalar AutoSave patch offsets and values. |
| `filesystem.c`: `fs_stage_workspace` | 2,048 | One union shared by Kit, Instrument, Scene+Effect, AutoSave writer, and HCNAMES regeneration staging. The Scene+Effect peak is 1,621 B; union members are not additive. |
| `filesystem.c`: `staging_buf` | 512 | Shared streaming and trace-batch buffer. |
| `filesystem.c`: `fs_list_cache_name` | 9,000 | One 1,000 × 9 browser/index name cache. |
| `filesystem.c`: `hcnames_name_mirror`, `fs_resident_source` | 1,771 | Separate 161 × 9 HCNAMES names and 161 × 2 provenance sources; Effect rows are 145..160. |
| `filesystem.c`: `op_effect_display_name` | 9 | Cached Effect filename stem for the current Scene/Bank child save. |
| `filesystem.c`: `op_effect_state` | 7 | Bounded `.fx` parser state retained across async file-reader passes. |
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
| `FxBuffer.c`: `fxbuf_state` | 28 | Linker arena base/size, twelve unit owners, count, and share callback. |
| `FxBuffer.c`: `fxbuf_handoffRecord` | 180 | Effect/voice handoff metadata and arena-relative positions. |
| `EffectsManager.c`: `effects_state` | 84 | Active type/Scene, force flag, 64-byte last-applied image, FX step/selection/held-Morph state, sequence signature, and common runtime values. |
| `sequencer.c`: `seq_fxEvent` | 1 | TIM3-to-foreground newest-wins RESET/STEP latch; no Scene, DSP, or LED work occurs in the ISR. |
| `menuEffects.c`: page state | 21 | Eight SELECT screen cells, Morph-view flag, `typ` transaction state, last Scene/type tracking, SEQ hold mask, and LED repaint signature; SRAM1, foreground UI lifetime. |

Other SRAM1 state comprises filesystem operation cursors and text buffers,
HCNAMES/boot control fields, Menu and front-panel state, sequencer/MIDI state,
modulation metadata, USB/driver records, and section padding. Notable small
owners are `menu_pendingPageSwitch` (1 B), `fs_boot_latch` (6 B linked),
`fs_boot_winner` (12 B linked), and `drumset_apply_stall_ticks` (2 B).
No Pattern storage is embedded in `scene_t`; `PAT_STACK_SIZE=256` reserves
8,192 B of pool per Scene while the 512 B bitmap covers the full address range.

## Conditional diagnostic SRAM1

These objects are conditional development allocations. Logging-only rows are
compiled out with their producers when `DEV_MODE_LOGGING=0`, while the
FxBuffer self-test row exists only when `DEV_MODE_DIAGNOSTIC=1`. A mode-off
section total must be measured from a clean rebuild; subtracting this table
from a mode-on total would miss alignment and other compile-time changes.

| Owner / object | Bytes | Use |
| --- | ---: | --- |
| `AutosaveTrace.c`: `autosave_trace_records` | 16,384 | Temporary 2,048 × 8 record ring. |
| `AutosaveTrace.c`: three 16-bit cursors/counter | 6 | Ring publication, flush, and dropped-record state. |
| `PatternTrace.c`: `pattern_trace_records` | 256 | 32 × 8 Pattern/automation diagnostic ring. |
| `PatternTrace.c`: three 16-bit cursors/counter | 6 | Pattern trace publication, flush, and dropped-record state. |
| `filesystem.c`: `fs_hcprms_boot_capsule` | 64 | Eight × 8 B frozen boot-ensure failure records; useful for one boot attempt. |
| `filesystem.c`: trace flush cadence and witness state | 3 | `fs_autosave_trace_next_due_tick` and `fs_trace_suppress_witness`; other logging control is included in the section total. |
| `buttonHandler.c`: `evt_drop_count` | 1 | Saturating front-panel overflow witness. |
| `FxBuffer.c`: `fxbuf_selfTestResult` | 1 | Diagnostic-only allocation self-test result; absent when `DEV_MODE_DIAGNOSTIC=0`. |
| `EffectsManager.c`: `effects_registryCheckCode` | 1 | Diagnostic-only registry invariant result; absent when `DEV_MODE_DIAGNOSTIC=0`. |

`DEV_STALL_DETECTION=1` also retains its separate phase/tick detector state;
its condition is `DEV_STALL_DETECTION`, rather than `DEV_MODE_LOGGING` alone.

## DTCM, DMA, FLASH, and allocation rule

| Object | Bytes | Placement |
| --- | ---: | --- |
| `squareRootLut` | 512 | DTCM `.dtcm`; `sine_table` moved to flash in S072 Step 1. |
| `EffectsManager.c`: `effects_runtime` | 76 | DTCM `.dtcmz`; union holding the active type's DSP runtime. |
| `mixer_fx_bus` | 256 | DTCM `.dtcmz`; two-channel 32-frame union used for saturated voice sends and in-place Effect floats. |
| `mixer_send_last_gain[6]` | 24 | DTCM `.dtcmz`; per-slot block-end send/fader ramp origins. |
| `mixer_fx_return_last_gain[2]` | 8 | DTCM `.dtcmz`; left/right Effect return ramp origins. |
| `.dtcm_fxbuf` | 126,624 | DTCM NOLOAD; elastic FX/voice audio arena owned by FxBuffer after the approved 288-byte Step 5 mixer allocation. |
| `audioOutBuffer`, `audioOutBuffer2` | 3,072 combined | DTCM `.dtcmz`; oscillator interpolation buffers and other DSP state account for the remainder. |
| `velocityModulators` | 264 | DTCM `.dtcmz`; six modulation nodes. |
| `osc_interp_a`, `osc_interp_b` | 128 combined | DTCM `.dtcmz`; two 32-sample interpolation buffers. |
| `dma_buffer`, `dma_buffer2`, `adc_dma_buf` | 3,072 + 28 | SRAM1 `.dma_nocache`; the entire section must stay within the linker's 4,096 B MPU limit. |
| `transientData` | 26,460 | FLASH `.text` constant PCM; no DTCM/SRAM shadow. |

The current production link's conventional `bss` figure includes the
126,624-byte NOLOAD arena. FLASH and sample-FLASH capacity are not SRAM
headroom.

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
