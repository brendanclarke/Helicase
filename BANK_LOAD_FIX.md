# Bank Load child-discovery reset

## Status

Implemented and hardware-confirmed during this session. Loading the freshly
saved Bank directory 000 Full no longer reports ERR BnkL14.

## Symptom and scope

The failure occurred only after Bank Load had begun loading the second selected
Scene. The saved tree itself was valid:

    SD_CARD/Bank/000 Full/bankset.bcg
      format=helicase.bankset
      version=2
      active_scene=6
      scene_mask_voice_edit=0x0040

The directory contains Bank-local Scene folders 00 through 15. Each has the
expected sceneset.scg, Kit directory, pattern.pat, and effects.fx payload.
The first two children illustrate the failure:

    00 Slak / Kit Brezel
    01 Slak / Kit Forest

The error therefore was not a malformed bankset.bcg or an invalid Kit saved by
Bank Save. It was a state-isolation defect in Bank Load while it reused the
shared Scene loader.

## Cause

filesystem_loadBank_tick delegates each Bank-local Scene payload to
filesystem_loadSceneDirectory_tick. That shared Scene loader discovers the
child-specific names of the Kit directory, pattern file, and effects file and
stores them in four operation scratch buffers:

    op_scene_child_open_name
    op_scene_child_display_name
    op_scene_pattern_open_name
    op_scene_effect_open_name

Before this fix, root Scene Load cleared those buffers at its own phase 0, but
Bank Load did not clear them between children. The first Bank child populated
the buffers with names from 00 Slak, including Kit Brezel. On the next child,
the non-empty buffers made the discovery phase believe that discovery was
already complete. It consequently attempted to open Kit Brezel inside
01 Slak, rather than discovering and opening Kit Forest. That open failed.

The externally visible BnkL14 is slightly misleading. The Bank wrapper reports
its own decimal phase number in hexadecimal. The child failure reaches the
Bank wrapper's decimal phase 20, which is rendered as hexadecimal 14, so the
reported code is BnkL14 rather than the Scene loader's local failure phase.

## Changes made

### Core/Hardware/SD/filesystem.c

Added the private helper:

    filesystem_resetSceneLoadChildDiscovery()

It clears all four child-discovery scratch buffers as one named operation.
Its comment records the ownership rule: a root Scene load needs one reset,
while a Bank load needs a reset before every delegated local Scene payload.
The comment also records the concrete 00 Slak / 01 Slak failure sequence and
the BnkL14 phase translation so future loader work does not reintroduce it.

Replaced the root Scene loader's individual buffer assignments in
filesystem_loadSceneDirectory_tick phase 0 with the helper. Root behavior is
unchanged; this makes the reset rule share the same implementation as Bank
Load.

Called the helper in both Bank Load handoffs:

1. Before the first selected Bank child is delegated to the Scene loader.
2. Before every later selected child is delegated after the Bank child iterator
   advances.

The second call is essential. Resetting only before the first child would still
carry the first child's discovered names into the next child.

### Core/Hardware/SD/filesystem.h

Expanded the public filesystem_requestLoadBank contract. The header now states
that a Bank child is an independent Scene payload and its Kit/pattern/effect
discovery must not inherit filenames from child 00. It also documents that a
violation surfaces as BnkL14 through the Bank wrapper.

## Runtime behavior after the fix

For every selected Bank-local Scene folder, Bank Load now follows this sequence:

    select a child folder
    clear only the Scene child-discovery scratch
    run the shared Scene-directory loader
    discover this child's Kit, pattern, and effects names
    load that child
    advance to the next selected child

The reset does not discard the Bank manifest, selected-child iterator, active
Scene, present-mask information, or the loaded Scene data. It only removes
transient names whose lifetime is exactly one Scene-directory load.

## Verification

- Inspected 000 Full and verified a valid version-2 bank manifest and complete
  Scene payload directories.
- Traced the stale-name path from the Bank child dispatcher through the shared
  Scene loader and the Bank error wrapper.
- Ran git diff --check successfully after the code change.
- The user confirmed that the corrected Bank Load works on hardware.

A local firmware build could not be run in this checkout because
arm-none-eabi-gcc is not installed or available on PATH. No build artifact was
modified.

## Regression invariant

Any future caller that delegates more than one directory to
filesystem_loadSceneDirectory_tick must establish a new child-discovery
lifetime for each directory. The buffers above are cache-like operation scratch,
not persistent loader state and not a valid optimization across sibling
directories.
