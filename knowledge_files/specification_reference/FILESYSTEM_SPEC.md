# Helicase SD Card Filesystem Specification

This is the authoritative product-level filesystem and instrument-file
reference for the Helicase/LXR-02 firmware through the Session 047 bounded-CRC
and boot-capture update. It includes the
full Session 032 instrument/kit file specification formerly kept in
`INSTRUMENT_FILE_SPEC.md`, plus the Session 033-039 runtime decisions for LFO,
velocity modulation, Morph, per-voice Morph, Scene modulation targets, Choke
behavior, Instrument Load, Kit/Instrument Morph Load, Kit/Instrument Morph
Save, Kit Save, root Instrument Save, Scene/Bank directory load/save, draft
Scene/Bank pattern persistence, storage-only LFO `self` routing, the
generalized `.hcindex` cache, root `/.hcnames`, canonical name repair, the
hidden Instrument Load temporary source, and the cold-boot tagged-runtime plus
final Scene/Bank Load index-ordering contracts. Low-level asyncfatfs API contracts
and caller rules now live in
`ASYNCFATFS_REFERENCE.md`.

AutoSave's hidden-record format, dirty ownership, and background writer are
authoritative only in `AUTOSAVE.md`. Development flags and diagnostic files are
authoritative only in `DEV_MODES.md`; this document names those facilities only
where they intersect the product filesystem.

Use this document to distinguish three things:

- Implemented now: root `Kit/NNN Name/` directory loading/saving, root
  `Instrument/` pool replacement into Scene-owned descriptor-indexed
  instrument parameter images, Kit/Instrument Morph Load, Kit/Instrument Morph
  Save, normal new-format Kit Save, root Instrument Save, root Scene
  Load/Save, 16-Scene root Bank scan/load/save, keyed settings.cfg,
  slot-ordered `.hcindex` name indexes for every Instrument type plus root Kit,
  Scene, and Bank, root `/.hcnames`, canonical eight-character name repair, a
  separate 2,048-byte non-Pattern validation stage, the reversible Instrument
  Load `kit` row backed by `.hctmp.<ext>`, and the root
  `/.hcprms1`/`/.hcprms2` AutoSave pair specified in `AUTOSAVE.md`.
- Settled target shape: Bank, Scene, Kit, Pattern, Sample, Wavetable, Effect,
  Instrument, and `settings.cfg` filesystem layout.
- Not implemented yet: applying the hidden AutoSave winner to resident state,
  whole-object AutoSave publication for Load/Save/copy operations, AutoSave
  Pattern/Effect persistence, crash-recoverable promotion into explicit Bank
  library files, real Effect load/save, final dynamic Pattern storage, descriptor-backed step
  automation playback, versioned/recoverable HCNAMES, and `/.hcrepair`
  roll-forward. The legacy `kitBrowser` map and File/Dir diagnostic caches are
  retired.

Historical session logs and drafts may describe older flat `.SND`/`GLO.CFG`
behavior. This file is the current source of truth for the intended filesystem
and current implemented state.

## Current Implementation Status

Implemented through the inherited Session 046 baseline plus Session 047:

- Normal kit loading scans root `Kit/` for numbered folders using asyncfatfs
  object iteration.
- Preferred kit folder names are `NNN Name`, for example `000 Init` or
  `001 Slak`.
- Compatibility kit folder names with a single underscore after the slot,
  `NNN_Name`, are accepted.
- FAT short-alias fallback accepts aliases beginning with a valid three-digit
  slot prefix, such as `000INI~1` or `001SLA~1`.
- Kit, root Scene, and root Bank scans store only the eight-character display
  name in the one generalized cache. A non-blank row is the slot-existence
  record; no per-slot presence, display-name, or short-alias arrays remain.
  Load reconstructs `NNN Name` and keeps any asyncfatfs alias only in
  operation-local scratch.
- The kit display name is the folder name after the three-digit slot prefix.
- `kitset.kcg` is parsed as the six-slot kit manifest.
- Six descriptor-keyed instrument text files are loaded from the kit folder.
- Root `Instrument/<type>/` is scanned one type at a time with asyncfatfs object
  iteration into the one shared 1,000-row alphanumeric browser cache. The cache
  is disposed between boot types; entering or changing the nested menu type
  reloads that type's `.hcindex` before browsing. During a combined
  Kit/Instrument menu session the applicable index remains resident while
  payload validation uses a separate stage. Instrument rows use sorted
  positions; the cache is never multiplied by registry type.
  A selected file can then be loaded into one explicit Scene/voice slot.
- Loaded instrument values write into the active `scene_t.kit` descriptor
  images, not into the old flat `parameter_values[]` sound buffer.
- VOICE menu pages resolve through active instrument descriptor layouts in
  `Core/DSP/Instruments/*/*Parameters.c`.
- Preset/InstrumentManager applies descriptor image values back into the DSP
  runtime after load and menu edits.
- SceneData initializes every retained Instrument type before
  `instrumentManager_runtimeInit()` constructs the six tagged DSP members.
  Scene activation first clears outgoing targets and image-applies all six
  incoming types, then performs one all-source LFO/velocity rebind. Cold boot
  starts that exact ordinary Scene-switch worker after audio startup; no
  physical slot/type arrangement is assumed.
- Descriptor-backed Morph works from Scene-owned main/morph endpoint images.
- PERF Morph has been split into one Scene global setter plus six per-voice
  Morph amounts. The global Morph control bulk-sets the six per-voice values.
- LFO and velocity modulation can target active-slot descriptor parameters
  without hardcoded per-instrument parameter lists.
- LFO and velocity modulation can target the shared Scene modulation namespace:
  per-voice Morph targets `1vm..6vm` and Scene Decimation `srt`.
- Per-instrument `instrument_decimation` is a voice-local descriptor target and
  is morphable, modulatable, and marked automatable for the future automation
  pass.
- LFOs now expose two target selector pairs with shared oscillator settings and
  shared polarity.
- VOICE sub-pages can expose up to 16 descriptor cells as four-cell screens.
- Instrument registry metadata declares Basic, Advanced, and Choke loading
  policy. Drum/Snare are Basic; Cymbal is Advanced; HiHat is Advanced|Choke.
- Kit Load uses SEQ buttons as Scene target toggles; Instrument Load uses them
  as one-Scene selection. Load-menu SEQ LEDs show Scene step activity and blink
  the current target selection.
- Root Instrument parsing is staged. An active-Scene commit clears all current
  modulation owners before type replacement, rebuilds all six runtime Morph
  images, then normalizes and reinstalls all source target relationships.
- Kit Morph Load is a Load menu entry `Load:[KitMrp  ]`. It parses the same
  root Kit directory as normal Kit Load, but Preset copies source normal
  endpoint values into resident morph endpoints only for slots whose instrument
  types match. Mismatched source/destination slots are no-change.
- Instrument Morph Load is the nested Instrument Load type-row sibling for the
  currently loaded slot type only, shown as `<Type>Mrp`. It loads the selected
  root Instrument file through normal staging, then copies staged normal
  endpoint values into the destination slot's morph endpoint only when the
  slot type still matches.
- Normal `Save:[Kit     ]` writes the active Scene kit to the directory Kit
  format: a numbered `Kit/` folder, `kitset.kcg`, and six descriptor-keyed
  instrument files containing `[params]` and `[morph]`.
- Root Kit/Scene/Bank scan/load/save slot range is now direct `000..999`.
  Slot `000` is a real library slot for all numbered filetypes. Firmware
  library slot variables are `uint16_t`; voice slots remain byte-sized `0..5`.
- Root Instrument Save is implemented from nested Save-page VOICE mode. It
  writes one resident Scene voice to `Instrument/<stem.ext>` using the same
  descriptor-keyed instrument text writer used by Kit Save member files.
- SceneData retains no Bank, Scene, Kit, Instrument, filename, or stem text.
  Root `/.hcnames` is the resident identity authority. One 81-byte active
  identity block (BankData's Bank row plus filesystem's Scene, Kit, and six
  Instrument rows) survives while `.hcindex` owns the generalized cache.
  Instrument leaves are derived from an eight-cell identity/index stem and the
  registry type extension.
- Instrument text files accept `self` only for `lfo_target_voice` and
  `lfo_target_voice_2`. The parser resolves it immediately to the file's
  expected one-based destination slot; SceneData, Menu, Preset, and DSP runtime
  still see ordinary numeric voice selectors.
- Kit Save emits `self` for an LFO target voice when the stored numeric target
  equals the source instrument's own one-based slot. Cross-slot LFO targets
  remain decimal voice numbers.
- Descriptor-backed LFO targets use descriptor-owned parameter-domain metadata
  and apply temporary LFO-shaped values through the normal descriptor runtime
  writer. Negative polarity matches original LXR value-relative behavior in
  parameter space instead of subtracting a full raw runtime range.
- Scene settings now own per-voice `audio_out[6]`, `fx_send_amount[6]`, and
  `fader_setting[6]`. `kitset.kcg` never emits these values; legacy
  `audio_out=` lines are parse-only side data for old embedded Kits.
- Root Scene Load/Save is implemented for `Scene/NNN Name/` folders containing
  `sceneset.scg`, embedded `Kit <name>/`, `pattern.pat`, and `effects.fx`.
  `sceneset.scg` never stores the Scene name.
- Root Scene and embedded Kit names originate in directory names but are
  published to their fixed HCNAMES rows. They are not fields of `scene_t` or
  `kit_t`.
- Root Bank scan/load/save uses the 16 resident Scene slots. Boot repairs,
  scans, and rebuilds `/Bank/.hcindex`, reloads it after Instrument index
  generation has disposed the shared cache, and tries BankData's restored slot
  (default 000) before root Scene/root Kit fallback. Empty Bank folders are
  valid and complete Bank selection before fallback.
- Bank-local Scene folders use two digits, `00..15`, not root-library
  three-digit numbering. Bank Save writes every child selected by its 16-bit
  save mask and Bank Load iterates every requested/present local child.
- Scene/Bank `pattern.pat` text v3 stores only the 112-byte 128x7 active-step
  bitmap as seven 32-hex-character rows. Version 1 placeholders remain
  accepted; v2 imports only its final 128-bit field and discards its former
  length/scale prefix.
- `File`, `Dir`, and Save-only `sDir` menu diagnostics and their two 64-entry
  name caches are retired. Compatibility APIs return empty/failure without
  starting filesystem work.
- Kit, Instrument, and non-Pattern Scene validation share one independent,
  aligned 2,048-byte stage. The 9,000-byte name cache never aliases payload
  staging. Pattern streams directly to final Scene SRAM after Scene
  settings/Kit validation and commit.
- Instrument Load exposes a synthetic `kit` row above typed pool row `000`.
  Entry writes the original voice to `Instrument/<type>/.hctmp.<ext>` and
  retains one nine-byte label. The hidden file is excluded from name repair and
  `.hcindex`; returning to `kit` parses it through the ordinary one-candidate
  Instrument stage.
- Explicit Scene/Bank OK commands keep `...`, cursor suppression, and the
  Menu input gate active through payload/HCNAMES work, shared runtime apply,
  and one final read-only reload of the unchanged root `.hcindex`. A pure Load
  does not physically scan or rewrite that index. Kit/Scene/Bank Saves alone
  own physical parent rescan plus complete index rebuild after namespace
  mutation.
- Entering top-level Load:Bank is not ready when `/Bank/.hcindex` alone has
  loaded. Menu immediately scans the highlighted Bank's immediate `00..15`
  children and holds input until that callback publishes the selectable mask.
  Only an accepted explicit OK request enters `...`.

Current bridges and limitations:

- `SCENE_COUNT` is 16. There is no second full resident Scene. Filesystem owns
  a 2,048-byte non-Pattern stage containing either one Kit, one Instrument
  candidate, or Scene settings plus embedded Kit.
- Pattern/container storage remains a bridge shape and will be replaced by the
  later dynamic stack Pattern implementation.
- `FS_FILE_KIT` save now routes to the new Kit directory writer. The old flat
  `.snd` Kit writer is no longer the normal Kit Save path.
- `FS_FILE_MORPH` load/save still uses the legacy `.SND` morph-kit path.
- Globals load/save through root keyed-text `settings.cfg` version 1. Legacy
  `glo.cfg` is retired and is not a fallback input.
- Effect, Wavetable-pool, and final dynamic Pattern-pool load/save operations
  are not implemented/promoted yet. Root Instrument, root Scene, and
  16-Scene root Bank load/save exist.
- Descriptor-backed LFO and velocity modulation runtime paths are in place for
  direct descriptor targets, voice-local decimation, per-voice Morph, and Scene
  Decimation. LFO direct descriptor overlays now go through descriptor-domain
  adapters; the remaining target-runtime gap is step automation.
- `AutomationNode` and the current step automation storage/playback path still
  use legacy/narrow target IDs and must be rebuilt for descriptor and Scene
  modulation targets.
- New Scene modulation target IDs are runtime/menu IDs; current Scene files
  persist Scene mix/routing settings but not the future full effect stack.
- The 16-Scene workspace, present/edit masks, and linked Scene/Pattern PERF
  selection are implemented. The hidden A/B scalar AutoSave writer exists as
  specified in `AUTOSAVE.md`; winner replay, whole-object publication,
  explicit-Bank promotion, and a separate background staging Scene remain
  future work.

## Root Layout

Settled target root directories:

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

Settled target root file:

```text
settings.cfg
/.hcnames
/.hcprms1
/.hcprms2
```

### Slot-ordered `.hcindex` name indexes and the single SRAM cache

`.hcindex` is a directory-local name index, not an opaque root boot marker.
Firmware creates or rebuilds these files from physical directory scans:

```text
Instrument/Drum/.hcindex
Instrument/Snare/.hcindex
Instrument/Cymbal/.hcindex
Instrument/HiHat/.hcindex
Kit/.hcindex
Scene/.hcindex
Bank/.hcindex
```

Instrument indexes contain alphabetically ordered display stems, one per line,
and can contain up to 1,000 rows. Kit, root Scene, and root Bank indexes are
slot ordered from 000 through 999; each line contains only the eight-character
display name, including blank lines for absent slots. The line number supplies
the three-digit slot prefix when Load/Save reconstructs `NNN Name`.

There is exactly one SRAM list/register array:
`fs_list_cache_name[1000][9]`, 9,000 bytes. Its active domain tag and count
are separate small fields. It contains one `.hcindex` domain or the 129-row
HCNAMES image, never both. No per-instrument-type, per-library, presence, or
open-alias name cache is permitted. The legacy `kitBrowser` compatibility map
was retired in Session 042; Kit occupancy is the active slot cache/index row.

The cache and payload stage are independent. An accepted Kit, Instrument, or
Scene payload may parse into the 2,048-byte `fs_stage_workspace` while the
active index continues to provide browser names. The rejected cache/stage union
erased index rows during request setup and produced blank `.hcindex` files,
`KitL00`, and scroll failures; it is not the current architecture.

Boot scans and writes Kit, root Scene, and root Bank indexes before audio
starts. Instrument types are then scanned/written one at a time. Because that
disposes the shared cache, boot reloads `/Bank/.hcindex` before selecting the
initial Bank. A successful Kit, Scene, or Bank Save performs the same physical
directory rescan and complete index rewrite before invoking the original Save
completion callback, so a newly created or renamed directory is immediately
visible without restarting.

### Root resident-name register: `/.hcnames`

HCNAMES is resident identity, not a directory browser. It has 129 fixed
logical rows:

```text
row 0        Bank
rows 1..16  resident Scene 0..15
rows 17..32 embedded Kit for resident Scene 0..15
rows 33..128 six Instruments per Scene
              row = 33 + scene * 6 + voice
```

Rows use fixed-order `name<TAB>source\n` text. `name` is at most eight
printable characters and may be empty; `source` is `-` (inherit), `?`
(unknown), `000` through `999` (direct root slot), or `@` (direct root
Instrument stem). The fixed row class supplies the namespace for a numeric
slot. The 129-by-`uint16_t` filesystem-owned source register follows
Instrument -> Kit -> Scene -> Bank until it finds a direct source or reaches
ordinary boot fallback. A legacy name-only line remains readable as unknown;
malformed extended records fail the read rather than silently inheriting. The
name cache remains space-padded and NUL-terminated.

Changing a row can change its physical byte length. Every targeted update
therefore reads all 129 name/source pairs into the shared cache/register,
overlays only the rows owned by the successful action, rewrites the complete
file, closes, and uses the normal flush gate. A staged source survives that
reread until the close succeeds, preventing stale on-card provenance from
overwriting a newly committed load. A missing file can be created by a targeted update from
blank rows plus the rows that action has proved valid, but only after a complete
case-insensitive root scan has proved zero matching HCNAMES entries. A NULL
read-open result is not proof of absence: one folded match permits one read
retry; multiple matches or any root-open/finder/close/FAT failure return an
error and authorize no creation or automatic repair.

Normal boot does not regenerate HCNAMES from resident SRAM. Scene identity is
not stored in `scene_t`, so a snapshot after a mask-selective Bank Load would
erase unselected Scene rows. The retained blocking writer is not part of the
normal boot path.

The only active identity strings outside the cache are exactly 81 bytes:
BankData's 9-byte Bank name plus filesystem's 72-byte Scene/Kit/six-Instrument
block. Kit/Instrument menu entry reads one Scene's seven-row block once and
later loads the needed index. Normal actions edit those rows and accumulate a
dirty Scene mask; family exit performs at most one HCNAMES update. Scene menu
entry borrows one Scene row. Bank Load/Save borrow the complete HCNAMES image.
Bank Save rebuilds `/Bank/.hcindex` before releasing its filesystem callback
because Save may mutate the root namespace. Bank Load publishes its completed
payload result after HCNAMES is durable; Menu applies the selected Scene and
only then reloads the unchanged `/Bank/.hcindex` read-only as the explicit
command's final step.

`settings.cfg` replaces legacy `GLO.CFG`/`glo.cfg` as the current system-settings
file. It stores allowlisted system-level settings and the active Bank number,
not the Bank display name. At boot, the current firmware reads this numbered
Bank selector; legacy glo.cfg is not attempted.

The current writer emits keyed text with:

    format=helicase.settings
    version=1
    active_bank=<0..999>
    autosave=<0|1>

The remaining accepted/written keys are bpm, ext_sync, quantisation,
midi_chan_global, midi_filt_tx, midi_filt_rx, midi_routing,
screensaver_on_off, bar_reset_mode, prescaler_clock_in,
prescaler_clock_out1, prescaler_clock_out2, follow, and osc_wave_interp.
The complete writer schema is 17 lines. Legacy `scene_source_NN` keys are
accepted and ignored so an existing settings file migrates without becoming an
error; provenance is now owned only by HCNAMES.
Unknown or out-of-scope keys are not a way to restore Scene state. In
particular, Morph, per-voice Morph, and Scene decimation belong to Scene
payloads, not global settings.

There is no implemented `.settings.cfg` backer or power-loss transaction for
`settings.cfg`. Its existing one-second revision/debounce writer rewrites the
live file; do not claim atomic replacement.

Root-level entries outside the recognized list are ignored by normal
loader/browser code.

## AsyncFATFS Directory Navigation

The underlying asyncfatfs layer uses a state-machine approach to navigate directories and open files. When writing filesystem traversal logic, several critical rules apply:

- **Case Sensitivity vs Insensitivity**: FAT filesystems are fundamentally case-insensitive but preserve case in Long File Names (LFN). When opening directories created by a user (e.g., instrument type folders like `Drum` or `Snare`), use `AFATFS_MATCH_CASE_INSENSITIVE` with `afatfs_opendir_lfn()` to tolerate manual lowercasing. Use `AFATFS_MATCH_CASE_SENSITIVE` only when strictly requiring an exact UI string match.
- **Directory Creation**: `afatfs_mkdir_lfn()` behaves as "open or create". If the directory exists, it resolves the handle; if missing, it creates the LFN fragments and generates an 8.3 alias. `afatfs_opendir_lfn()` strictly searches for an existing directory and will safely fail (return a NULL handle) if it does not exist.
- **Asynchronous Parent Traversal (`afatfs_chdirParent`)**: 
  - **WARNING:** `afatfs_chdirParent()` returns an `afatfsOperationStatus_e` enum (`SUCCESS` = 0, `IN_PROGRESS` = 1, `FAILURE` = 2), NOT a boolean.
  - Do **not** evaluate it as `if (!afatfs_chdirParent())`. Because `SUCCESS` is `0`, `!0` evaluates to true, which can cause state machines to incorrectly early-return upon success and get trapped in infinite traversal loops.
  - Correct usage must check explicitly: `if (st == AFATFS_OPERATION_IN_PROGRESS) return;`.
- **Absolute Root**: To jump back to the absolute root of the SD card, use `afatfs_chdir(NULL)`. This synchronously resets the global `afatfs.currentDirectory` to the FAT root directory without requiring an asynchronous block.

## Numbered Folders

`Bank`, `Scene`, `Kit`, and `Wavetable` contain meaningful numbered
subdirectories. Numbered folders use this form:

```text
000 <name>
001 <name>
002 <name>
003 <name>
...
```

The numeric prefix is the direct library slot number shown in the UI. Slot
`000` is a real slot, not a sentinel. Numbers do not need to be contiguous.
Browsers should scan slots sequentially and show missing slots as empty, for
example `003: Empty` when slot 3 has no matching folder.

Names after the numeric prefix are user-facing labels. The preferred separator
after the three-digit slot number is a space, as in `000 Init` or `001 Slak`,
but loaders may accept an underscore for compatibility with older generated
folders, as in `000_Init` or `001_Slak`. Spaces inside the displayed name are
valid. The numeric prefix is authoritative for slot order; folders should not
be sorted only by full filename.

For root Kit/Scene-style libraries, `NNN` is direct on disk and maps to the
same firmware library slot number. Do not add or subtract 1 for browser/library
slot identity. This is separate from instrument file voice coordinates, which
remain one-based `1..6` inside instrument text schemas.

### Canonical eight-character repair

Before root Kit/Scene/Bank index publication, the blocking index wrapper repairs
the selected namespace, rescans the physical parent, and writes `.hcindex` from
the repaired scan. Instrument boot indexing repairs each registry type before
its typed scan. Bank Load performs selected-Bank child repair as an internal
preflight and then continues into payload loading under the original callback.

Canonical physical components are:

```text
root numbered directory  NNN <up to 8 display characters>
Bank-local Scene         SS <up to 8 display characters>
typed Instrument         <up to 8 stem characters>.<extension>
```

Repair owns only one old/new candidate and one suffix retry. It closes iterator
ownership, renames one component, calls `afatfs_sync()`, and rescans from disk.
Trailing fixed-width display padding is trimmed when building FAT components.
When the canonical target collides, decimal suffix digits replace the shortest
possible tail inside the eight-character display stem.

`Instrument/<type>/.hctmp.<ext>` is reserved product scratch and is ignored by
both repair and typed index insertion.

`/.hcrepair` roll-forward was planned but is not implemented. Current repair is
ordered rename/sync behavior, not a journal or power-loss recovery protocol.
Root Kit and selected Bank embedded-Kit quarantine additionally reject trees
whose `kitset.kcg` references cannot be validated/opened. Root Scene blocking
quarantine was removed and must be redesigned as a foreground-pumped operation
before reintroduction.

## Bank

Status: implemented as a 16-resident-Scene Bank workspace. It has selected
child save/load and a staged root-Bank promotion flow; that explicit Save flow
is not a crash-recoverable transaction. The separate hidden A/B scalar
AutoSave record is specified in `AUTOSAVE.md`.

`Bank/` contains bank folders:

```text
Bank/
  000 <bank name>/
  001 <bank name>/
```

A bank represents all non-global data loaded as one performance set. The active
bank number is recorded in current `settings.cfg`; the bank display name is not
the persistent selector.

Each bank folder contains exactly one bank-level config file:

```text
bankset.bcg
```

`bankset.bcg` stores bank-level metadata/configuration. It also acts as the
validator, guard, and version marker for identifying a folder as a bank. A
folder without a valid `bankset.bcg` must not be loaded as a bank.

Each bank folder also contains up to 16 scene folders:

```text
Bank/000 <bank name>/
  bankset.bcg
  00 <scene name>/
  01 <scene name>/
  ...
  15 <scene name>/
```

Scene slot numbers inside a bank do not need to be contiguous. Bank-local
Scene folders use direct two-digit slots `00..15` for the 16 resident bank
scenes. This is intentionally different from root `Scene/NNN` library folders.
Missing scene slots are valid and will be shown as empty in the future UI. A
user may exchange scene folders between banks.

`bankset.bcg` v2:

```text
format=helicase.bankset
version=2
active_scene=0
scene_mask_voice_edit=0x0001
```

`active_scene` is a Bank-local `00..15` slot number and is not zero-padded in
the file. The Bank name is never stored in `bankset.bcg`; it comes only from
the `Bank/NNN <bank name>/` directory.

Current behavior:

- Boot scans `Bank/` and loads BankData's restored slot (default 000) before
  root Scene/root Kit fallback.
- Bank Load applies the v2 active Scene and edit mask, then loads only children
  selected by the caller and present in the two-digit local namespace. An empty
  requested/present intersection loads no Scene and never falls back to all
  present children. Unselected resident payloads, HCNAMES blocks, and presence
  bits remain unchanged.
- An empty Bank containing only valid `bankset.bcg` is valid; it completes Bank
  selection and then falls back to root Scene, root Kit, then defaults.
- Bank Save writes bankset.bcg and every child selected by the 16-bit mask.
  If the active Scene is outside a nonempty save mask, the saved manifest
  selects the first saved child so it never points to an absent payload.
- Save builds a non-numbered temporary sibling, preflights temp/old-name
  collisions, renames any previous numbered Bank to a non-loadable old
  sibling, and promotes the completed temp directory to the numbered name.
  Promotion failure reports BProm. This prevents in-place stale-tree merges
  but is not a durable journal/recovery transaction.
- A full Bank Load resets Scene child discovery for every delegated child.
  Filenames discovered in one local folder must never be reused for another.
- Bank Load retains no 16-child name or alias arrays. It keeps only a 16-bit
  occupancy mask, rescans the selected Bank parent for each requested child,
  resolves one lexical `SS Name` component into operation scratch, stages that
  child, and discards the component before advancing.
- Bank Load borrows the HCNAMES image once, overlays exactly eight name/source
  pairs for each successfully committed selected child, updates the Bank row on successful
  metadata commit, and writes once. A child counts as loaded only after the
  shared Scene reader validates and commits it, not when its directory merely
  opens. The completed result reaches Preset/Menu before any later filesystem
  request resets operation scratch; after DSP apply, Menu reloads the unchanged
  `/Bank/.hcindex` read-only.

## Scene

Status: root Scene Load/Save is implemented and promoted. Sixteen resident
Scenes are allocated for Bank workspace use; root Scene remains an explicit
numbered library/import-export pool.

`Scene/` is a root-level pool of user-copyable scene folders:

```text
Scene/
  000 <scene name>/
  001 <scene name>/
```

Scene folders in this pool can be loaded into a bank scene slot. They use the
same folder structure as scene folders inside a bank. Root `Scene/` is a
library/pool like root `Kit/` and root `Instrument/`: explicit Scene Save writes
there, explicit Scene Load imports from there, and root Scene files are not
autosaved.

A scene folder contains:

```text
sceneset.scg
Kit <kit name>/
pattern.pat
effects.fx
```

`sceneset.scg` stores scene-level metadata/configuration and validates the
folder as a scene. Current v1 Scene settings include global/per-voice Morph
values, `voice_decimation_all`, seven MIDI channel/note values, and the
Scene-owned per-voice mix settings `audio_out[6]`, `fx_send_amount[6]`, and
`fader_setting[6]`.

`Kit <kit name>/` is the scene's embedded kit directory. It works like a kit
folder but is named without a numeric slot prefix because it belongs to the
scene. The word after `Kit` is the kit name. The kit name is not stored in
`kitset.kcg`, `sceneset.scg`, or any other metadata field.

`pattern.pat` is currently one of two accepted text shapes plus one controlled
legacy import:

- thin v1 text placeholder;
- v2 text import, from which only the final 128-character on/off field of each
  track is retained;
- compact v3 text writer/reader format; legacy binary bridge payloads are
  rejected rather than decoded.

The v1 placeholder:

```text
format=helicase.pattern
version=1
placeholder=1
```

The thin placeholder means the staged PatternSet uses PatternData's empty
bridge defaults.

The legacy v2 import payload:

```text
format=helicase.pattern
version=2
track1=<length>,<scale>,<128 active bits>
...
track7=<length>,<scale>,<128 active bits>
```

Only the final on/off bit field is imported for each of 128 steps on each of
seven tracks. The former per-track `length` and `scale` prefixes are discarded.
Velocity, note, probability, automation, rotation, shuffle, next-pattern,
change-bar, and the main-step shadow are not retained.

The current v3 writer and reader payload is:

```text
format=helicase.pattern
version=3
track1=<32 hexadecimal characters>
...
track7=<32 hexadecimal characters>
```

Each row is the literal sixteen bytes of one `PatternSet` track bitmap in
ascending byte order. Bit zero of each byte is the earlier chronological step.
All seven rows are required. The resulting file represents exactly 112 bytes
of persistent on/off Pattern state and no timing or per-step metadata.

`effects.fx` currently stores a guarded placeholder until real effect storage
exists.

Current `scene_t` ownership:

- `scene_settings_t settings`
- `PatternSet pattern`
- `kit_t kit`

Current `scene_settings_t` fields:

- `morph_amount`
- `voice_morph_amount[INSTRUMENT_SLOT_COUNT]`
- `voice_decimation_all`
- `midi_channel[NUM_TRACKS]`
- `midi_note[NUM_TRACKS]`
- `audio_out[INSTRUMENT_SLOT_COUNT]`
- `fx_send_amount[INSTRUMENT_SLOT_COUNT]`
- `fader_setting[INSTRUMENT_SLOT_COUNT]`

Scene file work stores scene-level metadata and settings in `sceneset.scg`.
These do not belong in `kitset.kcg` or instrument files.

## Kit

Status: root Kit folder load and new-format Kit save are implemented on the
Session 036 asyncfatfs LFN/case foundation.

`Kit/` is a root-level pool of numbered kit folders:

```text
Kit/
  000 <kit name>/
  001 <kit name>/
```

Kit folders can be loaded into the active scene and saved from the active Scene
kit. Slot numbers do not need to be contiguous, and missing slots are shown as
empty in the UI. Root Kit slots are addressed as direct `000..999` on disk and
in firmware library-slot state.

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

Concrete current test-card example:

```text
SD_CARD/
  Kit/
    000 Init/
      kitset.kcg
      ...
    001 Slak/
      kitset.kcg
      slakd1.drm
      slakd2.drm
      slakd3.drm
      slaks1.snr
      slakc1.cym
      slakh1.hat
```

`kitset.kcg` is the kit folder guard/version file plus the six-slot instrument
manifest. The kit name comes only from the folder name:

- Root kit pool: `Kit/NNN <kit name>/` where `NNN` is direct `000..999`
- Scene embedded kit: `Kit <kit name>/`

The kit name is never stored inside `kitset.kcg`.

Users should not copy instrument files into a kit folder manually. Users may
copy instrument files out of a kit folder into the root `Instrument/` pool.
Kit membership is controlled by `kitset.kcg`.

Initial instrument file types:

```text
.drm  drum
.snr  snare
.cym  cymbal
.hat  hi-hat
```

These correspond to the four existing original LXR instrument types. Additional
instrument types may be added later.

Implemented Kit save behavior: saving a Kit writes a folder in this same shape:
`kitset.kcg` plus six descriptor-keyed instrument files. Session 036 adds
asyncfatfs LFN component creation/object iteration, so firmware-created Kit
folders and member instrument files preserve display spaces and mixed case
through VFAT LFN entries while returning generated 8.3 aliases for existing
open paths and `kitset.kcg` references.

### `kitset.kcg`

`kitset.kcg` owns only:

- Format/version validation.
- Slot membership.
- Per-slot instrument type.
- Per-slot instrument filename.

Example:

```text
format=helicase.kitset
version=1

[slot1]
type=drm
file=slakd1.drm

[slot2]
type=drm
file=slakd2.drm

[slot3]
type=drm
file=slakd3.drm

[slot4]
type=snr
file=slaks1.snr

[slot5]
type=cym
file=slakc1.cym

[slot6]
type=hat
file=slakh1.hat
```

Required top-level fields:

- `format=helicase.kitset`
- `version=1`

Required per-slot fields:

- `[slot1]` through `[slot6]`
- `type=drm|snr|cym|hat`
- `file=<8.3 instrument filename>`

Validation rules:

- All six slots must be present.
- Every slot must declare type and file.
- File extension must match declared type: `.drm`, `.snr`, `.cym`, or `.hat`.
- Legacy `audio_out=<0..5>` lines may still be parsed as compatibility side
  data. Scene Load imports them only when loading an embedded Kit inside an old
  Scene folder whose `sceneset.scg` lacks an `audio_out` line. Root Kit Load
  ignores them and preserves current Scene routing.

`kitset.kcg` does not own:

- Kit name.
- Pattern data.
- MIDI channel or MIDI note.
- Scene settings.
- Per-voice audio routing, FX send amount, or fader mode.
- `voice_decimation_all`.
- Instrument parameter values.
- Instrument morph endpoint values.

## Instrument Files

Status: implemented for Kit-folder files, root `Instrument/` pool load, and
root Instrument Save.

Instrument files are text key/value files with a fixed header and one or two
parameter sections. Kit Save member files and root Instrument Save use the same
schema and the same `storage_formatInstrumentLine()` writer.

Example:

```text
format=helicase.instrument
version=1
type=drm

[params]
osc1_wave=0
osc1_pitch_coarse=31
osc1_pitch_fine=126
instrument_vol=127
instrument_pan=63

[morph]
osc1_wave=0
osc1_pitch_coarse=31
osc1_pitch_fine=126
instrument_vol=127
instrument_pan=63
```

Required top-level fields:

- `format=helicase.instrument`
- `version=1`
- `type=drm|snr|cym|hat`

Section rules:

- `[params]` contains the main endpoint.
- `[morph]` contains the morph endpoint for morphable descriptor rows.
- Missing `[morph]` is allowed; the loader copies main values into morph values
  for descriptors flagged morphable.
- Unknown keys are skipped for forward compatibility.
- Known parameter and target rows parse as `uint8_t`. Target tokens use the
  compact byte selector domain; they are not packed 16-bit parameter IDs.
- `lfo_target_voice` and `lfo_target_voice_2` are menu/runtime destination
  selectors. Voices `1..6` select voice slots and the special display value
  `scn` selects the Scene modulation target namespace. The associated
  parameter value is a compact token: a local descriptor index, a Scene
  target index when the voice is scn, or 0xff for off. Runtime code resolves
  that token to a wide descriptor/Scene identity only at the apply boundary.
- `self` is accepted only for `lfo_target_voice` and `lfo_target_voice_2`.
  It is a storage-only relocation alias resolved by the parser with
  `storage_instrument_state_t.expected_slot` before writing Scene-owned
  descriptor images. It is never a Menu value, SceneData sentinel, packed
  wide parameter ID, or DSP runtime value.
- New save code must emit `self` only when the numeric LFO voice selector
  equals the source instrument's own one-based slot. Explicit cross-slot
  modulation remains a decimal voice number.

Instrument file metadata deliberately does not include:

- `slot`
- `kit_name`
- `source_name`
- `source_file`

The slot comes from `kitset.kcg`. The kit name comes from the kit folder.

The converter provides legacy compatibility by regenerating text files from
legacy `.SND` payloads. Storage keeps aliases for the prior HiHat decay text
keys because those names were shipped before the canonical Choke convention.

Legacy flat morph kit loads (`FS_FILE_MORPH`) remain legacy `.SND`.
`Load:[KitMrp]` and nested InstrumentMrp use new-format Kit/Instrument text
payloads for loading normal source endpoints into morph endpoints.
`Save:[KitMrp]` and nested InstrumentMrp Save also use the new-format text
payloads; their Morph Save projection writes the current interpolated value
into both normal and morph endpoint fields.

## Canonical Instrument Keys

The physical SD-card key vocabulary lives in each instrument descriptor table:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

Current descriptor counts:

- Drum: 39 descriptors.
- Snare: 38 descriptors.
- Cymbal: 39 descriptors.
- HiHat: 39 descriptors.

Descriptor key lookup is type-local. The same key may exist in multiple
instrument types but resolves against the loaded slot type.

HiHat is the first Choke instrument. Its visible closed-hat row is canonical
`amp_envelope_decay`; its alternate track-7 row is
`amp_envelope_decay_choke`. The old `_closed` and `_open` spellings are load
aliases only and must not be emitted by new conversion/save code. A Choke
instrument may expose any number of `<base>_choke` descriptors. When it is in
slot 6, VOICE7 substitutes each available sibling for its base descriptor;
those siblings remain separate normal modulation targets.

Current keys by family:

- Oscillator and noise: `osc1_wave`, `osc1_pitch_coarse`,
  `osc1_pitch_fine`, `osc2_wave`, `osc2_pitch_coarse`,
  `osc2_mod_amount`, `osc2_mod_type`, `osc3_wave`,
  `osc3_pitch_coarse`, `osc3_mod_amount`, `noise_freq`,
  `osc1_noise_mix`.
- Filter: `filter_freq`, `filter_reso`, `filter_drive`, `filter_type`.
- Amp envelope: `amp_envelope_attack`, `amp_envelope_decay`,
  `amp_envelope_decay_choke`, `amp_envelope_slope`, `amp_attack_repeat`.
- Pitch envelope: `pitch_envelope_decay`, `pitch_envelope_amount`,
  `pitch_envelope_slope`.
- Voice: `instrument_vol`, `instrument_pan`, `instrument_drive`,
  `instrument_decimation`.
- LFO: `lfo_rate`, `lfo_amount`, `lfo_amount_2`, `lfo_wave`,
  `lfo_polarity`, `lfo_retrigger_voice`, `lfo_sync`, `lfo_offset`,
  `lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`,
  `lfo_target_param_2`.
- Velocity: `velo_vol_on_off`, `velo_mod_amount`, `velo_mod_dest`.
- Transient: `transient_wave`, `transient_vol`, `transient_freq`.

## Descriptor Ownership

`ParamDescriptor` is the source of meaning for an instrument parameter:

- SD-card key.
- Short menu label.
- Long edit label.
- Category label.
- Display dtype.
- Capability flags.
- Runtime binding kind and offset/type payload.

The descriptor arrays live next to each instrument implementation. They own
instrument-local meaning. `InstrumentManager` is the registry/lookup layer, not
the owner of parameter text or per-instrument page layout.

The registry also owns immutable load-policy metadata, not serialized file
data: Basic types may appear without limit, Advanced types are limited to two
per Kit, and Choke types opt into slot-6 VOICE7 `_choke` substitution. Current
types are Drum/Basic, Snare/Basic, Cymbal/Advanced, and HiHat/Advanced|Choke.

Descriptor flags:

- `INSTRUMENT_PARAM_FLAG_MORPHABLE`: the descriptor has main and morph endpoint
  values and participates in the morph worker.
- `INSTRUMENT_PARAM_FLAG_MODULATABLE`: the descriptor is allowed in
  velocity/LFO target pickers.
- `INSTRUMENT_PARAM_FLAG_AUTOMATABLE`: the descriptor is allowed in step
  automation target pickers.

Normal `ROW` and `ROW_MENU` descriptors use `FLAGS_IMAGE`:

```c
INSTRUMENT_PARAM_FLAG_MORPHABLE |
INSTRUMENT_PARAM_FLAG_MODULATABLE |
INSTRUMENT_PARAM_FLAG_AUTOMATABLE
```

`ROW_NOBIND` descriptors have `flags=0` and are single-endpoint supplemental
values. Target selectors remain `ROW_NOBIND`.

`ROW_NOBIND_IMAGE` is used for image parameters that are
morphable/modulatable/automatable but do not write through an instrument-struct
offset. `ROW_SLOT_DECIMATION` is an explicit wrapper around this pattern so the
voice-local `instrument_decimation` row advertises its morph/mod/automation
contract at the descriptor site without creating a second flagging system:

- `instrument_decimation`
- `velo_mod_amount`

These apply through binding kinds instead:

- `INSTRUMENT_BIND_SLOT_DECIMATION`
- `INSTRUMENT_BIND_VELOCITY_AMOUNT`

## Scene Instrument Storage

Each `scene_t` owns one `kit_t`. Each `kit_t` owns six
`kit_instrument_slot_t` records.

`scene_t` and `kit_t` deliberately retain no Bank, Scene, Kit, Instrument,
filename, or stem text. Their only contents are playable settings, PatternSet,
Instrument types, and parameter images. HCNAMES owns display identity; the
immediate filesystem operation derives a component from an identity/index stem,
slot context, and registry extension.

`storage_kitset_t` still carries six LFN-sized `file=` components while one
Kit/Scene manifest is actively parsed. That 312-byte parser object is bounded
operation state needed to open the six files named by that on-card manifest; it
is not copied into the resident Scene and is not a browser/key cache.

`kit_settings_t` owns generated Kit-level values, including the non-Choke
slot-6 track-7 alternate-decay main and Morph endpoints.

Each instrument slot owns:

- `type`
- `parameter_images.instrument_parameters[64]`
- `parameter_images.morph_instrument_parameters[64]`
- `parameter_images.morph_interpolation[64]`

Arrays are indexed by descriptor index for the slot's current instrument type.
Descriptor index `0` is valid; empty menu cells use `INSTRUMENT_MENU_EMPTY`
(`0xff`) and skip cells use `INSTRUMENT_MENU_SKIP` (`0xfe`).

Canonical wide instrument parameter IDs, used only for lookup/runtime
resolution rather than resident target storage:

```c
id = slot * INSTRUMENT_PARAM_COUNT + descriptor_index
```

Current bounds:

- `INSTRUMENT_SLOT_COUNT`: 6.
- `INSTRUMENT_PARAM_COUNT`: 64.
- Voice parameter IDs: `0..383`.
- Scene modulation IDs start at `INSTRUMENT_VOICE_ID_COUNT` (`384`) and
  currently occupy `384..390` for `1vm..6vm` plus Scene Decimation `srt`.
- Remaining higher IDs remain reserved for later FX/general parameter address
  space.

`morph_interpolation[]` is runtime-derived state and is not serialized.

Resident parameter values are `instrument_param_value_t` bytes. Resident
target selections are `instrument_target_token_t` bytes, with
`INSTRUMENT_TARGET_TOKEN_OFF` equal to 0xff. Local target values are compact
indices, LFO voice selection uses self/voice/Scene values, and storage/menu
code expands a token only when resolving or displaying it. A saved Scene/Kit
must not store the wide ID above as a target token.

## Current Kit Load Path

Boot library/index and initial-load path:

1. `scene_initAll()` writes valid default Instrument types into every retained
   Scene slot; `bank_init()` establishes the empty/default Bank identity.
2. `dsp_init()` calls `instrumentManager_runtimeInit()` only after those type
   records exist, so a zero-valued raw BSS slot is never mistaken for a valid
   Drum assignment.
3. `filesystem_initCardAndMountBlocking()` mounts the card.
4. `filesystem_requestScanKits()`, `filesystem_requestScanScenes()`, and
   `filesystem_requestScanBanks()` scan the three numbered root libraries.
5. Each root index wrapper repairs its namespace, performs any Kit quarantine,
   rescans, and writes `/Kit/.hcindex`, `/Scene/.hcindex`, or `/Bank/.hcindex`
   with all 1,000 slot rows preserved.
6. `filesystem_createBootIndexBlocking()` repairs every registry-owned
   Instrument namespace, then scans and writes each typed `.hcindex` one type
   at a time.
7. Boot reloads `/Bank/.hcindex` and checks BankData's restore coordinate
   (default slot 000 on cold initialization). If that row is absent, it loads
   the Scene or Kit index before requesting the existing fallback ladder.
8. `filesystem_loadKitDirectory_tick()` opens the cached kit folder, parses
   `kitset.kcg`, resets slots in `fs_stage_workspace.kit_stage`, then parses
   each listed instrument file into that staged descriptor-indexed storage.
9. After every file validates, filesystem copies the complete staged Kit into
   each selected Scene. It does not replace PatternData or Scene settings.
10. Completion callback sets `PRESET_OP_KIT_LOAD`.
11. `menu_pollPresetStatus()` starts sound apply.
12. Before audio starts, `menu_startSoundApply()` calls
   `preset_sendDrumsetParameters()` synchronously to clear the outgoing graph
   and reset/image-apply every active Scene slot.
13. After `audioCodec_init()`, `main.c` starts the complete ordinary deferred
   Scene worker. It repeats the live Scene-switch clear/image sequence and
   performs the all-source LFO/velocity rebind only after every tagged runtime
   member has its final type.
14. Normal boot does not call the resident-name snapshot writer. Existing
    HCNAMES rows survive unless a successful load operation owned and updated
    them.

Runtime kit loads use the same Scene-owned apply logic, but the post-load apply
is chunked through `preset_startDrumsetApply()` /
`preset_tickDrumsetApply()` to avoid foreground bursts after audio is running.
The worker remains active through its final all-source rebind; reaching a zero
slot-pending mask does not by itself mean Scene activation is complete.

Current production boot also contains four pre-audio SD pacing boundaries:
250 ms before `SD_init()`, 1 ms between ACMD41 attempts with a one-second
timeout, 50 ms after mount before the first library scan, and 50 ms after index
generation before Bank reload/load. These were added after one intermittent
warm-boot report. They are boot-only timing policy, not asyncfatfs/runtime
pacing, and did not establish a reproducible root cause or verified fix. If a
hang recurs, localize its blocking stage without adding further blind delays.

## Current Root Instrument Load Transaction

Root Instrument Load is one explicit Scene, slot, type, and browser-index
request. Filesystem copies the selected typed-index key into immutable
operation scratch, validates the file into the one Instrument stage, and
publishes its display stem through the active identity row only after success.
Asynchronous parsing never resets or alters the live destination Scene slot.

For an inactive destination Scene, Preset commits the slot image only. For the
active Scene, the ordered transaction is:

1. clear all six current LFO target pairs and velocity targets while outgoing
   Scene slot types still resolve their old runtime nodes;
2. copy the staged slot into SceneData and publish name identity separately;
3. reset only the new type's runtime instance and apply the loaded slot route;
4. rebuild all six retained descriptor Morph/runtime images;
5. normalize each source's LFO voice/parameter pairs and velocity target, then
   reinstall all six source relationships;
6. release Menu controls.

The all-source pass is required because any source may target the replaced
slot. Accepted Preset request coordinates remain immutable across filesystem
read, commit, Morph rebuild, and rebind. Menu may still accept number-only
encoder movement and coalesce the newest desired pool row while an older
request drains; Scene, voice, type, and mode changes remain session boundaries.
Filesystem remains single-operation, and completion must use the accepted
operation tag rather than the mutable cursor.

Normal Instrument Load entry serializes the current voice to the hidden typed
file `.hctmp.<ext>` and retains one nine-byte original label. The synthetic
`kit` row sits above pool `000`. Returning to it loads the exact hidden
component through the normal candidate stage and ordered Preset apply. Negative
turns already clamped at `kit` are no-ops, and crossing to `kit` cancels any
obsolete deferred pool retry. Voice/type/mode/Scene/exit invalidates the SRAM
session; the dirty hidden file may remain on SD but is never indexed or
authoritative.

## Runtime Apply Path

Loaded or edited descriptor values are applied through Preset and
InstrumentManager:

- Menu instrument-cell edits call `preset_setInstrumentParameter()` when the
  descriptor is morphable.
- Non-morphable cells call `preset_setSupplementalParameter()`.
- `presetMorph_tick()` calls `preset_applyInstrumentRuntimeValue()` for each
  morphable descriptor.
- `preset_applyInstrumentRuntimeValue()` resolves the slot type and descriptor
  and calls `instrumentManager_writeRuntime()`.
- `instrumentManager_writeRuntime()` applies either a runtime instance offset
  or a supplemental binding kind.

Runtime writer coverage added in Session 032:

- Oscillator coarse/fine rows update `OscInfo.midiFreq` high/low bytes and call
  `osc_recalcFreq()`.
- Snare `noise_freq` writes the noise oscillator frequency.
- Filter frequency/resonance/drive/type use the old value shapers/setters.
- Filter type preserves the old `value + 1` rule so DSP type `0` remains off.
- Amp envelope attack/decay/slope use envelope setters.
- HiHat base/Choke decay use `slopeEg2_calcDecay()`.
- Pitch envelope decay/slope/amount use the existing pitch-envelope semantics.
- Transient waveform/frequency use transient setter/old pitch formula.
- Instrument drive uses `setDistortionShape()`.
- LFO rate uses `lfo_setFreq()`.
- Decimation writes `mixer_decimation_rate[slot]` through the old taper.
- Velocity amount writes `velocityModulators[slot].amount`.
- Simple linear fields still use the generic offset writer.

## Voice Menu Pages

Static non-voice pages still use `Core/Menu/menuPages.h`.

Voice pages are now dynamic descriptor cells:

- `VOICE1_PAGE` through `VOICE6_PAGE` resolve the descriptor layout for the
  instrument type currently assigned to that logical slot.
- `VOICE7_PAGE` remains the alternate trigger/menu view for slot 6.
- For a slot-6 Choke type, VOICE7 replaces a displayed base descriptor with its
  available `_choke` sibling.
- For a non-Choke slot-6 type with `amp_envelope_decay`, VOICE7 exposes the
  generated Scene Kit alternate-decay setting; without that descriptor it uses
  the ordinary slot-6 page.

The menu resolver produces a `menu_cell_t`:

- `MENU_CELL_STATIC`
- `MENU_CELL_INSTRUMENT`
- `MENU_CELL_EMPTY`

Instrument cells carry:

- slot
- descriptor index
- descriptor pointer

Display text comes from the descriptor:

- normal view: `short_name`
- edit view: `category` and `long_name`
- dtype: `descriptor->dtype`

Values come from Scene storage:

- normal voice mode: `instrument_parameters[]`
- `SHIFT+VOICE` morph endpoint view: `morph_instrument_parameters[]`

The layouts are stored in instrument files as `instrument_menu_page_t` arrays.
They use instrument-local enum names such as `DRUM_PARAM_OSC1_PITCH_FINE`, not
raw descriptor numbers and not voice-instance-specific names.

Non-instrument cells from old voice pages, such as track MIDI channel/note,
pattern length, or audio output, are not forced into instrument files. They
remain owned by Menu/Scene/Pattern areas.

## Morph, Modulation, and Automation

Morph values are user-facing 0..255 parameters. Menu edits and future file
storage should preserve that 0..255 range. MIDI CC and step automation are
7-bit input paths; they need explicit conversion into the morph range:

- Input `0..126` maps to `value * 2`.
- Input `127` maps to `255`, so the endpoint is reachable.

Current descriptor Morph state after Session 033:

- Instrument files can carry `[morph]` endpoint values.
- Missing `[morph]` copies main endpoint values into morph endpoint values.
- Scene instrument slots store main endpoint, morph endpoint, and derived
  interpolation images.
- The Morph worker runs against Scene-owned descriptor images and applies one
  descriptor per foreground pass.
- The worker uses the active slot's current instrument type and descriptor
  table, so instrument swapping remains dynamic and the Morph engine does not
  own hardcoded parameter lists.
- Per-voice Morph amounts live in `scene_settings_t.voice_morph_amount[6]`.
- PERF shows two four-cell screens: `mrp 1vm 2vm 3vm` and `4vm 5vm 6vm srt`.
- Setting global `mrp` bulk-sets all six per-voice Morph values.
- Setting `srt` controls Scene/global decimation and defaults to `127` when
  Scene state has no explicit value yet.
- Per-voice Morph is the actual Morph-engine control. Global Morph is only a
  convenience set operation.

Target selection stores compact byte tokens, not wide descriptor IDs or legacy
`modTargets[]` indices. Target display/resolution helpers enumerate active
Scene descriptors and convert the token to a wide ID only at that boundary.

Current working target state:

- Off targets use `INSTRUMENT_TARGET_TOKEN_OFF` (`0xff`).
- Target menu display expands compact local or Scene tokens to names only for
  presentation; SceneData stores the byte token.
- InstrumentManager validates resolved targets by descriptor/Scene flags.
- The velocity target picker is self-scoped: it offers one `off`, modulatable
  descriptors for the source voice's current Instrument, and the source-voice
  Morph token 0x40 where applicable. It does not browse arbitrary other voices
  or the general Scene namespace.
- The LFO target picker shows self, voice destinations `1..6`, and `scn`. For
  a voice destination, the compact parameter picker shows modulatable local
  descriptors; for scn it uses the Scene modulation target token domain.
- The parameter picker skips non-modulatable descriptor rows. It does not show
  repeated `off` placeholders for skipped rows.
- If the selected target voice changes and the previous target parameter is not
  valid for the new destination, the parameter resets to `off`.

Current LFO shape:

- Each voice owns one LFO oscillator/configuration.
- That LFO has two target selector pairs and two amounts:
  `lfo_target_voice/lfo_target_param/lfo_amount` and
  `lfo_target_voice_2/lfo_target_param_2/lfo_amount_2`.
- `lfo_polarity` is shared by both target pairs and displays only `neg`, `pos`,
  and `bi`.
- Negative polarity applies original-LXR value-relative math in descriptor
  parameter space: `base * (1 - amount + amount * source)` for zero-based
  domains. It does not subtract a full raw runtime range.
- Positive polarity applies upward from the base/default value.
- Bipolar polarity applies equally around the base/default value where the
  destination range allows it.
- LFO VOICE menu short pages are:
  `frq snc wav ofs`, `rtg pol am1 am2`, and `vo1 ds1 vo2 ds2`.

Current velocity modulation behavior:

- Direct descriptor targets write through the descriptor-aware runtime path.
- Voice-local `instrument_decimation` is a descriptor target using the special
  `INSTRUMENT_BIND_SLOT_DECIMATION` binding.
- Scene per-voice Morph targets are retained set operations on
  `voice_morph_amount[slot]`, scaled by velocity and amount, and update the
  PERF menu value.
- Scene Decimation is a retained set operation on `voice_decimation_all` and
  updates the PERF menu value.

Current LFO modulation behavior:

- Direct descriptor targets install InstrumentManager adapters, not raw
  runtime pointers. The adapter captures the Scene/Morph base descriptor value
  and descriptor modulation domain, shapes the temporary parameter-domain value,
  and applies it through `instrumentManager_writeRuntime()` so envelopes,
  filters, pitch, transient, distortion, and LFO-rate writers keep their normal
  scaling and side effects.
- Voice-local `instrument_decimation` uses the special supplemental binding.
- Per-voice Morph LFO modulation is a hidden secondary layer centered around
  the retained per-voice Morph base value. It does not move the PERF menu value.
- Multiple LFO sources targeting the same voice Morph sum their signed deltas
  around the retained base value, then clamp to `0..255`.
- The Morph worker adds one extra foreground pass for each voice whose Morph is
  currently LFO-modulated. It still interpolates one descriptor per pass rather
  than trying to recalculate a whole voice immediately.
- Scene Decimation LFO modulation is runtime-only; it does not move the retained
  `voice_decimation_all` menu value.

Current target limitations:

- `AutomationNode` still plays back by emitting legacy MIDI CC/CC2 through
  `midiParser_ccHandler()`.
- `preset_applyInstrumentRuntimeValueInternal()` currently ignores its
  `recordAutomation` argument.
- `seq_recordAutomation()` still accepts/narrows destination as `uint8_t`.

Therefore, the remaining descriptor target follow-up is step automation:
`AutomationNode`, step target storage, automation recording, and automation
display must preserve descriptor/Scene target IDs and apply through the same
descriptor-aware runtime routes.

The main remaining modulation correctness follow-up is dynamic owner enumeration
for non-adapter paths: `modNode_resetTargets()` and
`modNode_directOriginalValueChanged()` still enumerate fixed global nodes rather
than all dynamic runtime-pool sources. Descriptor LFO targets no longer use the
raw runtime-float backend, but any future direct backend must not write shaped
DSP members such as `SlopeEg2.decay` for byte-domain descriptors.

## Pattern

Status: current pattern load/save is a bridge shape; final storage is deferred
to the dynamic stack Pattern implementation.

`Pattern/` is a root-level pool of pattern files:

```text
Pattern/
  <pattern name>.pat
```

Files are browsed alphanumerically. A pattern file can be loaded into a scene.
Users may copy a scene's `pattern.pat` into this pool, and may copy a pool
pattern into a scene if they rename it to `pattern.pat`.

Current bridge notes:

- Live `NUM_PATTERN` is 1.
- Each resident Scene owns exactly one 112-byte `PatternSet` bitmap; no `Step`,
  automation, length, scale, rotation, shuffle, note, probability, or velocity
  Pattern storage remains.
- v3 files serialize the seven literal bitmap rows. The old single global
  shuffle and per-track timing extensions are ignored/omitted.
- Final interchange migration/backfill should happen in external converters
  once the final Pattern storage shape settles.

## Sample

Status: legacy sample/loop install path exists; this target root pool naming is
part of the future typed layout.

`Sample/` contains an alphanumerically sorted list of `.wav` files to write to
flash:

```text
Sample/
  <sample name>.wav
```

Samples play from normal oscillators. Looping is an oscillator-level option,
not a directory-level distinction.

## Wavetable

Status: settled target, not implemented.

`Wavetable/` contains numbered wavetable folders:

```text
Wavetable/
  000 <wavetable name>/
  001 <wavetable name>/
```

Each wavetable folder contains an alphanumerically sorted set of `.wav` files:

```text
Wavetable/000 <wavetable name>/
  <sample a>.wav
  <sample b>.wav
  <sample c>.wav
```

Wavetables are loaded during the sample-load process and written to flash. They
behave like normal samples in storage, but are only read by wavetable
oscillators. A wavetable oscillator operates on one wavetable at a time and can
be modulated across all samples inside that wavetable. Wavetable samples always
play looped. The menu shows the wavetable name when selecting the wavetable
used by the oscillator.

## Effect

Status: settled target, not implemented.

`Effect/` is a root-level pool of effect files:

```text
Effect/
  <effect name>.fx
```

Files are browsed alphanumerically. An effect file can be loaded into a scene.
Users may copy a scene's `effects.fx` into this pool, and may copy a pool
effect into a scene if they rename it to `effects.fx`.

Scene `effects.fx` stores the scene's effect settings and effect automation
sequence. Effects and effect file formats are future DSP work.

## Instrument

Status: root browser, one-slot load, Instrument Morph Load, and standalone
root Instrument Save are implemented.

`Instrument/` is a root-level pool of instrument files:

```text
Instrument/
  <instrument name>.<type>
```

Files are browsed alphanumerically by type when loading into a kit in a scene.
Users may copy instrument files from a kit folder into this pool, or save one
resident voice to the pool from nested Save-page Instrument Save. Users should
not copy files from this pool directly into a kit folder; kit membership is
controlled by `kitset.kcg`.

Entering Instrument Load on a voice shows the current Kit membership stem.
Changing the type row is a non-destructive filter/policy operation. Moving the
lower row selects one root pool file and immediately starts the staged
transaction described above. Basic/Advanced assignment policy is defined by
the firmware registry, never by file contents.

Entering Instrument Save on a voice from the Save page shows the source slot's
current type and an eight-character editable stem. OK writes the selected
resident Scene/voice to `Instrument/<stem.ext>`, where the extension comes from
the source slot type. The source Scene, source voice slot, type, and visible
target filename are captured when the request is accepted so later UI movement
cannot retarget an in-flight save.

Initial recognized instrument types:

```text
.drm
.snr
.cym
.hat
```

## Current Load/Save Menu Reachability

Status retained through the Session 047 baseline:

- `Load:[Kit     ]`, `Load:[KitMrp  ]`, `Load:[Scene   ]`, and
  `Load:[Bank    ]` are promoted top-level entries.
- `Save:[Kit     ]`, `Save:[KitMrp  ]`, `Save:[Scene   ]`, and
  `Save:[Bank    ]` are promoted top-level entries.
- `Load:[File]`, `Save:[File]`, `Load:[Dir]`, `Save:[Dir]`, and
  `Save:[sDir]` are retired. Their compatibility calls return no result and
  cannot allocate/rebuild the removed diagnostic caches, even in Dev Mode.
  Two 49-byte Menu compatibility strings plus nine bytes of result-screen
  scalars remain linked but unreachable (107 bytes total); they are residual UI
  state, not filesystem list caches or musical identity.
- Scene and Bank load/save use explicit OK/OW confirmation. They do not
  live-load on scroll.
- An accepted OK/OW request changes the confirmation field to `...`, suppresses
  every cursor/underline, and locks input until all filesystem, DSP, and
  operation-specific terminal work completes. Completion always resets to the
  bracketed top-level type row and restores `ok` or `OW`. Preparatory index and
  Bank-child preview work may use the storage gate but never displays `...`.
- Load:Bank entry continues a successful `/Bank/.hcindex` load directly into
  a child preview of the unchanged highlighted Bank. The preview holds the
  input gate until its physical `00..15` mask is published, preventing an
  actionable zero-mask OK state.
- Kit and KitMrp keep live-on-scroll load behavior.
- VOICE press on the Load page enters nested Instrument Load.
- VOICE press on the Save page enters nested Instrument Save/InstrumentMrp Save.
- Root Kit, Scene, and Bank browser names use one shared 1,000-row SRAM cache.
  Save completion performs a physical rescan and durable `.hcindex` rebuild
  before returning control to Menu; entry/type changes dispose and reload that
  cache as described in the name-index section above.
- Root Scene and Bank Load do not use that Save rebuild. After payload/HCNAMES
  completion and shared DSP activation, Menu performs one read-only reload of
  the unchanged selected root index and only then terminates the command.
- Combined Kit/Instrument entry first borrows HCNAMES for one Scene's Kit plus
  six Instrument identity rows, then replaces the cache with the requested
  index. Normal actions mark rows dirty; leaving the family performs at most
  one HCNAMES rewrite.

Still compiled but intentionally gated from the normal type cycler:

- `Settings` / Globals
- `Samples`
- Pattern
- All
- Performance
- legacy Morph

Do not widen the type cycler by enum order. Promote one operation at a time
after retesting it on the Session 036 asyncfatfs foundation.

## Save Operations

Implemented:

- Kit save writes a `Kit/<NNN Name>/` folder in the same logical shape the
  current loader accepts: `kitset.kcg` plus six instrument files. The folder and
  member files are created through asyncfatfs LFN primitives, with returned 8.3
  aliases used for `kitset.kcg` references/open paths.
- Instrument save writes one resident Scene/voice slot to the root
  `Instrument/<type>/` pool. It creates/opens the typed registry directory with
  LFN/case-sensitive asyncfatfs APIs, opens the target display filename with
  `afatfs_fopen_lfn()`, streams the descriptor-keyed instrument schema, and
  updates the active typed browser cache without retaining a per-type array.
- KitMrp and InstrumentMrp Save use the same text schemas as normal saves, but
  for morphable parameters they write the current per-voice interpolated value
  into both `[params]` and `[morph]`. This is a flattened snapshot of the
  current morph position, not an inverted endpoint pair. Morph Save does not
  rename the resident kit or instruments.
- Scene Save writes a root `Scene/<NNN Name>/` directory. The writer streams
  `sceneset.scg`, creates `Kit <kit name>/`, streams embedded `kitset.kcg`
  without `audio_out`, writes six embedded Instrument files, writes draft text
  `pattern.pat`, and writes placeholder `effects.fx`. Scene and embedded Kit
  names are directory-owned.
- Bank Save writes bankset.bcg version 2 and one local `SS <scene name>/`
  payload for every selected Scene bit. A zero child-scene mask is valid and
  creates an empty Bank. The completed payload is written to a unique
  non-numbered temporary Bank sibling before promotion to the numbered slot.
- After Kit, root Scene, or root Bank Save completes its physical directory
  write and final FAT flush, firmware rescans that parent directory and
  rewrites the complete slot-ordered `.hcindex`. The original Save completion
  callback is delayed until this rebuild is durable; the Save menu then
  refreshes the current slot's displayed name from the active shared cache.
- Normal Kit and Instrument Save also update their active identity row(s) and
  mark the combined name session dirty; HCNAMES is rewritten once at the
  family boundary. Morph saves preserve resident identity. Scene and Bank
  operations own their targeted/full-register HCNAMES transaction as part of
  their completion chain.

Still future:

- Pattern save writes the final dynamic-stack pattern format once implemented.
- Effect save writes the selected effect stack/settings format once effects
  exist.
- `settings.cfg` save writes the strict allowlisted system/global settings and
  active Bank number. There is no current .settings.cfg backer.

The current legacy non-Kit save paths are implementation leftovers and should
not be used as the new-format specification.

### Save/Overwrite Safety

Root library replacement must be scoped by parent directory and product parser:

- Enter the correct root directory first, such as `/Kit/`, `/Scene/`, or
  `/Bank/`.
- Scan only immediate child objects in that parent.
- Parse visible display names with the correct parser:
  - root libraries use three-digit `NNN <name>`;
  - Bank-local child Scenes use two-digit `SS <name>`.
- Delete or replace only physical objects whose parsed slot equals the target
  slot.
- Never run a recursive delete from the filesystem root using a broad target
  string.

Kit Save may use short-alias fallback for older/converted Kit folders. Scene
Save deliberately disables short-alias fallback and deletes only visible names
that parse as the exact Scene slot, preventing the root Scene wipe class of
bug. Bank-local selection uses storage_parseBankSceneFolder and carries the
captured afatfsObjectId_t to native deletion, avoiding a second ambiguous LFN
lookup. Bank Save promotes a complete temporary root tree; it does not claim
to preserve unselected old children across a replacement.

### asyncfatfs Boundary

The low-level asyncfatfs API, LFN/SFN alias rules, object iteration behavior,
filename sanitization, and caller checklist live in
`ASYNCFATFS_REFERENCE.md`. This product-level spec assumes callers go through
`filesystem.c` or those documented asyncfatfs primitives instead of rebuilding
FAT/VFAT traversal locally.

Current production replacement captures the selected object from an
LFN-aware scan and requests native `afatfs_deleteTree()` for same-slot cleanup.
That low-level recursive-delete path is not yet reliable enough to guarantee
replacement: an overwrite Save may leave the old Bank, root Scene, or Kit
folder in place. The product contract is therefore deliberately stronger than
the present implementation; repair the native deleter rather than adding an
`old*` rename/boot-cleanup workaround. No current path has an atomic or
crash-recoverable replace primitive, so none may claim power-loss-safe commit
semantics.

## Verification Anchors

Use these as smoke tests when changing filesystem, descriptor storage, or
instrument runtime propagation:

- Build with `make` and package with `make img` when an image is needed.
- Boot with `SD_CARD/Kit/001 Slak`.
- Confirm the Kit scan shows the folder-derived kit name.
- Confirm `kitset.kcg` slot type/file/audio routing is honored.
- Confirm canonical HiHat keys `amp_envelope_decay` and
  `amp_envelope_decay_choke` parse; legacy `_closed/_open` aliases remain
  compatible input only.
- Confirm VOICE pages populate from active instrument descriptors.
- Confirm Slak file values are visible on VOICE pages.
- Confirm loaded voices produce audio.
- Confirm editing `instrument_vol`, filter frequency, envelope decay, and
  waveform changes sound immediately.
- Confirm Morph reaches both endpoints for a simple audible descriptor.
- Confirm LFO and velocity targets show one `off`, skip non-modulatable
  descriptors, and apply to direct descriptor, voice-local decimation, and
  Scene targets.
- Confirm Instrument Load starts at `kit <stem>`, does not load while changing
  type, loads immediately only from lower-row pool movement, and respects the
  two-Advanced limit.
- Confirm Instrument entry writes `.hctmp.<ext>`, neither repair nor
  `.hcindex` publishes it, decrementing `000 -> kit` restores the original
  parameters/name, repeated negative detents remain clamped, and a rapid
  backspin still permits a later positive move into the pool.
- Confirm an accepted Instrument transaction keeps immutable Scene/voice/type
  coordinates while number-only cursor movement coalesces the newest desired
  pool row; Scene, VOICE, type, and mode boundaries must invalidate the
  temporary session rather than retarget accepted work.
- Confirm Kit Save creates/opens the target Kit folder, writes `kitset.kcg`,
  writes six instrument files with one header, one `[params]`, and one `[morph]`
  section each, and can be loaded again.
- Confirm saved LFO self-targets emit `self` only on LFO voice selector keys
  whose numeric value equals the source slot.
- Confirm Kit slots `000`, above 255, and `999` display and save/load without
  wrapping or off-by-one mapping.
- Confirm boot creates or refreshes `/Kit/.hcindex`, `/Scene/.hcindex`,
  `/Bank/.hcindex`, and each registry-owned Instrument `.hcindex` after the
  appropriate repair pass. Confirm normal boot does not overwrite existing
  HCNAMES rows from a partial resident snapshot.
- Confirm entering each top-level Kit, Scene, and Bank Load/Save type reloads
  only its selected index into the one shared 9,000-byte name cache. Confirm
  combined Kit/Instrument entry borrows one HCNAMES block, payload parsing uses
  the independent 2,048-byte stage, and dirty family exit performs one
  preserve/overlay/rewrite.
- Confirm entering Load:Bank without turning the initially highlighted number
  chains index completion into its selected-Bank child preview, gates input
  until the physical mask is resident, and never submits a zero-mask request.
- Confirm explicit root Scene/Bank Load ordering is payload -> HCNAMES ->
  completed Preset result -> shared Scene DSP clear/image/all-source rebind ->
  read-only root-index reload -> command reset. No root scan or index write may
  occur in a pure Load.
- Confirm an accepted OK/OW command displays `...` with no cursor through its
  complete terminal work and always returns to the bracketed type row.
- Confirm a Kit, Scene, or Bank Save makes a new or renamed directory visible
  immediately after its directory rescan and `.hcindex` rewrite, without a
  restart; the current Save slot display must also refresh.
- Confirm root Bank boot reloads `/Bank/.hcindex` after Instrument index
  generation and checks BankData's restore slot before fallback.
- Confirm mask-selective Bank Load changes only the selected/present child
  payload and its eight HCNAMES rows. A mask with no present child must not load
  all children or erase unselected resident names.
- Confirm the cold-boot selected Scene and a manual Scene return construct the
  same six runtime types/images and install both LFO target pairs plus velocity
  only after all incoming tagged members are valid.
- Confirm LFO negative polarity on envelope decay follows the visible parameter
  direction and amount scale through the descriptor writer.
- Treat step automation as pending until the descriptor-aware AutomationNode
  pass is complete.

## AutoSave boundary

Status: the hidden A/B scalar writer is implemented in the Session 047
baseline. Its complete format, ownership, scheduling, power-loss behavior,
bounded CRC contract, duplicate rules, and extension process live only in
`AUTOSAVE.md`.

The obsolete per-Instrument/Scene dot-backer proposal formerly in this section
was never implemented and is removed to prevent two competing AutoSave
specifications. Current firmware writes only the root `/.hcprms1` and
`/.hcprms2` records; it does not create `.sceneset.scg`, `.kitset.kcg`,
`.pattern.pat`, `.effects.fx`, `.bankset.bcg`, or `.settings.cfg` backers.
Explicit Bank/Scene/Kit/Instrument Load and Save continue to use the ordinary
product objects specified above.

Whole-object Load/Save/copy publication, winner replay into resident state,
Pattern/Effect persistence, and crash-recoverable promotion into explicit Bank
files remain future AutoSave work. They must be added through `AUTOSAVE.md`, not
by reviving the removed dot-backer design.

## Example Target Layout

```text
settings.cfg
Bank/
  000 Factory/
    bankset.bcg
    00 Breakbeat/
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
      effects.fx
Scene/
  000 Loose Jam/
    sceneset.scg
    Kit Loose/
      kitset.kcg
      ...
    pattern.pat
    effects.fx
Kit/
  000 909ish/
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
  000 Vowels/
    a.wav
    e.wav
    i.wav
Effect/
  short_room.fx
Instrument/
  909kik.drm
  snap.snr
```
