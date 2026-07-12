# Session 034 Handoff Log

DATE: 2026-07-12
SESSION GOAL: Finish Instrument Load: registry/Choke behavior, root pool and
Load UI, Scene-aware Kit/Instrument refinements, then diagnose and correct the
Instrument-load Voice 2 parameter-lock/long-decay failure.
SOURCE AT END: local working directory, a development branch.
VERIFIED ON HARDWARE: yes. The user reported that the final Instrument Load
workflow works substantially as expected. Local `make -j4` and `git diff
--check` passed. The established nano-libc syscall warnings and LTO serial note
remain the only build output of concern.

## Completed

- Added registry-owned `Basic`, `Advanced`, and `Choke` type flags, labels,
  extensions, registry iteration, and Advanced-count selection enforcement.
- Completed slot/type-dispatched runtime hosting, so a loaded type is the type
  actually triggered, rendered, filtered, panned, and parameter-applied.
- Generalized HiHat closed/open decay to canonical base/Choke keys and added
  generic VOICE7 `_choke` descriptor substitution.
- Added generated non-Choke slot-6/track-7 alternate decay storage, menu,
  `kitset.kcg` keys, and Scene modulation target `7dc`.
- Implemented root `Instrument/` scanning/loading, populated the pool from
  converted Kit files, and completed nested Instrument Load UI behavior.
- Removed Pattern, MorphKit, Perform, and All from visible Load/Save options.
- Added Scene-aware Kit target toggles and Instrument single-Scene selection,
  Load-menu Scene LEDs, retained Kit file stems, and stopped voice preview.
- Replaced the unsafe one-slot Instrument loader/apply lifecycle with staged,
  transactional commit, all-source modulation clear/rebind, and UI locking.
- Folded the temporary audits into this log and durable project documents.

## Registry, Assignment Policy, And Runtime Dispatch

Files: `Core/DSP/Instruments/InstrumentManager.c/h`, all four instrument
`*Parameters.c/h` files, voice engines, mixer, MIDI trigger path, and `main.c`.

The registry now supplies firmware-only type metadata. It is never stored in an
instrument text file:

| Type | Label | Token / extension | Flags |
|---|---|---|---|
| Drum | `Drum` | `drm` / `.drm` | Basic |
| Snare | `Snare` | `snr` / `.snr` | Basic |
| Cymbal | `Cymbl` | `cym` / `.cym` | Advanced |
| HiHat | `HiHat` | `hat` / `.hat` | Advanced, Choke |

A Kit may have any number of Basic instruments and no more than two Advanced
instruments. The destination slot is excluded when testing a replacement.
Menu, storage, and runtime code ask InstrumentManager rather than keeping their
own type lists.

The original runtime was fixed to three Drum globals plus singleton Snare,
Cymbal, and HiHat globals. InstrumentManager now preserves those native
objects for legacy compatibility while owning per-type runtime pools and
type-dispatching the active Scene slot. Pointer-based engine entry points let
multiple slots host the same type. Mixer and trigger paths now call
InstrumentManager's current-slot dispatch; track 7 remains the slot-6 alternate
trigger.

## Choke And Track-7 Decay

HiHat canonical file keys are now:

| Previous key | Canonical key | Meaning |
|---|---|---|
| `amp_envelope_decay_closed` | `amp_envelope_decay` | base/closed decay |
| `amp_envelope_decay_open` | `amp_envelope_decay_choke` | Choke/alternate decay |

User-facing descriptor text and runtime members did not change. There is one
`hihat_menu_pages[]`; the former open menu table is gone. On VOICE7 for a
slot-6 Choke instrument, Menu asks InstrumentManager for the descriptor whose
key is the base key plus `_choke`. This rule is generic and `_choke` rows remain
normal independent modulation targets. Storage accepts the two legacy HiHat
keys as aliases; descriptor tables stay canonical.

For a non-Choke instrument in slot 6 that owns `amp_envelope_decay`, track 7
uses generated retained Kit endpoints:

- `slot6_track7_amp_envelope_decay`
- `slot6_track7_morph_amp_envelope_decay`

They are optional `kitset.kcg` settings, a generated VOICE7 menu cell, and
Scene modulation target `7dc`; they are not instrument-file values. Velocity
modulation retained-writes the setting. LFO modulation uses a runtime-only
override so it cannot rewrite saved Kit data every block. If a non-Choke type
lacks base decay, VOICE7 falls back to the ordinary slot-6 page.

## Instrument Pool And Load UI

Files: `Core/Hardware/SD/filesystem.c/h`, `storageTypes.c/h`,
`Core/Scene/Preset/presetManager.c/h`, `Core/Menu/menu.c/h`,
`Core/Hardware/frontPanel/buttonHandler.c`, PatternData, SceneData, converter,
and `SD_CARD/Instrument/`.

Filesystem owns a private, per-type root Instrument cache with sorted FAT-open
names and eight-character display stems. Menu sees type, count, display index,
and display name. Pool files are grouped by extension; display position is
one-based and saturates visually at `999` without semantic meaning. The
converter upgraded 62 Kit files and populated 186 root instrument files; its
current-tree fallback remains safe when legacy `PAR_*` symbols are unavailable.

Visible Load/Save choices are now only Kit, Settings, and Samples. Compatibility
backends were not deleted merely because the product UI no longer exposes them.

Pressing a VOICE button in Load enters Instrument Load for that slot. The type
row is selected/bracketed immediately. The lower row begins as `kit <stem>`
from `kit_t.instrument_display_name[slot]`, which is display provenance only,
not a parameter, path, or new save authority. Changing type changes only the
pending type/filter and Advanced eligibility; it never loads a file. First
lower-row motion changes the source to a root-pool item, renders `[###]<stem>`
with brackets around only the Kit/list field, and loads immediately. Later type
changes retain the last Kit/pool identity until lower-row movement. The old
Instrument loading progress screen was removed.

When stopped, a second press on the selected destination previews with
`seq_previewVoice()`; track 7 retains its ordinary preview route.

`pat_sceneHasActiveSteps(scene)` is the PatternData-owned occupancy query for
Load-menu Scene LEDs. Kit Load uses SEQ buttons as a 16-bit Scene target-mask
toggle; the active Scene begins selected and blinking, and can be deselected.
Instrument Load selects exactly one Scene and never uses a mask. Base LEDs show
only Scene pattern activity and blink overlays show selected targets. Button
handling consumes matching press/release events while Load owns the SEQ row.
`SCENE_COUNT` is still one, but the design loops over valid resident Scenes and
the 16 physical LEDs. A selected Kit parses once into staged `kit_t` and fans
out only the Kit payload into selected Scenes after all files validate. Pattern
and Scene settings remain per-Scene; only active Scene runtime is applied.

## Voice 2 Parameter-Lock Investigation

The user observed Voice 2 (`slakd2`) sustaining for minutes. Voice-menu decay,
LFO assignments, polarity, and amount seemed ineffective. The fault appeared
from both stopped manual preview and sequenced playback. `2vm` was zero; both
main and Morph decay endpoints were set low; `slakd2` in the Kit and root pool
was identical and benign: decay `56`, LFO amount `0`, LFO target off, velocity
amount `0`, velocity target off. Any Instrument loaded into Voice 2 retained
the fault; loading a complete Kit removed it.

The Kit-versus-Instrument result isolated a one-slot lifecycle defect. A
one-slot apply rewrote only the destination while a Kit apply rebuilt all six
source relationships. A stale cross-slot source or a dynamic-pool source
orphaned during a type swap could retain/restore a bad direct runtime value.

Supporting audit evidence that must remain visible:

- The root pool had 42 contradictory LFO voice/parameter pairs among 55 active
  finite first pairs. Menu normalized a mismatch to off/invalid while runtime
  previously installed the packed canonical target directly.
- The legacy converter copied target voice independently of a canonical target
  made from hardcoded descriptor order. That list is stale after LFO additions;
  later IDs must be regenerated against current descriptor arrays.
- `modNode_resetTargets()` and `modNode_directOriginalValueChanged()` still
  enumerate fixed globals instead of every dynamic runtime-pool node.
- Direct envelope LFO modulation still writes raw `SlopeEg2.decay` float in
  generic `0..1` range instead of byte-domain setter semantics. Zero is a zero
  decrement and can hold an envelope indefinitely.
- Pattern `Step` uses 16-bit canonical IDs but legacy `AutomationNode` playback
  still assumes 8-bit CC/CC2 destinations and a 255-cell baseline array.
  `65535` off and canonical IDs can cause wrong legacy writes or out-of-bounds
  reads. This is a separate high-severity migration task, not the immediate
  stopped-preview writer in the observed Voice 2 failure.

## Implemented Instrument Transaction

Files: `filesystem.c/h`, `presetManager.c/h`, `InstrumentManager.c/h`,
`menu.c/h`, and `buttonHandler.c`.

The root Instrument path is now staged and immutable:

1. Filesystem accepts explicit Scene, slot, type, and cache-index coordinates.
   `preset_loadInstrument()` writes completion context only after acceptance;
   a rejected overlap cannot redirect an in-flight completion.
2. Filesystem parses and validates into private `kit_instrument_slot_t` and
   display-name staging, never into live SceneData during I/O.
3. At active-Scene completion, Preset clears every current LFO pair and
   velocity target before the slot type changes.
4. Preset copies staged slot/name into SceneData, resets only the incoming
   runtime instance, applies its route, and queues `presetMorph_requestAll()`.
5. The bounded cursor rebuilds all six descriptor Morph/runtime images, then
   normalizes/reinstalls target pairs and velocity targets one source per pass.
6. Inactive Scene loads commit retained state only; later Scene activation owns
   its runtime apply.

All-source clear/rebind is intentional: any source can target the changed slot.
Clear and reset are separate APIs because clear must resolve outgoing type while
reset must resolve incoming type.

The Instrument transaction lock now covers the entire read, commit, all-slot
Morph rebuild, and source rebind. Encoder was already dropped by
`menu_storageBusy`; Scene selection, VOICE selection/preview, and all mode
changes including Load exit are now consumed without mutation while locked.
The filesystem remains single-operation.

## Documentation, Data, And Verification

- Updated `FILESYSTEM_SPEC.md` and `MODULE_INTERCHANGE_SPEC.md` with the
  implemented root Instrument pool, Choke/track-7 contract, Scene controls,
  transaction boundary, and module ownership.
- Updated `SCOPING_TARGETS.md` and `MEMORY.md` with Session 034 progress and
  remaining automation/modulation/save work.
- `INSTRUMENT_LOAD_AUDIT.md`, `INSTRUMENT_LOAD_MENU_FIX.md`,
  `INSTRUMENT_KIT_LOAD_REFINEMENTS.md`, and
  `INSTRUMENT_LOAD_PARAM_LOCK_BUG.md` are fully represented by this log and the
  durable documents; they may be deleted when desired.
- `make -j4` passed with text `297344`, data `348`, bss `98184` bytes. `git
  diff --check` passed.

## Remaining Work / Known Risks

1. New-format save is next: Kit/Instrument/Scene/Bank save is not implemented.
2. Migrate Pattern automation end-to-end to descriptor/Scene IDs and typed
   Preset/InstrumentManager application. Do not route canonical IDs through
   legacy MIDI CC packing.
3. Give shaper-backed modulation, especially envelope/pitch time controls,
   byte-domain owner adapters rather than direct runtime floats.
4. Make normal block reset/base-refresh iterate current InstrumentManager
   runtime sources, not hardcoded fixed voice globals.
5. Regenerate and validate converted LFO canonical target IDs from current
   descriptors; do not retain a hardcoded drifting descriptor-order table.
6. Continue hardware stress testing rapid Instrument loads and controls during
   a transaction, source/target type swaps, and cross-slot LFO destinations.

## End of Session Block

```
DATE: 2026-07-12
SESSION GOAL: Complete Instrument Load and resolve the load-path parameter lock.
COMPLETED: Instrument flags, Choke/track-7 behavior, root Instrument pool/UI,
Scene-aware Load refinements, and staged all-source runtime commit/rebind.
VERIFIED ON HARDWARE: yes; user reports Instrument Load works substantially as
expected. `make -j4` and `git diff --check` pass.

CHANGES THIS SESSION:
- InstrumentManager: type/runtime dispatch, all-source modulation clear, and
  incoming runtime reset.
- filesystem/storage/Preset: staged Instrument parse, immutable request context,
  transactional commit, six-slot runtime rebuild, normalization/rebind.
- Menu/button/Pattern/Scene: nested Load UI, Scene controls/LEDs, source stems,
  transaction input lock, stopped preview.
- Instrument descriptor/converter/SD data: flags, canonical Choke keys,
  generated track-7 decay, root Instrument pool.
- Specs, trajectory, memory, Session 034 log: durable closeout.

KNOWN ISSUES INTRODUCED: none known.
KNOWN ISSUES RESOLVED: Instrument Load no longer mutates live Scene state during
I/O or leaves stale cross-slot modulation/runtime state by design.

NEXT SESSION RECOMMENDED GOAL: implement new-format save, or separately migrate
Pattern automation before relying on automation with descriptor instruments.
BLOCKERS: rapid transaction stress testing remains useful; no design decision
is needed to begin save planning.

CRITICAL REMINDERS FOR NEXT SESSION:
- Root Instrument parsing must stay staged; filesystem must not reset a live
  Scene slot while reading.
- Clear outgoing owners before type change; rebuild all sources after runtime
  images are current.
- Keep Instrument controls locked through read, commit, Morph rebuild, rebind.
- LFO target voice and canonical parameter are one coupled relationship.
- Pattern automation is not descriptor-safe yet.
- Raw direct envelope modulation can set zero decay and needs an owner adapter.
```
