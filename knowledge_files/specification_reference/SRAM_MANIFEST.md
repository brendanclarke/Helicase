# SRAM manifest

The detailed section/symbol inventory below was regenerated from the
2026-09-18 Session 067 build (detail inventory from Session 064 build at
`d5af5fd`; Session 065/066/067 deltas noted below); the Session 068 delta is
noted separately below and is small (front-panel event-ring RAM only). Session
069's Pattern Stack Service delta is recorded below. The current Session 069
Pass 1 implementation build (2026-09-20) reports
`text=449,476`, `data=404`, and `bss=291,724` from
`arm-none-eabi-size build/lxr02.elf`.
The approved 290-byte `fs_resident_source` cache, the one-byte
`menu_pendingPageSwitch`, and Session 061 boot scratch all share normal SRAM1.
This remains a linked-image inventory: sizes come from
`arm-none-eabi-size -A` and `arm-none-eabi-nm -S --size-sort`, not source
estimates. Earlier session figures retained below are historical baseline
notes, not the current total.

Session 052 adds no retained allocation (Bank present-mask witness reuses the
existing eight-byte trace record/ring; the no-op dirty-mark fallback uses only
the existing canonical mutation mask). Session 057 added a handful of
operation-scoped scratch bytes (see its note below). Session 058 adds the
Option 1/2 and fast-drain allocations documented in its note below. Session 059
Phase One and Phase Two add no retained allocation. Session 061's HCNAMES type
schema uses existing SceneData and operation scratch. Session 061 adds a
five-byte set of filesystem boot-latch fields, seven bytes of boot-winner
fields, and six Menu notice bytes; linked LTO symbols are 6, 12, and 6 bytes
respectively because of structure alignment. The final 96-Instrument-type
lifetime fix adds no BSS: it aliases the existing 144-byte Bank-child scratch.
Session 062 B/B½ added the permanent `pat_regions` Pattern allocation and
removed the embedded Scene bitmap. Session 063 expanded each region by 23
parameter bytes and removed the retained legacy discard object. Session 064
adds exactly 10,586 bytes for one 10,519-byte Pattern snapshot, a two-byte
Pattern dirty mask, sixteen 32-bit generations, and one drain-Scene byte. The
HCNAMES mirror/source arrays expand from 129 to 145 rows (144 + 32 bytes).
Session 065 adds 50 B new static: 48 B `seq_automation_dirty[6]` (per-slot
64-bit dirty bitmap, sequencer.c) and 2 B menu state (`menu_stepAutoCursor`,
`menu_stepAutoNumberLocked`, menu.c). The 192 B sequencer pending automation
buffer was committed before Session 065. BSS delta from Session 064: +824 B.
Session 066 adds 64 B new static: 44 B VOICE overlay state block in menu.c
(overlay-active flag, active-parameter index, async search state, CGRAM valid
mask, underline suppression/validity byte, 4 B working-value cache, debounce
timestamp, and tracking fields; verified by `_Static_assert`) and 20 B
buttonHandler state (held-step timing, overlay routing). 496 B flash font table
in lcd.c (62 glyphs × 8 bytes, `.rodata`). BSS delta from Session 065: +64 B.
Text delta from Session 065: +5,360 B.
Session 067 adds 288 B new BSS in PatternStackService.c: 256 B for the 64-entry
volatile `uint32_t` SPSC ring buffer (SRAM1) and ~32 B of service state
variables (scene, open, handover, replace_pending, bulk cursors, logical chunks,
tier1 scan cursor, reactive state). Text delta: +8,184 B
(post-service) then −64 B (dtype fix), net +8,184 B from Session 066. BSS delta
from Session 066: +288 B. The dtype offset bug fix removed code (net −64 text)
but added no RAM. Session 067 final build: text=447,580, data=412, bss=291,140.
Session 068 adds 56 B new BSS in `buttonHandler.c` (front-panel event-ring
capacity expansion 16→64 entries plus overflow-detection state; see the dated
note below) and no other retained allocation — the chase-light fix
(`menu_setPlayedPattern()`) and the track-length sequencer fix are both
logic-only with zero new storage. Text delta from Session 067: +280 B across
three sub-changes. Final build: text=447,860, data=412, bss=291,196.

Session 069 adds the non-persisted Pattern Stack Service reservation image and
three one-byte policy/lifecycle flags: 515 B of source-owned SRAM1 `.bss`
state. The old `tier2_scan_cursor` and `last_compact_tick` state is removed.
Pass 1 adds 11 B of source-owned SRAM1 state for the scalar dirty count,
semantic Pattern timestamp, and Pattern scheduler epoch/cursor; the linked
image measures `text=449,476`, `data=404`, `bss=291,724` after
linker alignment. `reservation_image` is 512 B and the three flags are each
one byte in the link map. The reservation image is neither persisted in PAT4
nor duplicated per Scene/Pattern.

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
| SRAM1 normal (`.data` + `.bss`) | `0x20020c1c` | included below | 284,528 B | included in SRAM1 total |
| **SRAM1 total** | `0x20020000` | **376,832 B** | **287,628 B** | **89,204 B — future Pattern data only** |
| **All static allocated RAM** | — | — | **299,908 B** | — |

The image contains 412 B of initialized SRAM1 data and 289,964 B of
zero-initialized data: 3,100 B in `.dma_nocache`, 283,292 B in normal SRAM1
`.bss`, and 3,572 B in DTCM `.dtcmz`. The initialized DTCM `.dtcm` section is
read-only table storage at runtime but still consumes 8,708 B of DTCM capacity.

## Linker sections

| Section | Address | Size | Region | Contents |
| --- | ---: | ---: | ---| --- |
| `.text` | `0x080081c8` | 436,544 B | FLASH | Firmware code and ordinary read-only data, including `transientData` |
| `.itcm` | `0x00000000` | 3,768 B | ITCM | Hot code copied from FLASH at reset |
| `.dtcm` | `0x20000000` | 8,708 B | DTCM | Fast immutable DSP lookup tables |
| `.dtcmz` | `0x20002204` | 3,572 B | DTCM | Zero-initialized DSP/audio working buffers |
| `.dma_nocache` | `0x20020000` | 3,100 B | SRAM1 | DMA audio/ADC buffers |
| `.data` | `0x20020c1c` | 404 B | SRAM1 | Initialized writable globals |
| `.bss` | `0x20020db0` | 285,052 B | SRAM1 | Normal zero-initialized globals, including Pattern storage/snapshot |

The final FLASH load image remains safely before the reserved sample-FLASH
boundary `0x08080000`. `build/lxr02.bin` is 449,880 B; the packaged
`LXRV2_lxr02.img` is 449,896 B including its 16-byte image header.

## Primary SRAM1 owners

| Symbol | Size | Purpose |
| --- | ---: | --- |
| `scenes` | 19,200 B | 16 resident `scene_t` values; Pattern storage is external to each Scene |
| `pat_regions` | 168,304 B | 16 × 10,519-B Scene Pattern regions: address array, pool, bitmap, and 23 parameter bytes |
| `reservation_image` | 512 B | Non-persisted SRAM1 `.bss` bit image owned by PatternStackService.c; trailing-slack reservations for the current service Scene |
| Reservation density/budget/rebuild flags | 3 B | SRAM1 `.bss` policy latch, adaptive AutoSave-pressure budget flag, and lifecycle rebuild wake owned by PatternStackService.c |
| `autosave_dirty_count` | 2 B | Exact SRAM1 `.bss` population of set bits in the canonical scalar AutoSave mask |
| `autosave_last_pattern_semantic_us` | 4 B | TIM2 timestamp of the latest semantic Pattern mutation, owned by Autosave.c |
| `fs_pattern_first_dirty_us` + `fs_pattern_scene_cursor` | 5 B | Semantic Pattern quiet-window epoch and rotating drain fairness state, owned by filesystem.c |
| `pat_autosave_snapshot` | 10,519 B | Sole immutable Pattern AutoSave snapshot for one in-flight Scene |
| `autosave_pattern_dirty_mask` | 2 B | Separate one-bit-per-Scene Pattern work ownership |
| `fs_pattern_generation` + `fs_pattern_drain_scene` | 65 B | Sixteen hidden-pair generation baselines plus current drain selector |
| `fs_list_cache_name` | 9,000 B | Shared typed-Instrument and numbered-library `.hcindex` browser cache |
| `fs_resident_source` | 290 B | Persistent 145-row HCNAMES provenance register; approved filesystem-owned source cache |
| `hcnames_name_mirror` | 1,305 B | Dedicated 145-by-9 HCNAMES name mirror, independent of `fs_list_cache_name` |
| `hcnames_mirror_valid` | 1 B | Session 058 Option 1C tri-state mirror validity gate |
| `op_bank_child_scratch` | 144 B | Shared Session 058 Option 1A 16-by-9 Bank-child display view and zero-growth Session 061 16-by-6 boot-reader Instrument-type view; lifetimes are mutually exclusive |
| `fs_boot_latch` | 6 B linked / 5 B logical | Boot-only Bank-fallback byte plus 16-bit Case-2 and Case-3 masks; replayed after tracking enables, with notice fields cleared by Menu accessors |
| `fs_boot_winner` | 12 B linked / 7 B fields | Stage-10b/11 validated winner identity: valid byte, record index, generation, Bank-match byte |
| Menu boot-notice state | 6 B | Post-audio one-shot Scene mask, Bank flag, active flag, and 16-bit start tick; no payload or filesystem ownership |
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
| Pattern representation | `scenes` is 19,200 B; `pat_regions` is 16 × 10,519 B = 168,304 B; `pat_autosave_snapshot` is 10,519 B. No `PatternSet`, discard sink, or `Step[7][128]` symbol is linked. |
| Slider LUT | `slider_lut` is 4,096 B: 1,024 `float` values, four ADC codes per non-interpolated node. |
| Instrument runtime ownership | Exactly one `runtime_slots` symbol is linked at 7,056 B. No native drum/snare/cymbal/hat object or per-engine expansion pool is linked. |

## 2026-09-08 Session 061 AutoSave reader allocation note

The reader's declared semantic fields total 18 bytes: five bytes in the
filesystem boot latch, seven in the winner record, and six in the Menu notice
sequencer. LTO alignment makes the linked symbols 6, 12, and 6 bytes (24 bytes
of named objects); the measured Session-061 `.bss` increase from the 96,184-B
pre-reader build was 28 bytes after whole-layout alignment. This is boot/notice
control state only, not payload storage. Earlier plans calling the winner
"8 bytes" and the total "19 bytes" were source estimates, not linked sizes.

The originally considered standalone 96-byte Instrument-type snapshot was not
allocated. Both boot readers instead own the 96-byte view of
`op_bank_child_scratch` from complete HCNAMES parse/regeneration through the
last Scene. Canonical Bank Load owns the mutually exclusive 144-byte 16x9 name
view after the reader returns and `filesystem_start()` clears the object. The
9,000-byte list cache remains untouched because canonical fallback still needs
the Bank index. Final type-lifetime comparison against the pre-fix build was
`text -40`, `data 0`, `bss 0`; two six-byte per-Scene local snapshots were also
removed, reducing their aligned stack frames by eight bytes each.
| Transient PCM ROM | `transientData` is 26,460 B at `0x0805b3a0` in FLASH. DTCM `.dtcm` is 8,708 B, down exactly 26,460 B from the preceding image. |

## Verification commands

```sh
make -j2
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
```

For the current Session-062 C/D image, conventional
`arm-none-eabi-size` reports `text=408,220 B`, `data=404 B`, and
`bss=262,468 B`. The latter is the combined zero-init total across memory
regions; `size -A` provides the section split above. Regenerate both
configurations before a future change that alters logging-gated allocations.

## 2026-09-09 Session 062 B/B½ allocation note

The clean ARM link adds the permanent `pat_regions` symbol at exactly 167,936
bytes (`0x29000`): 16 Scene regions of 10,496 bytes each, in normal SRAM1,
owned by `PatternData.c` for the firmware lifetime. Each region contains the
1,792-byte address array, the 8,192-byte `PAT_STACK_SIZE=256` pool reservation,
and the 512-byte full-width free bitmap. Removing `scene_t.pattern` reduces
`scenes` from 20,992 to 19,200 bytes. The disconnected legacy v3 bridge adds a
112-byte `filesystem_pattern_discard` in normal SRAM1, so the measured SRAM1
total is 259,300 bytes and 117,532 bytes remain reserved for future Pattern
expansion. No DTCM or logging-only allocation changed.

## 2026-09-10 Session 062 C/D implementation note

Steps C/D add no retained RAM: the allocator, block reader/writer, menu
specials bridge, and Sequencer probability path are code only. The forced full
ARM rebuild reports `text=408,220 B`, `data=404 B`, `bss=262,468 B`; section
`.text` is 395,288 B and normal SRAM1 `.bss` remains 255,796 B. The linked
`pat_regions` symbol remains 167,936 B (`0x29000`), `scenes` remains 19,200 B,
and `filesystem_pattern_discard` remains 112 B. `make -B -j2`, `make img`, and
`git diff --check` passed. Step E hardware verification passed: note override,
velocity override, probability gating, multi-scene independence, value
persistence across scene switches, and pool reuse after erase/clear.

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
  1 B, `op_bank_child_scratch` 144 B, `text_buf_pos` + `text_buf_len` 4 B,
  `op_bank_cwd_at_parent` 1 B — **1,311 B**, within the approved 1,320-byte
  reservation. The scratch's 144-byte size is unchanged; its alternative
  `boot_reader_type[96]` view is borrowed only by the pre-audio Stage-11
  readers before any normal Bank Load.
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

## 2026-09-18 Session 067 Pattern Stack Service allocation note

`PatternStackService.c` adds 288 bytes of normal SRAM1 `.bss`, owned by
PatternStackService for the firmware lifetime:

- 256 B: 64-entry volatile `uint32_t` SPSC ring buffer for queued mutations.
- ~32 B: service state variables — `service_scene`, `service_open`,
  `replace_pending`, `bulk_op`/`bulk_track`/`bulk_target`/`bulk_cursor`,
  `logical_chunks_used`, `tier1_scan_cursor`, `reactive_compact_requested`,
  `last_compact_tick`, and `handover_phase`.

289 bytes actual vs 282 bytes scheduled; within the +300 approved BSS ceiling
for this session. The +7 byte delta is attributable to alignment and minor
state variable additions not in the original schedule.

No DTCM, DMA, or logging-only allocation changed. No reserved SRAM1 Pattern
capacity or DTCM delay-line capacity was consumed — the service state is
control overhead, not Pattern data storage.

Build metrics post-service: `text=447,644`, `data=412`, `bss=291,140`.
Build metrics post-dtype-fix (final): `text=447,580`, `data=412`,
`bss=291,140`. The dtype fix removed 64 bytes of text (conversion code) and
added no RAM.

## 2026-09-19 Session 068 front-panel event-ring allocation note

`buttonHandler.c` adds 56 bytes of normal SRAM1 `.bss`, owned by
buttonHandler for the firmware lifetime, fixing a silent front-panel
button-event-ring overflow (see `068_SESSION_HANDOFF_LOG.md` §1 and
`PATTERN_DYNAMIC_STACK.md` §5):

- 48 B: `evt_ring[]` capacity expansion from 16 to 64 entries (1 byte per
  entry; the ring changed from a masked head/tail scheme, which reserved one
  slot and left 15 usable, to monotonic producer/consumer counters using all
  64 slots).
- 1 B: `evt_overflow_flag` (unconditional, set by `evt_push()` on a full
  ring, cleared by the next `buttonHandler_processEvents()` drain).
- 1 B: `evt_drop_count` (`DEV_MODE_LOGGING`-only saturating counter).

66 bytes were the approved ceiling (64-byte ring + 2-byte overflow state,
counting the full 64-byte ring rather than only its 48-byte expansion over
the prior 16-entry ring); the measured actual delta against the Session 067
baseline is 56 bytes (291,196 − 291,140), i.e. 6 bytes of alignment/layout
movement beyond the 50-byte source-level estimate (48 + 1 + 1), consistent
with the small over-schedule pattern already seen in the Session 067 note
above. This RAM was explicitly approved as an exception for front-panel
input-integrity correctness — it is not drawn from either general reserved
pool (DTCM delay-line headroom, SRAM1 Pattern-data headroom).

No DTCM, DMA, or Pattern-region allocation changed. The Session 068
chase-light fix (`menu_setPlayedPattern()`, one validated byte assignment, no
new storage) and the track-length sequencer fix (reads an existing
`pat_scene_region_t` field, no new storage) both add zero RAM.

Final Session 068 build: `text=447,860`, `data=412`, `bss=291,196`.
