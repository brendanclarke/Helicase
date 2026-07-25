# Transient sample table in internal flash

## Decision requested

Move the immutable `transientData` PCM table out of DTCM and let the ordinary
read-only-data linker rule place it in the firmware's internal FLASH region.
This is a placement-only migration: its element type, dimensions, sample
values, waveform numbering, playback arithmetic, and public symbol remain
unchanged. It does not create an SRAM cache, a runtime copy, or a new RAM
allocation.

The table is currently 12 x 2,205 signed 8-bit samples:

```
NUM_TRANSIENTS * TRANSIENT_SAMPLE_LENGTH * sizeof(int8_t)
= 12 * 2205 * 1
= 26,460 bytes (0x675c)
```

The current linked image locates it at `0x20000200` in DTCM `.dtcm`. DTCM is
therefore used both for the table's 26,460-byte runtime copy and for the
startup copy from its FLASH load image. Removing the DTCM placement attribute
will put the same immutable bytes in `.text`/`.rodata` at a `0x080...` FLASH
address and eliminate that startup copy. It should release exactly 26,460 B
of DTCM, subject only to normal linker alignment.

The released DTCM is reserved exclusively for future delay-line buffers; it
is **not** available for incidental features or Pattern data. Free normal
SRAM1 is reserved exclusively for future Pattern data. Any future new or
enlarged RAM allocation requires an explicit byte/region/owner callout and
user acknowledgement before implementation; the durable project policy is in
`MEMORY.md`.

## Current linked-state baseline

These values are measured from `build/lxr02.elf` after the 1,024-entry slider
LUT and tagged instrument-slot work.

| Item | Current value | Consequence of this migration |
| --- | ---: | --- |
| `transientData` | 26,460 B, DTCM `.dtcm`, `0x20000200` | Moves to ordinary read-only FLASH |
| `.dtcm` | 35,168 B | Expected about 8,708 B |
| `.dtcmz` | 3,572 B | No change |
| DTCM static total | 38,740 B / 131,072 B | Expected about 12,280 B; about 118,792 B reserved for future delay lines |
| firmware `.text` section | 312,352 B | Contains the table after the move; measure, do not estimate as a new 26,460-B image cost |
| app FLASH window | 491,520 B (`0x08008000..0x0807ffff`) | Linker assertion remains the capacity authority |

The table already has a FLASH load image today because `.dtcm` is declared
`AT> FLASH`. Consequently, moving it normally changes FLASH *placement* and
removes the boot copy; it is not expected to duplicate the table in firmware
FLASH. The post-change ELF and image size must verify the exact result.

The current map makes that expectation concrete: `.dtcm` is loaded from
`0x08055634`, is `0x8960` bytes, and ends at `0x0805df94`; `transientData`
occupies `0x675c` of that load range. The firmware binary is currently
352,148 B. Default placement should move those same bytes into the FLASH
`.text` output section, shift subsequent FLASH load addresses, and leave the
final load-image end and binary size unchanged except for any documented linker
padding. This is a measurement target, not an assumption.

## Real-code audit

### Definition and placement

- `Core/DSPAudio/transientTables.c` defines `transientData` as
  `INCCM const int8_t transientData[NUM_TRANSIENTS][TRANSIENT_SAMPLE_LENGTH]`.
  `INCCM` is a compatibility alias for `INDTCM`, which applies
  `__attribute__((section(".dtcm")))` in `config.h`. This single attribute is
  the direct cause of the DTCM allocation.
- The same file defines `transientVolumeTable` with `INCCM`, but it is only
  69 `float`s (276 B) and has no live linked consumer. It is not included in
  this table move.
- `Core/DSPAudio/transientTables.h` supplies the public dimensions and the
  `extern const int8_t` declaration. It has no placement attribute, so clients
  already express the correct immutable interface.
- The actual LTO-linked symbol is local (`r`) despite the source declaration
  being `extern`, because every use is within this firmware link. Its current
  address is `0x20000200`, immediately after the 512-B `squareRootLut`; the
  following 8,194-B `sine_table` begins at `0x2000695c`. Neither placement nor
  ordering is a source-level API, so both may change after the move.

### Link and boot path

- `config.h` defines `INDTCM`, `INDTCMZ`, `INCCM`, and `INCCMZ`; only the
  `INCCM` on this one definition must be removed. The macros remain necessary
  for real DTCM DSP state and must not be changed globally.
- `STM32F765VIHx_FLASH.ld` collects `.rodata` in `.text` in FLASH. It maps
  `.dtcm` to DTCM with `AT> FLASH`; thus every `.dtcm` initializer has a FLASH
  load address. No linker-script edit is expected: an ordinary `const` object
  already follows the required default FLASH rule.
- `Core/Src/startup_stm32f765xx.s` copies the entire `.dtcm` load image to
  DTCM at reset. It needs no code change. Its copied range will shrink by the
  table's final aligned size automatically.
- `Core/Hardware/clocks.c` enables ART/prefetch, I-cache, and the 16-KB M7
  D-cache. DTCM bypasses the cache, whereas the new FLASH reads use the normal
  flash/cache path. This is the performance boundary that requires hardware
  validation; it does not require a cache-policy change.
- `Makefile` compiles DSP sources with `-Ofast -flto -fdata-sections
  -ffunction-sections` and links with `--gc-sections`. This is why the unused
  scalar renderer and its volume table have no linked symbols, and why the
  final ELF/map—not an individual object file—is the placement authority.

### Runtime readers and parameter writers

`transient_calcBlock` in `Core/DSPAudio/transientGenerator.c` is the live
renderer. It selects `transientData[waveform - 2][phase]` for waveform values
2 through 13, reads one signed byte per output sample, scales it by volume,
and writes the supplied `int16_t` block. `transient_setWaveform` bounds the
accepted selector to `< NUM_TRANSIENTS + 2`; values 0 and 1 clear the output
block. They are nevertheless musically active modes outside this renderer:
all four engines use 0 for snap-envelope pitch modulation, and use 1 for the
trigger-time oscillator-offset behavior. Moving the backing bytes must not
change either special-mode behavior or the 2..13 PCM-row contract.

The live audio path invokes `mixer_calcNextSampleBlock` in 32-frame
`OUTPUT_DMA_SIZE` sub-blocks (0.73 ms at the configured 44,108 Hz). At the
maximum six tagged runtime slots with PCM selectors active, this migration
changes at most 6 x 32 = 192 signed-byte sample reads per sub-block from DTCM
to FLASH. No call site copies the table or retains a table pointer.

The four live engine call sites are:

| File | Consumer buffer | Why it is an affiliate, not a placement edit |
| --- | --- | --- |
| `Core/DSP/Instruments/Drum/DrumVoice.c` | `modBuf` | Calls the renderer for a Drum slot. |
| `Core/DSP/Instruments/Snare/Snare.c` | `transBuf` | Calls the renderer for a Snare slot. |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | `mod` | Calls the renderer for a Cymbal slot. |
| `Core/DSP/Instruments/HiHat/HiHat.c` | `mod1` | Calls the renderer for a Hi-hat slot. |

Each engine initializes and triggers its embedded `TransientGenerator`; none
holds a pointer to, copies, or mutates the table. `InstrumentManager.c` writes
the waveform and pitch parameters through the generator, while the four
parameter descriptor files expose `transient_wave`, `transient_vol`, and
`transient_freq`. The live MIDI path resolves those same descriptor keys.
These files need no source edit for a storage-only migration, but they define
the full set of user-visible paths to exercise in validation.

`Core/MIDI/MidiParser.c` still contains direct fixed-engine transient cases
inside `#if 0`; they are archived, noncompiled compatibility text and are not
part of the migration.

### Suspicious legacy path: separate from the placement change

`transient_calc` remains declared and defined in `transientGenerator.h/.c`,
but no source call site exists and it has no symbol in the current ELF. The
linked image also contains no `transientVolumeTable`. The linker has already
discarded both as unreachable.

This scalar path differs from the live block renderer: it indexes
`transientData[waveform - 1]`, whereas the live path uses `waveform - 2` and
returns an all-zero transient buffer for selectors 0 and 1. If it were ever
revived, its table selection could be inconsistent or out of range. It should
not be silently repaired under a memory-placement commit. After the FLASH move
is validated, make a separate, reviewable cleanup decision to remove the dead
declaration, function, and envelope-table definition, or to specify and test
a supported scalar contract before retaining them.

## Exact implementation plan (no firmware changes in this planning pass)

The source audit produces exactly two firmware source edits. They are paired
definition/interface documentation changes: no renderer, engine, parameter,
MIDI, linker, startup, clock, or build-rule change is expected. The comments
below are the required comment-text descriptions to land with the code, so the
storage decision is documented in both the `.c` definition and `.h` interface.

### 1. Move the sole PCM definition to default read-only FLASH

**File and exact site:** `Core/DSPAudio/transientTables.c`, the definition
currently beginning `INCCM const int8_t transientData[...]` immediately after
the dead-stripped `transientVolumeTable` definition.

**Required code change:** Delete only the `INCCM` token from this one
definition, producing:

```c
const int8_t transientData[NUM_TRANSIENTS][TRANSIENT_SAMPLE_LENGTH] =
```

Do not alter the initializer, its `int8_t` element type, either dimension
macro, global source-level linkage, row order, or the `INCCM` on
`transientVolumeTable`. Retain `#include "config.h"`, because that smaller
table still uses `INCCM` in source even though it currently dead-strips.

**Required `.c` comment text:** Place this directly above the definition:

```c
/* Immutable transient PCM ROM: ordinary const placement keeps this
** NUM_TRANSIENTS x TRANSIENT_SAMPLE_LENGTH table in internal FLASH.
** transient_calcBlock() reads it directly; do not add an SRAM/DTCM shadow.
** The DTCM capacity released by this placement is reserved for delay lines. */
```

**What it does and why it must exist:** `INCCM` expands through `config.h` to
the `.dtcm` section attribute. That override is the only cause of this
otherwise immutable table being allocated at DTCM VMA `0x20000200` and copied
there at reset. With no explicit section attribute, the GCC `const` object is
emitted as ordinary read-only data; the existing linker script collects
`.rodata` and `.rodata.*` into its FLASH `.text` output section. This is the
minimal change that removes the 26,460-B DTCM allocation and startup copy
without introducing a second table or a RAM allocation.

**Inputs:** The existing 26,460 initializer bytes; `NUM_TRANSIENTS == 12`;
`TRANSIENT_SAMPLE_LENGTH == 2205`; the current compiler's ordinary `const`
section; and the linker's existing FLASH `.rodata` collection rule.

**Outputs:** Exactly the same two-dimensional immutable table, addressed from
the internal application FLASH range (`0x080...`), read by the same generated
renderer operations. `.dtcm` loses the table's aligned 26,460 B; the reset
loop automatically copies fewer words because its end symbol is linker-derived.

**Code affiliates:** `transientTables.h` declares this object; the linker
provides default `.rodata` placement; startup consumes `_sidtcm` through
`_edtcm`; `transient_calcBlock` reads the rows; the Drum, Snare, Cymbal, and
Hi-hat block renderers supply its output buffers. No affiliate's call signature
or data layout changes.

### 2. Make the public header state the permanent storage/selector contract

**File and exact site:** `Core/DSPAudio/transientTables.h`, replace only the
two-line stale PCM comment directly above the existing `extern const int8_t
transientData[...]` declaration.

**Required code change:** Preserve the declaration verbatim. Replace its
comment with the following interface documentation:

```c
/* Immutable firmware PCM ROM: NUM_TRANSIENTS signed 8-bit waveforms, each
** TRANSIENT_SAMPLE_LENGTH frames. The definition uses default const placement
** in internal FLASH; clients may read it but must not create a RAM shadow.
** transientGenerator maps selectors 2..(NUM_TRANSIENTS + 1) to rows 0..11;
** selectors 0 and 1 are generator special modes, not PCM rows. */
```

**What it does and why it must exist:** Every live renderer reaches the table
through `transientGenerator.h`, which includes this header. The definition-side
comment alone would not make the no-shadow, FLASH-resident contract visible at
the interface. Keeping the `extern const int8_t [12][2205]` declaration
unchanged preserves all client type checking, table indexing, and link-time
identity; the comment prevents a future caller from treating freed DTCM as a
license to reintroduce a copy.

**Inputs:** Existing dimensions, `transient_setWaveform`'s selector range, and
the four engines' existing `TransientGenerator` members.

**Outputs:** Documentation only—no generated-code, ABI, RAM, or FLASH-size
change beyond the defining object's relocated contents.

**Code affiliates:** The `.c` definition above; `transientGenerator.h/.c`;
Drum/Snare/Cymbal/Hi-hat renderers; the four descriptor tables; Instrument
Manager's special waveform writer; and the active MIDI descriptor-key mapper.

### 3. Explicitly leave the audited affiliates unchanged

The following files are inspected implementation boundaries, but must not be
edited for this migration. Listing them prevents accidental scope growth.

| File(s) | Current responsibility | Why no change is correct |
| --- | --- | --- |
| `config.h` | Defines `INDTCM`/`INCCM` for all real DTCM data. | Removing or redefining the macro globally would relocate unrelated DSP state. Only the one table must stop opting in. |
| `STM32F765VIHx_FLASH.ld` | Places ordinary `.rodata` in FLASH; maps `.dtcm` `AT> FLASH`; asserts both FLASH bounds. | Its existing rules implement the target placement and automatically shrink the DTCM copy image. A new custom FLASH section would add unnecessary policy and risk. |
| `Core/Src/startup_stm32f765xx.s` | Copies `[ _sidtcm, _edtcm )` at reset, then zeroes `.dtcmz`. | Both boundaries come from the linker; the loop correctly shrinks without code changes. |
| `Core/Hardware/clocks.c` | Enables ART/prefetch, I-cache, and D-cache. | The table stays immutable and has no DMA producer/consumer. Cache/MPU behavior must be tested, not redesigned or given a new RAM buffer. |
| `Makefile` | Builds DSP sources with `-Ofast`, LTO, data/function sections and links with GC. | These existing flags yield the authoritative final placement; no compiler-option change is necessary or justified. |
| `Core/DSPAudio/transientGenerator.c/.h` | Bounds selectors, resets phase, and generates each block from `transientData`. | Preserve the `waveform - 2` row mapping, phase arithmetic, multiplication order, and API exactly; changing them is an audio-behavior change, not storage migration. |
| `Core/DSP/Instruments/{Drum,Snare,Cymbal,HiHat}/*` | Initializes/triggers a generator and mixes its 32-frame block into engine-local scratch. | The table has no per-engine storage or pointer ownership. Existing special mode 0 (snap) and 1 (offset) behavior remains valid. |
| `InstrumentManager.c`, four `*Parameters.c`, `MidiParser.c` | Writes/declares `transient_wave`, `transient_vol`, and `transient_freq`. | Parameters continue to alter only generator state. The old direct MIDI switch is under `#if 0` and has no compiled effect. |

### 4. Record the exact linked result after the source edit

**Files:** `TRANSIENTS_IN_FLASH.md` and `SRAM_MANIFEST.md`. Record the linked
result immediately after a successful firmware build; append the target-audio
outcome when hardware validation is available.

**Required documentation changes:** Add an execution note to this plan with
the actual post-build symbol address, `.dtcm`/`.dtcmz` sizes, final FLASH load
end, binary size, and hardware-test outcome. Regenerate `SRAM_MANIFEST.md`
from that new ELF, not by arithmetic. It must label the newly unoccupied DTCM
capacity as **reserved for future delay-line buffers**. It must continue to
label free normal SRAM1 as **reserved for Pattern data**.

**Why it must exist:** The source edit's purpose is memory placement. A fresh
linked manifest is the only durable proof that the linker, LTO, and section
alignment delivered the intended result and that no unrelated RAM owner grew.

**Inputs:** The successful post-change `build/lxr02.elf`, map file, binary,
and target-audio result.

**Outputs:** A current storage manifest and an auditable implementation record;
neither output allocates RAM or changes firmware behavior.

**Affiliates:** `Makefile`, linker script, startup copy loop, the two source
files changed above, and the project RAM-allocation policy in `MEMORY.md`.

## Execution record

### 2026-07-25 — FLASH-ROM placement implemented

- Applied the two planned firmware edits only:
  `Core/DSPAudio/transientTables.c` removes `INCCM` solely from
  `transientData`, with the adjacent definition-side FLASH/no-shadow/delay-line
  reservation comment; `Core/DSPAudio/transientTables.h` retains its declaration
  and adds the adjacent public FLASH/selector/no-shadow contract comment.
  `transientVolumeTable`, the initializer bytes, dimensions, row order,
  renderer arithmetic, engine callers, linker script, startup copy loop,
  clock/cache setup, and build flags were not changed.
- `make -j2` succeeded. The only diagnostics are the established newlib-nano
  `_close`, `_lseek`, `_read`, and `_write` syscall-stub warnings plus the LTO
  serial-compilation notice; no new compiler or linker error/warning resulted
  from the placement change.
- `transientData` now links as `t` at `0x08053264`, size `0x675c` / 26,460 B.
  The lower-case text class is expected because this linker script merges
  ordinary read-only data into the executable `.text` output section under
  LTO; its FLASH address and unchanged size are the placement invariants.
- `.dtcm` is exactly `0x2204` / 8,708 B (was `0x8960` / 35,168 B), an exact
  26,460-B reduction. `.dtcmz` remains `0x0df4` / 3,572 B. DTCM static use is
  now 12,280 B, leaving 118,792 B reserved exclusively for future delay-line
  buffers.
- The DTCM load segment now starts at `0x0805bd8c` and ends at
  `_eflash_load = 0x0805df90`, still below the `0x08080000` sample-FLASH
  boundary. `transientData` is absent from that segment and the reset copy
  therefore excludes it.
- The firmware binary is 352,144 B, four bytes smaller than the 352,148-B
  baseline; this confirms no duplicate FLASH copy. `.text` is 338,808 B, and
  the four-byte total reduction is ordinary section-layout alignment.
- Regenerated `SRAM_MANIFEST.md` from the new ELF. It records the table in
  FLASH, the new DTCM layout, and the separate DTCM delay-line/SRAM1 Pattern
  reservations.
- Target-audio validation remains pending physical hardware. It must complete
  the test matrix below before the migration is accepted as sonically verified.

## Acceptance and rollback criteria

### Build and linked-image proof

Run these commands after the two source edits:

```sh
make -j2
arm-none-eabi-nm -S --size-sort build/lxr02.elf
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-readelf -l -W build/lxr02.elf
rg -n -C 4 'transientData|^\.dtcm|_sidtcm|_eflash_load' build/lxr02.map
```

They must establish all of the following:

1. `transientData` remains `0000675c` bytes and has a `0x080...` application
   FLASH address, never a `0x200...` DTCM address. Its symbol class can be
   lower-case `t` when the linker merges `.rodata` into executable `.text`, or
   `r` in another valid LTO layout; address and size are the invariants.
2. `.dtcm` falls from `0x8960` / 35,168 B to `0x2204` / 8,708 B, since the
   current two-byte tail alignment remains after subtracting `0x675c`.
   `.dtcmz` remains `0x0df4` / 3,572 B. DMA, `.data`, `.bss`, Pattern storage,
   and the tagged runtime-slot allocation must be unchanged.
3. `transientData` no longer belongs to the DTCM `LOAD` program segment. The
   final `.dtcm` load end and every ordinary FLASH load address may shift, but
   the linker assertions must still keep `_etext` and `_eflash_load` before
   `0x08080000`, the sample-FLASH boundary.
4. Compare the final `build/lxr02.bin` size with the 352,148-B baseline.
   It should be identical or differ only by observed linker alignment; any
   material growth means the table has been duplicated and is a failure.

### Target-audio proof

1. On Drum, Snare, Cymbal, and Hi-hat, verify selector 0 retains snap-pitch
   behavior and selector 1 retains trigger-time offset behavior. The transient
   block remains zero for those modes, but the engine behavior is not silence.
2. On each engine type, audition PCM selectors 2 through 13, including low and
   high `transient_freq`, low and high `transient_vol`, and retriggering before
   the 2,205-frame one-shot completes. The output must match the current
   waveform identity, pitch, and decay behavior.
3. Exercise `transient_wave`, `transient_vol`, and `transient_freq` from the
   panel, scene/kit load path, and active MIDI CC descriptor path. Assign
   transient-capable engines across the six tagged runtime slots and repeat a
   representative selection so placement has no type/slot dependency.
4. Run the worst case: six active PCM transients at high level, diverse row
   selections, fast retriggering, and pitch extremes. There must be no audible
   glitch, missed audio deadline, or CPU-monitor regression. This is at most
   192 FLASH byte loads per 32-frame render sub-block.
5. If the hardware test fails, restore the single `INCCM` token on
   `transientData` and retain the two explanatory comments only if still
   truthful. Do not introduce a RAM cache, prefetch buffer, or other DTCM/SRAM
   allocation without a new explicit user acknowledgement.

### Documentation proof

Regenerate `SRAM_MANIFEST.md` from the resulting ELF and add measured results
to this plan. The manifest must record the 26,460 B as DTCM capacity reserved
for future delay-line buffers, never as free/general SRAM. Free normal SRAM1
remains reserved for Pattern data.

## Explicit non-goals

- No conversion, compression, resampling, or change of the signed 8-bit PCM
  samples.
- No change to waveform IDs, parameter IDs, scene/kit serialization, MIDI
  behavior, or tagged instrument-slot ownership.
- No linker script, startup assembly, cache configuration, or DMA change
  unless the default `const` placement unexpectedly fails verification.
- No use of newly released DTCM for a cache, scratch buffer, Pattern data, or
  feature other than a future explicitly approved delay-line allocation.
- No removal of the unrelated dead scalar renderer/envelope table in the same
  commit; that cleanup remains a separately scoped choice.
