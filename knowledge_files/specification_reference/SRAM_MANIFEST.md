# SRAM manifest

The detailed section/symbol inventory below was regenerated from the current
2026-08-31 logging-on Session 059 Gate B build (HEAD `0c90434` plus the
uncommitted Phase Two source patch).
`arm-none-eabi-size build/lxr02.elf` reports `text=385,420`, `data=404`, and
`bss=96,176`; `build/lxr02.bin` is 385,824 B and `build/LXRV2_lxr02.img` is
385,840 B. The approved 258-byte `fs_resident_source` cache, the one-byte
`menu_pendingPageSwitch`, and the Session 058 allocations listed in the
"2026-08-30 Session 058 net allocation note" all share normal SRAM1. This
remains a linked-image inventory: sizes come from `arm-none-eabi-size -A` and
`arm-none-eabi-nm -S --size-sort`, not source estimates. Earlier session
figures retained below are historical baseline notes, not the current total.

Session 052 adds no retained allocation (Bank present-mask witness reuses the
existing eight-byte trace record/ring; the no-op dirty-mark fallback uses only
the existing canonical mutation mask). Session 057 added a handful of
operation-scoped scratch bytes (see its note below). Session 058 adds the
Option 1/2 and fast-drain allocations documented in its note below. Session 059
Phase One and Phase Two add no retained allocation.

`DEV_LOGGING_IWDG`'s retained boot capsule (config.h; see DEV_MODES.md) adds a
new, separate 12-of-32-approved-byte allocation in previously-unmapped SRAM2
(`0x2007c000`), the `.devwdg_noinit` linker section in
STM32F765VIHx_FLASH.ld. It does not touch SRAM1 or DTCM and is outside the
table below, which only ever covered SRAM1/DTCM; confirmed via
`arm-none-eabi-size -A`: `.devwdg_noinit 12 537378816` (`0x2007c000`).
DEV_MODE_LOGGING-and-DEV_LOGGING_IWDG-gated only, lifetime one boot attempt,
owner filesystem.c.

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
| SRAM1 normal (`.data` + `.bss`) | `0x20020c1c` | included below | 89,908 B | included in SRAM1 total |
| **SRAM1 total** | `0x20020000` | **376,832 B** | **93,008 B** | **283,824 B — future Pattern data only** |
| **All static allocated RAM** | — | — | **105,288 B** | — |

The image contains 404 B of initialized SRAM1 data and 96,176 B of
zero-initialized data: 3,100 B in `.dma_nocache`, 89,504 B in normal SRAM1
`.bss`, and 3,572 B in DTCM `.dtcmz`. The initialized DTCM `.dtcm` section is
read-only table storage at runtime but still consumes 8,708 B of DTCM capacity.

## Linker sections

| Section | Address | Size | Region | Contents |
| --- | ---: | ---: | ---| --- |
| `.text` | `0x080081c8` | 372,488 B | FLASH | Firmware code and ordinary read-only data, including `transientData` |
| `.itcm` | `0x00000000` | 3,768 B | ITCM | Hot code copied from FLASH at reset |
| `.dtcm` | `0x20000000` | 8,708 B | DTCM | Fast immutable DSP lookup tables |
| `.dtcmz` | `0x20002204` | 3,572 B | DTCM | Zero-initialized DSP/audio working buffers |
| `.dma_nocache` | `0x20020000` | 3,100 B | SRAM1 | DMA audio/ADC buffers |
| `.data` | `0x20020c1c` | 404 B | SRAM1 | Initialized writable globals |
| `.bss` | `0x20020da8` | 89,504 B | SRAM1 | Normal zero-initialized globals |

The final FLASH load image remains safely before the reserved sample-FLASH
boundary `0x08080000`. `build/lxr02.bin` is 385,824 B.

## Primary SRAM1 owners

| Symbol | Size | Purpose |
| --- | ---: | --- |
| `scenes` | 20,992 B | 16 resident `scene_t` values; each holds one 112-B bitmap `PatternSet` |
| `fs_list_cache_name` | 9,000 B | Shared typed-Instrument and numbered-library `.hcindex` browser cache |
| `fs_resident_source` | 258 B | Persistent 129-row HCNAMES provenance register; approved filesystem-owned source cache |
| `hcnames_name_mirror` | 1,161 B | Session 058 Option 1C dedicated 129-by-9 HCNAMES name mirror (replaces HCNAMES borrowing of `fs_list_cache_name`) |
| `hcnames_mirror_valid` | 1 B | Session 058 Option 1C tri-state mirror validity gate |
| `op_bank_child_display` | 144 B | Session 058 Option 1A 16-by-9 Bank-local child display names, captured in one scan |
| `text_buf_pos` + `text_buf_len` | 4 B | Session 058 Option 1D buffered text-reader cursors |
| `op_bank_cwd_at_parent` | 1 B | Session 058 Option 1B Bank-delegated parent-CWD retention flag |
| `bank_scene_sd_clean_mask` + `bank_scene_sd_clean_slot` + `bank_sd_save_mutated_mask` | 6 B | Session 058 Option 2 card-verified clean-Scene authority (2 B BSS mask, 2 B `.data` slot, 2 B BSS mutation-during-save) |
| `op_bank_sd_clean_candidate_mask` + `op_bank_sd_clean_candidate_slot` | 4 B | Session 058 Option 2 operation-scoped save candidate (2 B BSS + 2 B `.data`) |
| `fs_fast_drain_active` | 1 B | Session 058 foreground-only fast-drain selector |
| `wait_started_tick` | 2 B | Session 058 SD response-wait start timestamp; repurposed from the retired `retry_count` (no net new SRAM) |
| `menu_pendingPageSwitch` | 1 B | Approved normal-SRAM1 queued non-Load destination while a busy Load/Save owner drains; page-plus-one encoding, no payload/name storage |
| `afatfs` | 6,984 B | Async FAT filesystem state |
| `runtime_slots` | 7,056 B | Six tagged engine slots, 1,176 B reserve each |
| `sample_info_cache` | 1,440 B | Sample-information cache |
| `sample_name_cache` | 1,080 B | Sample-name cache |
| `USB_OTG_dev` | 1,524 B | USB device state |
| `fs_stage_workspace` | 2,048 B | Aligned Kit/Instrument/Scene staging workspace |
| `autosave_dirty_mask` | 3,856 B | Sole canonical AutoSave mutation mask |
| `fs_autosave_parameter_cache` | 4,608 B | Dedicated bounded AutoSave patch offsets/values |
| `autosave_trace_records` | 16,384 B | `DEV_MODE_LOGGING`-only 2,048-by-8-byte lifecycle ring; temporary approved diagnostic expansion |
| AutoSave trace cursors/cadence/witness latches | 12 B | `DEV_MODE_LOGGING`-only: three 16-bit ring cursors/drop count, 16-bit flush cadence, and 4 B of W/F/G observer latches |
| `drumset_apply_stall_ticks` | 2 B | Normal SRAM1 bound for a continuously non-quiet Scene post-load voice apply |
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
| `transientData` | 26,460 B | FLASH `.text` at `0x08058e40` | Immutable transient PCM ROM; no DTCM/SRAM shadow |
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
| Transient PCM ROM | `transientData` is 26,460 B at `0x0805b3a0` in FLASH. DTCM `.dtcm` is 8,708 B, down exactly 26,460 B from the preceding image. |

## Verification commands

```sh
make -j2
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
```

For the current logging-on image, conventional `arm-none-eabi-size` reports
`text=385,420 B`, `data=404 B`, and `bss=96,176 B`. The latter is the combined
zero-init total across memory regions; `size -A` provides the section split
above. Regenerate both configurations before a future change that alters
logging-gated allocations.

Session 051 moved no allocated region, but the linked totals shifted from the
Session 050 build: text grew 1,360 B, initialized `.data` grew 4 B, and bss
shrank 12 B (net -8 B RAM). The four initialized bytes must still be
identified to their owner under the allocation policy even though total RAM
shrank.

## 2026-08-30 Session 058 net allocation note

Independently re-verified against the current working tree at HEAD `124a6cf`
(not copied from a planning document): `arm-none-eabi-size build/lxr02.elf`
reports `text=382,700 B`, `data=404 B`, `bss=96,160 B` (`dec=479,264`).
Section split (`size -A`): `.dma_nocache=3,100`, `.data=404`, normal SRAM1
`.bss=89,488`, `.dtcm=8,708`, `.dtcmz=3,572`, `.itcm=3,768`,
`.devwdg_noinit=0`. Compared against the Session 057 close-out build
(`text=380,436 B`, `data=408 B`, `bss=94,848 B` per `MEMORY.md` and the note
below): **net +2,264 B text, -4 B data, +1,312 B bss**.

The Session 058 additions are all normal SRAM1 and named in the primary-owners
table above:

- Option 1 (1A/1B/1C/1D): `hcnames_name_mirror` 1,161 B, `hcnames_mirror_valid`
  1 B, `op_bank_child_display` 144 B, `text_buf_pos` + `text_buf_len` 4 B,
  `op_bank_cwd_at_parent` 1 B — **1,311 B**, within the approved 1,320-byte
  reservation.
- Option 2: `bank_scene_sd_clean_mask` 2 B + `bank_sd_save_mutated_mask` 2 B +
  `op_bank_sd_clean_candidate_mask` 2 B in `.bss`, and
  `bank_scene_sd_clean_slot` 2 B + `op_bank_sd_clean_candidate_slot` 2 B in
  `.data` (non-zero `BANK_SD_CLEAN_SLOT_NONE = 0xffff` initializers) — **10 B**
  (the proposal's "about eight bytes" omitted the candidate-slot retention).
- Fast drain: `fs_fast_drain_active` 1 B.
- SD real-time timeout: `wait_started_tick` 2 B repurposed from the retired
  `retry_count` — net zero.

Net new retained RAM = 1,311 + 10 + 1 = **1,322 B**. The source-level symbol
sizes above are exact; the linked `.data`/`.bss` aggregates can differ slightly
from a source sum because of section alignment. None of the Session 058
allocations draws against either reserved pool (DTCM delay-line headroom,
SRAM1 Pattern-data headroom). The two 2-byte `.data` slot fields
(`bank_scene_sd_clean_slot`, `op_bank_sd_clean_candidate_slot`) are
initialized non-zero globals, not a new buffer/cache/pool, and the `-4 B data`
net versus Session 057 is a session aggregate, not an indication those fields
were removed — they are present and accounted above.

## 2026-08-30 Session 059 AsyncFATFS Phase One/Two result

The clean logging-on Phase Two link keeps the AsyncFATFS owner at
`afatfs=6,984 B` (`0x1b48`), unchanged from the Phase One baseline. The
compile-time retained-state checks remain `afatfsCreateFile_t=144 B`,
`afatfsFile_t=188 B`, and `afatfsRenameObject_t=552 B`. The final link reports
`text=385,420 B`, `data=404 B`, `bss=96,176 B`; `size -A` reports
`.text=372,488 B`, `.data=404 B`, `.bss=89,504 B`, `.dma_nocache=3,100 B`,
`.dtcm=8,708 B`, and `.dtcmz=3,572 B`. The generated payload is 385,824 B and
the packaged image is 385,840 B.

Phase Two replaces appended-directory full-cluster zero-fill with one
first-sector initialization and adds no retained field, global, cache, buffer,
or operation-state member. Because the direct owner symbol and asserted
layouts are unchanged, Phase One and Phase Two add **zero retained SRAM**.
No Pattern-reserved SRAM1 or delay-line-reserved DTCM was used.

Gate B hardware/media testing was deliberately deferred by the user. No
SD-card fixture, raw-sector inspection, reboot/remount run, host FAT check,
future-size Pattern fixture, or repeatable Bank timing run was performed.
Source review and the forced ARM build found no expected problem, but hardware
acceptance is not claimed.

## 2026-08-28 Session 057 net allocation note

Independently re-verified against the current working tree (not copied from a
planning document): `arm-none-eabi-size build/lxr02.elf` reports
`text=380,436 B`, `data=408 B`, `bss=94,848 B` (`dec=475,692`). Compared
against the Session 056 close-out build (`text=381,268 B`, `data=400 B`,
`bss=94,800 B`, per `056_SESSION_HANDOFF_LOG.md` §5): **net -832 B text,
+8 B data, +48 B bss** across the whole session.

The net bss figure is small because it nets two much larger opposing changes,
not because little changed. Additions, all normal SRAM1 `.bss`, `filesystem.c`
unless noted: 4 bytes of settings-recovery scratch (§6 of
`057_SESSION_HANDOFF_LOG.md`); `op_load_invalid_layer`, `op_bank_scene_failed_mask`,
`op_bank_existing_dir_found`, `op_delete_slot_bank_local` (Bank/Scene
quarantine and per-child Bank Save state, a few bytes each); `pm_bank_load_failed_scene_mask`
in `presetManager.c`; and seven new stall-detector site pairs (`_last_phase`
uint8_t + `_ticks` uint32_t each, gated `#if DEV_STALL_DETECTION`, default on).
Removals: three reverted `afatfsFile_t` slots at the corrected 188 bytes each
(564 bytes, `asyncfatfs.c` — see the `ASYNCFATFS_REFERENCE.md` handle-pool
correction from the same session) and the 4-byte `op_bank_total_ticks`
counter, added and then fully removed within the session (net zero
contribution to the final image, but real churn along the way).

This note states the verified before/after totals and the qualitative set of
additions/removals; it does not claim a byte-exact per-symbol reconciliation
(that would need an `arm-none-eabi-nm -S --size-sort` diff against a rebuilt
Session 056 baseline, not performed this pass). No RAM allocation this
session drew against either reserved pool (DTCM delay-line headroom, SRAM1
Pattern-data headroom) — every new static above is a handful of bytes of
operation-scoped scratch, not a new buffer/cache/pool.

## 2026-08-16 Scene-Load record-publication allocation

The current logging-on image carries the approved temporary
`AUTOSAVE_TRACE_RECORD_COUNT == 2048` ring: 16,384 B in normal SRAM1,
owned by `AutosaveTrace.c` for the process lifetime and omitted entirely when
`DEV_MODE_LOGGING == 0`. Its cursor/drop state, filesystem flush cadence, and
the W/F/G evidence latches total 12 B, also logging-only. The separately
approved `drumset_apply_stall_ticks` is 2 B of normal SRAM1 `.bss`, owned by
`presetManager.c` for the cooperative Scene post-load worker's process
lifetime. It bounds a non-quiet envelope wait at 1,000 foreground passes; it
does not hold payload, identity, or persistence data. No DTCM, DMA, name-cache,
AutoSave-mask, patch-cache, or additional writer allocation was added.

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

## 2026-08-11 deferred Load/Save exit allocation

User-approved `menu_pendingPageSwitch` is exactly **one byte** of normal
SRAM1 `.bss`, owned by `menu.c`. Zero means no pending request; otherwise it
holds the latest non-Load physical destination plus one while Load/Save's
existing storage/apply owner is busy. Its lifetime ends when the next safe
`menu_pollPresetStatus()` invokes the existing `menu_switchPage()` exit path.
It owns no payload, name, filesystem handle, or AutoSave state. The allocation
exists solely to preserve the normal mode-switch exit intent until cleanup can
begin; its linked section impact is checked by the build below.

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
