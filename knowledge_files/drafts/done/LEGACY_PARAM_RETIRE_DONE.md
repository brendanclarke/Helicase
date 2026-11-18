# Legacy parameter retirement plan

## Purpose

The current firmware still contains a small legacy parameter namespace and a
384-byte generic value bridge. The resident instrument parameter model has
already moved into Scene-owned descriptor images, but several menu, Pattern,
MIDI, modulation, and filesystem paths still use the legacy IDs and arrays.

This document records exactly what remains and the work required to retire it
safely. It is a retirement plan, not permission to delete the arrays before the
interleaved callers have been migrated.

## Current SRAM and Flash objects

The current source defines `END_OF_SOUND_PARAMETERS` as 1 and contains:

```c
Parameter parameterArray[END_OF_SOUND_PARAMETERS];
```

The current target `Parameter` is an aligned pointer plus a type byte, giving
8 bytes per entry. The fresh linked ELF contains:

| Object | Fresh ELF size | Region | Actual role |
|---|---:|---|---|
| `parameterArray` | 8 B | SRAM1 `.bss` | One legacy pointer/type dispatch entry |
| `parameters2` | 1 B | SRAM1 `.bss` | One legacy Morph overlay byte |
| `paramToModTarget` | 1 B | SRAM1 `.bss` | One legacy reverse mapping byte |
| `parameter_values[384]` | 384 B | SRAM1 `.bss` | Flat UI, performance, Pattern, and global byte bridge |
| `parameter_dtypes[384]` | 384 B | Flash `.text` | Constant menu datatype table |

`parameterArray` is not parameter metadata. It is a legacy runtime dispatch
table and, in this build, contains only the `PAR_NONE` namespace entry. There
is no 1,824-byte parameter-metadata allocation in the current binary.

Instrument values are held in Scene-owned byte images and serialized by the
instrument/Kit file writers. The generic legacy array is not their canonical
storage.

## Complete generic parameter list

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
endpoint images are owned by the Scene/Kit structures and are not duplicated
here.

## Why retirement is not a single deletion

The arrays are small, but their IDs are still interleaved with live behavior:

- Menu editing and display code indexes `parameter_values` for Pattern fields,
  globals, Morph, MIDI, SOM, Euclidean, and step-edit values.
- MIDI handlers write legacy IDs for Morph, Scene performance values, and other
  control paths.
- Pattern/Sequencer bridge code uses the generic IDs for step editing,
  automation destinations, track settings, and menu synchronization.
- Modulation code uses `paramToModTarget` and the parameter-ID namespace for
  reverse lookup and target display/dispatch.
- `parameters2` remains exposed to Menu as the Morph-side legacy buffer even
  though canonical instrument Morph endpoints are Scene-owned.
- Filesystem bridge/container paths still serialize or hydrate selected legacy
  global and Pattern values through `parameter_values`.
- `parameterArray` itself is nearly retired, but its init/set API and callers
  must be proven unused before the object and compatibility functions are
  removed.

The retirement must therefore migrate behavior by domain rather than simply
deleting 394 bytes and allowing the compiler to expose unrelated assumptions.

## Proposed retirement sequence

1. Create a source-use inventory for every `PAR_*`, `parameter_values`,
   `parameters2`, `paramToModTarget`, `parameterArray`,
   `paramArray_setParameter()`, and `parameterArray_init()` reference.
2. Classify each reference as Scene-owned, Pattern-owned, global-settings-owned,
   runtime-only, Menu transport, or genuinely obsolete.
3. Replace instrument/Morph uses first with Scene-owned descriptor images and
   the existing InstrumentManager/Preset accessors. This must leave no
   instrument endpoint value in `parameter_values` or `parameters2`.
4. Replace Scene performance IDs with direct `scene_settings_t` accessors:
   overall/per-voice Morph, Scene decimation, audio routes, FX sends, fader
   modes, MIDI channels, and MIDI notes.
5. Replace Pattern IDs with PatternData accessors. This is a bridge step only;
   the complete Pattern storage replacement remains Phase 4 work and must not
   be conflated with parameter retirement.
6. Move global settings IDs to their owning global-settings representation and
   keep file serialization explicit by named field. Do not recreate a generic
   SRAM array under another name.
7. Replace modulation reverse lookup with descriptor/Scene target ownership,
   retaining only the minimum runtime mapping required by live modulation.
8. Remove `parameters2` after Menu Morph editing no longer exposes it as a
   backing buffer.
9. Remove `paramToModTarget` after all reverse target lookup and target display
   paths use the descriptor/Scene registry directly.
10. Remove `parameterArray`, its initialization/set functions, and the legacy
    `ParameterArray` compatibility declarations after an ELF symbol/use audit
    proves they are unreachable.
11. Remove unused `PAR_*` enum members and shrink or delete the generic enum
    only after all file-format and menu code has moved to named owner APIs.

## Verification gates

- `rg` shows no instrument endpoint load/edit/save path using
  `parameter_values` or `parameters2`.
- Scene performance controls round-trip through Scene-owned fields without a
  generic ID bridge.
- Pattern editing and menu refresh continue to work through PatternData while
  the current bridge remains in place.
- Global settings load/save still round-trip every named field.
- MIDI Morph, Scene performance, automation, and modulation target display
  remain functional.
- The fresh ELF contains no `parameterArray`, `parameters2`,
  `paramToModTarget`, or `parameter_values` symbols when their last legitimate
  client is removed.
- `arm-none-eabi-size -A`, `arm-none-eabi-nm -S --size-sort`, and hardware
  menu/MIDI/load-save tests are run after each domain migration.

## Explicit non-goals

- Do not call `parameter_dtypes[384]` SRAM storage; it is a Flash-resident
  constant table and must be handled separately if its menu-generation role is
  later redesigned.
- Do not remove the current Pattern bridge as part of this document; its full
  replacement is scoped to Phase 4.
- Do not store parameter metadata in SRAM. Persistent object values and named
  file fields must remain explicit in their owning files and owner structures.
