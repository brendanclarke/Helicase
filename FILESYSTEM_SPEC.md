# Helicase SD Card Filesystem Specification

This document defines the Phase 2 SD card layout for the Helicase/LXR-02 firmware.
The goals are to replace the legacy flat `P000.SND`/`GLO.CFG` layout with a typed
library structure, make banks/scenes/kits portable as folders, and keep user-copyable
files predictable.

## Root Layout

The firmware recognizes these root-level directories:

```text
Bank/
Scene/
Kit/
Pattern/
Sample/
Wavetable/
Effect/
Instrument/
```

The only root-level file recognized by the firmware is:

```text
settings.cfg
```

`settings.cfg` replaces the legacy `GLO.CFG`. It stores system-level settings and a
reference to the last loaded bank. At boot, the firmware should load the bank recorded
there.

Root-level entries outside this list are ignored by the normal loader/browser.

## Numbered Folders

`Bank`, `Scene`, `Kit`, and `Wavetable` contain meaningful numbered subdirectories.
Numbered folders use this form:

```text
001 <name>
002 <name>
003 <name>
...
```

The numeric prefix is the slot number shown in the UI. Numbers do not need to be
contiguous. Browsers should scan slots sequentially and show missing slots as empty,
for example `003: Empty` when slot 3 has no matching folder.

Names after the numeric prefix are user-facing labels. The preferred separator after
the three-digit slot number is a space, as in `001 Slak`, but loaders may accept an
underscore for compatibility with older generated folders, as in `001_Slak`. Spaces
inside the displayed name are valid. The numeric prefix is authoritative for slot
order; folders should not be sorted only by full filename.

## Bank

`Bank/` contains bank folders:

```text
Bank/
  001 <bank name>/
  002 <bank name>/
```

A bank represents all non-global data that is loaded at one time. The last loaded bank
is recorded in `settings.cfg`.

Each bank folder contains exactly one bank-level config file:

```text
bankset.bcg
```

`bankset.bcg` stores bank-level metadata/configuration. It also acts as the validator,
guard, and version marker for identifying a folder as a bank. A folder without a valid
`bankset.bcg` must not be loaded as a bank.

Each bank folder also contains up to 16 scene folders:

```text
Bank/001 <bank name>/
  bankset.bcg
  001 <scene name>/
  002 <scene name>/
  ...
  016 <scene name>/
```

Scene slot numbers inside a bank do not need to be contiguous. Missing scene slots are
shown as empty in the UI. A user may exchange scene folders between banks.

## Scene

`Scene/` is a root-level pool of user-copyable scene folders:

```text
Scene/
  001 <scene name>/
  002 <scene name>/
```

Scene folders in this pool can be loaded into a bank scene slot. They use the same
folder structure as scene folders inside a bank.

A scene folder contains:

```text
sceneset.scg
Kit <kit name>/
pattern.pat
effect.fx
```

`sceneset.scg` stores scene-level metadata/configuration and validates the folder as a
scene.

`Kit <kit name>/` is the scene's embedded kit directory. It works like a kit folder,
but is named without a numeric slot prefix because it belongs to the scene.

`pattern.pat` stores the scene's pattern data.

`effect.fx` stores the scene's effect settings and effect automation sequence.

## Kit

`Kit/` is a root-level pool of numbered kit folders:

```text
Kit/
  001 <kit name>/
  002 <kit name>/
```

Kit folders can be loaded into a scene. Slot numbers do not need to be contiguous, and
missing slots are shown as empty in the UI.

A kit folder contains:

```text
kitset.kcg
<instrument 1>.<type>
<instrument 2>.<type>
<instrument 3>.<type>
<instrument 4>.<type>
<instrument 5>.<type>
<instrument 6>.<type>
```

`kitset.kcg` stores kit-level metadata/configuration. It also records which instrument
files occupy the six voice slots.

The six instrument files are named by the kit/instrument, for example:

```text
909_hard.drm
splashy.snr
highlights.hat
```

Users should not copy instrument files into a kit folder manually. Users may copy
instrument files out of a kit folder into the root `Instrument/` pool.

Initial instrument file types are:

```text
.drm  drum
.snr  snare
.cym  cymbal
.hat  hi-hat
```

These correspond to the four existing original LXR instrument types. Additional
instrument types may be added later.

## Wavetable

`Wavetable/` contains numbered wavetable folders:

```text
Wavetable/
  001 <wavetable name>/
  002 <wavetable name>/
```

Each wavetable folder contains an alphanumerically sorted set of `.wav` files:

```text
Wavetable/001 <wavetable name>/
  <sample a>.wav
  <sample b>.wav
  <sample c>.wav
```

Wavetables are loaded during the sample-load process and written to flash. They behave
like normal samples in storage, but are only read by wavetable oscillators. A wavetable
oscillator operates on one wavetable at a time and can be modulated across all samples
inside that wavetable. Wavetable samples always play looped. The menu shows the
wavetable name when selecting the wavetable used by the oscillator.

## Pattern

`Pattern/` is a root-level pool of pattern files:

```text
Pattern/
  <pattern name>.pat
```

Files are browsed alphanumerically. A pattern file can be loaded into a scene. Users
may copy a scene's `pattern.pat` into this pool, and may copy a pool pattern into a
scene if they rename it to `pattern.pat`.

## Sample

`Sample/` contains an alphanumerically sorted list of `.wav` files to write to flash:

```text
Sample/
  <sample name>.wav
```

Samples play from normal oscillators. Looping is an oscillator-level option, not a
directory-level distinction.

## Effect

`Effect/` is a root-level pool of effect files:

```text
Effect/
  <effect name>.fx
```

Files are browsed alphanumerically. An effect file can be loaded into a scene. Users
may copy a scene's `effect.fx` into this pool, and may copy a pool effect into a scene
if they rename it to `effect.fx`.

## Instrument

`Instrument/` is a root-level pool of instrument files:

```text
Instrument/
  <instrument name>.<type>
```

Files are browsed alphanumerically by type when loading into a kit in a scene. Users may
copy instrument files from a kit folder into this pool. Users should not copy files from
this pool directly into a kit folder; kit membership is controlled by `kitset.kcg`.

Initial recognized instrument types are:

```text
.drm
.snr
.cym
.hat
```

## Example

```text
settings.cfg
Bank/
  001 Factory/
    bankset.bcg
    001 Breakbeat/
      sceneset.scg
      Kit 909ish/
        kitset.kcg
        909kik.drm
        dark.drm
        click.drm
        snap.snr
        metal.cym
        tight.hat
      pattern.pat
      effect.fx
Scene/
  001 Loose Jam/
    sceneset.scg
    Kit Loose/
      kitset.kcg
      ...
    pattern.pat
    effect.fx
Kit/
  001 909ish/
    kitset.kcg
    909kik.drm
    dark.drm
    click.drm
    snap.snr
    metal.cym
    tight.hat
Pattern/
  four_on_floor.pat
Sample/
  glass_hit.wav
Wavetable/
  001 Vowels/
    a.wav
    e.wav
    i.wav
Effect/
  short_room.fx
Instrument/
  909kik.drm
  snap.snr
```
