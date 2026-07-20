# SRAM1 and DTCM memory manifest

This document is generated from the current source tree and a clean, freshly
linked firmware image. It is intentionally limited to SRAM1 and DTCM. No
project specification, previous audit, restored document, or historical log was
used as evidence.

## Evidence and scope

The image was rebuilt with:

```text
make clean && make -j2 && make img
```

The measurements below come from:

- `STM32F765VIHx_FLASH.ld`, for region origins and section placement;
- `arm-none-eabi-size -A build/lxr02.elf`, for linked section sizes;
- `arm-none-eabi-nm -a -S --size-sort build/lxr02.elf`, for linked object sizes;
- current declarations and uses in `Core/`, especially `SceneData`,
  `PatternData`, `SampleMemory`, `filesystem`, `ParameterArray`, and
  `adcPots`;
- a temporary compile-time `sizeof` probe built against the current headers.
  It was deleted after measurement. Results were:

```text
sizeof(Step)                 0x000c = 12 B
sizeof(PatternSet)           0x2a2c = 10,796 B
sizeof(kit_instrument_slot_t) 0x00c1 = 193 B
sizeof(kit_t)                0x052d = 1,325 B
sizeof(scene_settings_t)     0x0028 = 40 B
sizeof(scene_t)              0x2f8c = 12,172 B
sizeof(SampleInfo)           0x000c = 12 B
sizeof(Parameter)            0x0008 = 8 B
```

Only allocations present in the fresh ELF are current linked SRAM usage.
Source declarations that are unreachable and removed by link-time garbage
collection are listed separately as dormant source-path cost; they are not
included in current SRAM totals.

## Physical regions

The linker defines:

| Region | Origin | Capacity | Use in this manifest |
|---|---:|---:|---|
| SRAM1 | `0x20020000` | 0x5c000 = 376,832 B | Ordinary writable data, BSS, DMA section |
| DTCM | `0x20000000` | 0x20000 = 131,072 B | CPU-local DSP data |

The linker places the stack at `0x20080000`. The static SRAM1 calculation below
stops at the end of the SRAM1 linker region, `0x2007c000`; it does not claim
the separate upper 16 KiB region as SRAM1.

## Fresh ELF section totals

| Section | Address | Size | Region | Current meaning |
|---|---:|---:|---|---|
| `.dma_nocache` | `0x20020000` | 3,100 B | SRAM1 | Two audio/DMA buffers plus ADC DMA state; MPU non-cacheable window |
| `.data` | `0x20020c1c` | 408 B | SRAM1 | Initialized writable objects |
| `.bss` | `0x20020db8` | 272,932 B | SRAM1 | Zero-initialized writable objects |
| `.dtcm` | `0x20000000` | 35,168 B | DTCM | Initialized DSP tables/data copied from Flash at boot |
| `.dtcmz` | `0x20008960` | 6,716 B | DTCM | Zero-initialized DSP runtime data |

Current static allocation is:

```text
SRAM1 = 3,100 + 408 + 272,932 = 276,440 B
DTCM  = 35,168 + 6,716       = 41,884 B
```

That leaves 100,392 bytes of unallocated SRAM1 capacity and 89,188 bytes of
unallocated DTCM capacity. These are static-link figures; stack consumption,
interrupt nesting, and compiler-generated automatic variables are runtime
usage and are not included in the section totals.

The ordinary three-column `size` summary reports `bss = 282,748` because it
aggregates no-load data from multiple physical sections. The section report
above is the authoritative SRAM1/DTCM split.

## Pattern and Step storage

### What the current source stores

`Core/Bank/Scene/Pattern/PatternData.h` defines:

```c
Step pat_subStepPattern[7][128];
uint16_t pat_mainSteps[7];
PatternSetting pat_patternSettings;
LengthRotate pat_patternLengthRotate[7];
```

The desired packed active-step storage for the 16 resident Scenes is:

```text
7 tracks * 128 steps * 16 Scenes / 8 = 1,792 bytes
```

That is not the current physical representation. The active bit is bit 7 of
`Step.volume`, so it is logically one bit but physically occupies a byte inside
each Step record. A current Step stores:

| Step field | Size | Meaning |
|---|---:|---|
| `volume` | 1 B | 7-bit volume plus active bit `0x80` |
| `prob` | 1 B | Step probability |
| `note` | 1 B | MIDI note |
| alignment | 1 B | In-memory alignment before 16-bit fields |
| `param1Nr` | 2 B | Automation destination ID 1 |
| `param1Val` | 1 B | Automation value 1 |
| alignment | 1 B | In-memory alignment |
| `param2Nr` | 2 B | Automation destination ID 2 |
| `param2Val` | 1 B | Automation value 2 |
| alignment | 1 B | Struct tail alignment |
| **one `Step`** | **12 B** | |

One Scene has 896 Steps, so its physical Step array is:

```text
7 * 128 * 12 = 10,752 B
```

Its other PatternSet storage is:

| Field | Size per Scene | Meaning |
|---|---:|---|
| `pat_mainSteps[7]` | 14 B | Seven 16-bit main-step masks |
| `pat_patternSettings` | 2 B | `changeBar` and `nextPattern`; retained compatibility fields, inert to Sequencer |
| `pat_patternLengthRotate[7]` | 28 B | Seven records containing length, rotate, scale, shuffle |
| **PatternSet total** | **10,796 B** | |

### One Scene versus the whole resident device

`SCENE_COUNT` is 16 in `SceneData.h`. Therefore:

| Scope | Step arrays | Other PatternSet fields | PatternSet total |
|---|---:|---:|---:|
| One resident Scene | 10,752 B | 44 B | 10,796 B |
| 16 resident Scenes | 172,032 B | 704 B | 172,736 B |
| Packed active-bit target across 16 Scenes | 1,792 B | not included | 1,792 B |

The current resident PatternSet allocation exceeds the packed active-bit target
because the runtime still retains velocity, probability, note, and two
automation lanes for every step. The active bits are embedded in those records;
there is no separate 1,792-byte bitset.

**Phase 4 removal note:** this entire current Step/PatternSet allocation is
temporary bridge storage and is the wrong long-term allocation for the product.
It is intentionally left in place for now. Phase 4 in `SCOPING_TARGETS.md`
will remove this model completely and replace it with the dynamic Pattern
implementation; this manifest must not be used as a design endorsement of the
current 172,736-byte Pattern allocation.

The filesystem also has `op_staged_scene`, a second complete `scene_t` used
while a Scene load is validated before committing it to resident memory. This
adds 12,172 B temporarily, including another 10,796-byte PatternSet. It is not
a seventeenth resident Scene.

There is no separate pattern array in SRAM for each Sequencer pattern number.
The current source routes pattern storage through Scene/PatternData. Remaining
Sequencer objects are indices, timing state, automation-node state, and event
counters; they do not contain the 896-Step arrays.

The binary pattern serializer in `filesystem.c` writes nine meaningful bytes
per Step, omitting the in-memory padding. The draft text pattern writer in
`storageTypes.c` writes seven 128-character active-bit rows plus track timing.
Neither fact changes the current resident SRAM layout: the live `PatternSet`
still contains the full 12-byte Steps.

## Scene, Kit, and parameter storage

### One resident Scene

The current `scene_t` is 12,172 bytes:

| Member | Size | Meaning |
|---|---:|---|
| `scene.display_name` | 9 B | Eight display characters plus NUL |
| `scene.settings` | 40 B | Scene performance, mix, and MIDI settings |
| `scene.pattern` | 10,796 B | One current PatternSet |
| `scene.kit` | 1,325 B | One embedded six-slot Kit |
| alignment | 2 B | Struct alignment around the 16-bit-aligned PatternSet |
| **one `scene_t`** | **12,172 B** | |

`scene_settings_t` stores these bytes:

- `morph_amount`: 1;
- `voice_morph_amount[6]`: 6;
- `voice_decimation_all`: 1;
- `audio_out[6]`: 6;
- `fx_send_amount[6]`: 6;
- `fader_setting[6]`: 6;
- `midi_channel[7]`: 7;
- `midi_note[7]`: 7.

Total: 40 bytes per resident Scene.

### One embedded Kit

The current `kit_t` is 1,325 bytes:

| Member | Size | Meaning |
|---|---:|---|
| `kit.settings` | 2 B | Two generated slot-6/track-7 decay endpoint values |
| `kit.instruments[6]` | 1,158 B | Six slots at 193 B each |
| `kit.display_name` | 9 B | Kit display name plus NUL |
| `kit.instrument_display_name[6][9]` | 54 B | Six retained eight-character source names plus NUL |
| `kit.instrument_stem[6][17]` | 102 B | Six retained 16-character source stems plus NUL |
| **one `kit_t`** | **1,325 B** | |

One `kit_instrument_slot_t` is 193 bytes:

- one instrument type byte;
- 64 normal instrument parameter bytes;
- 64 Morph endpoint parameter bytes;
- 64 runtime Morph interpolation bytes.

The six slots therefore contain 384 normal endpoint bytes, 384 Morph endpoint
bytes, and 384 runtime interpolation bytes. The interpolation image is a live
runtime result; it is not a third endpoint stored in instrument files.

### Entire resident device

The 16 resident Scenes occupy:

| Resident object | Device-wide size |
|---|---:|
| `scenes[16]` complete records | **194,752 B** |
| embedded Kits, already included above | 21,200 B |
| normal instrument endpoint images, already included | 6,144 B |
| Morph endpoint images, already included | 6,144 B |
| runtime interpolation images, already included | 6,144 B |
| Scene settings, already included | 640 B |
| PatternSets, already included | 172,736 B |

The rows below the first are ownership breakdowns, not additional allocations.
They must not be added to 194,752 B again.

A Bank does not allocate another resident 16-Scene array. Bank-local Scene
payloads are loaded through the same resident Scene workspace and the one
`op_staged_scene` load buffer.

## Parameter inventory

### Canonical instrument parameter images

`InstrumentManager.h` defines six instrument slots and 64 descriptor positions
per slot. `SceneData.h` stores three byte images per slot:

| Scope | Normal endpoint | Morph endpoint | Runtime interpolation | Total |
|---|---:|---:|---:|---:|
| One instrument slot | 64 B | 64 B | 64 B | 192 B |
| One Scene, six slots | 384 B | 384 B | 384 B | 1,152 B |
| 16 resident Scenes | 6,144 B | 6,144 B | 6,144 B | 18,432 B |

These are descriptor-indexed values. The selected instrument type determines
which descriptor table gives each byte its meaning. Instrument file `[params]`
and `[morph]` data populate the two endpoint images. The runtime interpolation
image is generated from them and is not persistent instrument-file metadata.

### Scene and Kit parameter/settings bytes

These are stored separately from the generic `parameter_values` bridge:

| Owner | Stored values | Size per owner |
|---|---|---:|
| Kit | Two generated decay endpoint bytes | 2 B |
| Kit | Six instrument type selectors | 6 B included in six 193-B slots |
| Kit | Six normal endpoint images | 384 B |
| Kit | Six Morph endpoint images | 384 B |
| Scene | Global Morph and six per-voice Morph values | 7 B |
| Scene | Scene-wide decimation | 1 B |
| Scene | Six audio routes, six FX sends, six fader modes | 18 B |
| Scene | Seven MIDI channels and seven MIDI notes | 14 B |
| Scene | Pattern fields | 10,796 B |

The 40-byte Scene settings total is 7 + 1 + 18 + 14. The 1,152-byte Kit
parameter-image total is contained inside the 1,325-byte Kit.

### Legacy `ParameterArray` objects

The current source sets `END_OF_SOUND_PARAMETERS` to 1 and defines:

```c
Parameter parameterArray[END_OF_SOUND_PARAMETERS];
```

The current target `Parameter` is an aligned pointer plus a type byte, giving
8 bytes. The fresh ELF contains:

| Object | Fresh ELF size | Region | Actual role |
|---|---:|---|---|
| `parameterArray` | 8 B | SRAM1 `.bss` | One legacy pointer/type dispatch entry |
| `parameters2` | 1 B | SRAM1 `.bss` | One legacy Morph overlay byte |
| `paramToModTarget` | 1 B | SRAM1 `.bss` | One legacy reverse mapping byte |
| `parameter_values[384]` | 384 B | SRAM1 `.bss` | Flat UI, performance, Pattern, and global byte bridge |
| `parameter_dtypes[384]` | 384 B | Flash `.text` | Constant menu datatype table, not SRAM |

`parameterArray` is not parameter metadata. It is a legacy runtime dispatch
table, and in this build it contains only the `PAR_NONE` namespace entry.
There is no 1,824-byte parameter-metadata allocation in the current binary.
Instrument values are held in Scene-owned byte images and are serialized by the
instrument/Kit file writers; the generic array is not their canonical storage.

### Every generic parameter group

The 384-byte `parameter_values` array is byte-indexed. The current enum groups
are:

- `PAR_NONE` / `PAR_MOD_WHEEL`;
- Roll and overall Morph;
- active Step, Step volume, Step probability, and Step note;
- Euclidean length, steps, and rotation;
- automation track, P1/P2 destinations, and P1/P2 values;
- shuffle, Pattern beat, Pattern next, and track length;
- SOM X, SOM Y, flux, and SOM frequency;
- track rotation, track scale, track MIDI channel, and track MIDI note;
- global BPM;
- MIDI channels 1 through 6;
- external sync and Follow;
- quantisation and screensaver on/off;
- MIDI mode, MIDI channel 7, MIDI routing, TX filter, and RX filter;
- input/output clock prescalers;
- trigger/gate mode;
- bar-reset mode, global MIDI channel, and oscillator waveform interpolation;
- six Scene per-voice Morph values;
- Scene-wide voice decimation.

These bytes are a transport/compatibility array. Scene settings and instrument
endpoint images are owned by their Scene/Kit structures, not duplicated here.

## Sample metadata and caches

The source contains three different sample-related lifetimes. They overlap in
field shape but not in purpose.

### `sample_info_cache`

`SampleInfo` is 12 bytes and `SAMPLE_MAX_COUNT` is 120. The runtime cache is:

```text
sample_info_cache[120] = 120 * 12 = 1,440 B SRAM1
```

It is refreshed from the persistent sample-flash `SampleInfo` table and is read
by `sampleMemory_getSampleInfo()` and the sample playback path. It holds sample
offsets, sizes, and the packed loop flag after validation. It is necessary in
the current code because the audio path and accessors use a validated SRAM
copy instead of repeatedly reading the flash metadata table.

Other linked runtime sample state is:

| Object | Size | Purpose |
|---|---:|---|
| `sample_info_cache[120]` | 1,440 B | Validated sample offset/size/name records |
| `sample_name_cache[120][9]` | 1,080 B | Menu display names with NUL terminator |
| `sample_loop_cache[120]` | 120 B | Extracted loop flag for playback |
| `sample_count` | 1 B | Number of valid samples |
| `sample_generation` | 4 B | Invalidates DSP cached metadata after refresh/install |
| **runtime sample subtotal** | **2,645 B** | |

The persistent flash table and flash display-name table are not SRAM1 or DTCM
and are not included in the memory totals.

### `sample_manifest`

`filesystem.c` defines a source-only SD-import record:

```text
filename[13]
sort_name[80]
name[3]
display_name[8]
data_offset[4]
data_bytes[4]
```

Its padded size is 112 bytes, so `sample_manifest[120]` would cost 13,440
bytes. It is used only by the modal WAV importer to scan the SD directory,
parse headers, sort long names, reject files that do not fit, and then stream
the selected files into sample flash. It is not the persistent flash table and
not a playback cache.

The importer also declares `sample_io_buf[516]` (512-byte transfer buffer plus
margin) and a one-byte manifest count. The fresh ELF contains none of
`sample_manifest`, `sample_io_buf`, or the installer symbols: the importer is
currently dead-stripped from this linked image. The 13,957-byte catalog/buffer
figure is therefore dormant source-path cost, not current SRAM usage.

### `install_info`

`SampleMemory.c` declares:

```text
install_info[120]  = 1,440 B
install_names[120][8] = 960 B
```

These are write-side staging arrays for sample installation. `install_info`
starts from the current runtime sample table, accepts appended/replaced sample
records, packs the loop flag back into the size field, and commits the new
metadata table at the end. `install_names` does the same for the persistent
eight-character display-name table.

They are not redundant with `sample_info_cache`: the cache is the live
read/playback view, while `install_info` is a write transaction image that lets
the installer preserve existing samples while constructing a new table. They
could be unified only by changing the installer lifetime and commit logic.

The fresh ELF contains neither installer array because the installer call path
is not reachable in this build. If linked, the source installer arrays would
consume 2,400 bytes, plus scalar installer state.

## Stored names and list caches

This section separates resident names, browser/list caches, and one-operation
name buffers. It does not count constant UI label tables in Flash.

### Resident names

| Owner | Field | Size |
|---|---|---:|
| One Scene | `scene.display_name` | 9 B |
| One embedded Kit | `kit.display_name` | 9 B |
| One embedded Kit | `instrument_display_name[6][9]` | 54 B |
| One embedded Kit | `instrument_stem[6][17]` | 102 B |
| **Names inside one `scene_t`** | above four fields | **174 B** |
| **16 resident Scenes** | above four fields | **2,784 B** |
| Bank workspace | `bank_display_name` | 9 B |
| Save editor | `preset_currentName` | 8 B |

Additional linked UI/error text storage is separate from resident object names:

| Owner | Field | Size | Role |
|---|---|---:|---|
| Filesystem | `fs_error_code` | 9 B | Current filesystem error display |
| Menu | `menu_testEditName` | 49 B | Diagnostic test-name editor |
| Menu | `menu_testResultName` | 49 B | Diagnostic result name |
| Menu | `menu_instrumentSaveName` | 9 B | Instrument Save editor |
| Menu | `currentDisplayBuffer[2][16]` | 32 B | LCD text buffer |
| Menu | `editDisplayBuffer[2][17]` | 34 B | LCD edit/display buffer |

`op_staged_scene` contains another 174 bytes of resident-style names and
`op_staged_kit` contains 165 bytes of Kit/instrument names. Those bytes are
already part of the linked 12,172-byte and 1,325-byte staging symbols,
respectively; they must not be added a second time.

The 174 bytes per resident Scene are already included in the 12,172-byte
`scene_t`. The 2,784-byte 16-Scene total is already included in `scenes`.

### The one shared browser/list cache

The current source declares exactly one physical name array:

```text
fs_list_cache_name[1000][9] = 9,000 B SRAM1
fs_list_cache_kind          = 1 B
fs_list_cache_type          = 1 B
fs_list_cache_count         = 2 B
```

Total current linked allocation for this cache and its control fields is 9,004
bytes. Instrument, Kit, root Scene, and root Bank operations reuse this same
array. Kit/Scene/Bank rows are direct slot rows; Instrument rows are sorted
rows. The cache is disposed when the active domain changes, so there is no
per-instrument or per-library duplicate.

The fresh ELF also contains `kb_map[1000]` at 2,000 bytes and `kb_numKits` at
2 bytes. These are a legacy KitBrowser slot-number map, not a second name
cache. `kb_kitName` and `kb_mapIndex` exist in source but are dead-stripped from
the fresh ELF; they are not current SRAM allocations. The linked KitBrowser
state also has two one-byte flags (`kb_dirty`, `kb_name_pending`).

The source-only sample installer has `install_names[120][8]` (960 bytes), and
the source-only SD importer has name fields inside each 112-byte
`sample_manifest` record. Neither is present in the fresh ELF.

### Current linked operation name buffers

These are bounded state-machine scratch buffers, not 1,000-entry caches. Sizes
below include the terminating byte where the declaration does.

| Linked object/group | Size | Use |
|---|---:|---|
| `loaded_name` | 9 B | Async load-name result |
| `op_root_open_name`, `op_scene_root_open_name` | 26 B | One short FAT alias each |
| `op_lfn_name` | 80 B | One LFN accumulator |
| `op_line_buf`, `op_write_line_buf` | 320 B | Streamed text lines |
| Kit Save display + six member filenames + aliases | 49 + 294 + 13 = 356 B | One in-flight Kit Save |
| embedded Scene Kit display + alias | 49 + 13 = 62 B | One Scene payload operation |
| delete-tree name stacks and child names | 558 B | Fallback recursive delete walker |
| Scene display, child display, and three aliases | 9 + 9 + 39 = 57 B | One Scene load/save operation |
| Bank display, save names, aliases | 9 + 147 + 26 = 182 B | One Bank save/load operation |
| Bank-local child names and aliases | 144 + 208 = 352 B | Up to 16 child Scenes in selected Bank |
| Instrument Save display + alias | 49 + 13 = 62 B | One Instrument Save |
| staged Instrument display + stem | 9 + 17 = 26 B | One Instrument Load |
| `fs_test_file_name[64][49]` | 3,136 B | Diagnostic root-file list cache |
| `fs_test_dir_name[64][49]` | 3,136 B | Diagnostic root-directory list cache |
| test operation names and aliases | 49 + 49 + 13 + 13 = 124 B | Diagnostic operation scratch |

The linked ELF confirms the largest of these directly: `fs_test_file_name` and
`fs_test_dir_name` are each 3,136 bytes, and the shared name array is 9,000
bytes. Automatic local arrays used while parsing one line or one directory are
stack usage, not static SRAM symbols, and are not silently added to the above
static objects.

## Slider LUT

`Core/Hardware/frontPanel/IO/adcPots.c` declares:

```text
slider_lut[4096]  = 16,384 B SRAM1  (4096 floats)
slider_vol[6]     = 24 B SRAM1     (six current float values)
```

The ADC input remains 12-bit, 0..4095. The code clamps a 50-code deadzone at
each end, leaving raw codes 50..4045 inclusive, a 3,995-code span. The LUT
stores the configured logarithmic transfer curve so the foreground loop does
not call `powf()` for every slider sample.

A 2,048-entry float LUT would be 8,192 bytes and free 8,192 SRAM1 bytes. It
would not change the ADC hardware resolution, `slider_vol` float precision,
the deadzone, the logarithmic curve, or audio-block interpolation. It would
change only lookup quantization if the input mapping were changed accordingly.

With a simple `raw >> 1` index, each LUT value covers two raw ADC codes and the
usable 50..4045 range occupies 1,998 distinct half-resolution bins. It is not
exactly 2,048 usable positions because the deadzone is applied before the
usable range is considered. A deadzone-aware mapping could spread the usable
range over all 2,048 entries instead. Either implementation may increase
gain stair-stepping; it does not alter the nominal ADC or float output
resolution.

## SRAM1 detailed allocation

The section total is authoritative. The following linked symbols identify the
largest and semantically important allocations within it:

| Linked object | Size | SRAM1 role |
|---|---:|---|
| `scenes[16]` | 194,752 B | Sixteen complete resident Scene records |
| `slider_lut` | 16,384 B | Full 12-bit slider transfer LUT |
| `op_staged_scene` | 12,172 B | One temporary Scene payload |
| `fs_list_cache_name` | 9,000 B | One shared 1,000-row name cache |
| `afatfs` | 6,688 B | Async FAT filesystem state |
| `fs_test_file_name` + `fs_test_dir_name` | 6,272 B | Diagnostic file/dir list caches |
| `kb_map` | 2,000 B | Legacy KitBrowser slot map; not names |
| `usb_MidiMessages` | 2,048 B | USB MIDI message storage |
| runtime instrument groups | 10,044 B | Drum, Snare, Cymbal, and HiHat runtime state |
| sample runtime caches | 2,645 B | Sample info/name/loop/count/generation |
| `USB_OTG_dev` | 1,524 B | USB runtime state |
| `op_staged_kit` | 1,325 B | One temporary Kit payload |
| `parameter_values` | 384 B | Legacy generic parameter byte bridge |
| `staging_buf` | 512 B | Filesystem stream staging |

The runtime instrument groups are DSP state, not duplicates of the
Scene-owned instrument parameter images. The Scene image is the retained
configuration; these groups are the applied audio-engine state.

The 3,100-byte `.dma_nocache` section contains two 1,536-byte DMA buffers and
28 bytes of ADC DMA state. It is physically in SRAM1 but kept in its own MPU
non-cacheable section.

The current `.data` section is only 408 bytes. It includes initialized values
such as the cache type tag and sample generation; `.bss` contains the large
zero-initialized arrays listed above.

## DTCM detailed allocation

### Initialized `.dtcm`

| Linked object | Size | Role |
|---|---:|---|
| `squareRootLut` | 512 B | DSP lookup table |
| `transientData` | 26,460 B | DSP transient/read-only table |
| `sine_table` | 8,194 B | DSP waveform table |
| alignment | 2 B | Section padding |
| **`.dtcm` total** | **35,168 B** | |

### Zero-initialized `.dtcmz`

| Linked object/group | Size | Role |
|---|---:|---|
| `audioOutBuffer` | 1,536 B | Audio output buffer |
| `audioOutBuffer2` | 1,536 B | Second audio output buffer |
| `voiceArray` | 1,764 B | Voice runtime state |
| `hatVoice` | 484 B | Hi-hat runtime state |
| `snareVoice` | 420 B | Snare runtime state |
| `cymbalVoice` | 476 B | Cymbal runtime state |
| `velocityModulators` | 264 B | Velocity modulation state |
| `osc_interp_a`, `osc_interp_b` | 128 B | Oscillator interpolation state |
| mixer/oscillator control fields and alignment | 108 B | Decimation, routing, gain, interpolation-generation state |
| **`.dtcmz` total** | **6,716 B** | |

Pattern storage, Scene/Kits, names, slider LUT, sample caches, FATFS state, and
generic parameter arrays are all in SRAM1. They do not consume DTCM.

## Direct answers

- The expected 1,792-byte packed active-step total is not the current SRAM
  representation. Current resident Step arrays occupy 172,032 bytes because
  each Step also retains volume, probability, note, and two automation lanes;
  complete resident PatternSets occupy 172,736 bytes.
- `sample_info_cache` is the live validated playback cache. `sample_manifest`
  is an SD WAV-import catalog. `install_info` is a write-side flash-table
  staging array. Their similar fields do not make them interchangeable. Only
  `sample_info_cache` and the display/loop caches are linked in current SRAM;
  the importer and installer arrays are dormant source-path allocations.
- `parameterArray` is an 8-byte legacy pointer/type dispatch table, not
  metadata. The current binary has no 1,824-byte parameter-metadata object.
- Halving `slider_lut` frees 8,192 bytes and reduces software transfer-curve
  granularity, with possible stair-stepping and deadzone-mapping changes. It
  does not change ADC hardware resolution or DTCM use.
- There is one 9,000-byte general name cache. `kb_map` is a 2,000-byte legacy
  slot map, not another name cache. Resident names, sample names, diagnostics,
  and one-operation aliases are listed separately above.
