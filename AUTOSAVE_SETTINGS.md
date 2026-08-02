# Autosave settings and source provenance

## Status and scope

Implementation completed in source on 2026-08-02. Static and firmware-build
verification has passed; the hardware behavior listed in the verification plan
remains for device testing. The requested `SD_CARD/settings.cfg` fixture has
also been updated to the implemented schema. No settings format version was
changed or added.

The plan is based on the current code in:

- `main.c`
- `config.h`
- `Core/Bank/BankData.c` and `.h`
- `Core/Bank/Scene/SceneData.c` and `.h`
- `Core/Bank/Scene/Autosave.c` and `.h`
- `Core/Bank/Scene/Preset/ParameterArray.h`
- `Core/Bank/Scene/Preset/presetManager.c` and `.h`
- `Core/Hardware/SD/filesystem.c` and `.h`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Menu/menu.c`, `menu.h`, `menuPages.h`, and `MenuText.h`

This pass has four outcomes:

1. `settings.cfg` remains the persistent owner of the last successfully loaded
   or saved root Bank slot, and an asynchronous settings write is requested
   after every successful Bank Load or Bank Save.
2. Sixteen two-byte Scene-source values are retained in SRAM and serialized in
   `settings.cfg`. Successful root Scene and Bank Load/Save operations update
   only the resident Scenes they actually sourced.
3. a changed setting committed from the Global settings page requests a
   trailing one-second debounced `settings.cfg` write;
4. a new Global `ats` / `AutoSave` on/off setting, default ON, gates every
   autosave-record read and write without deleting or rewriting either hidden
   record while disabled.

This pass does not implement Scene reload-from-source, Pattern provenance,
Kit/Instrument provenance, an atomic replacement transaction for
`settings.cfg`, or Pass 2 of `AUTOSAVE_PARAM_HOOK.md` beyond the lifecycle
coordination explicitly identified below.

## Findings from the current code

### `settings.cfg`

`filesystem_loadGlobals_tick()` currently resets a fixed allowlist of Global
values and `bank_restoreBankSlot()`, then parses `settings.cfg`. The keyed text
schema is `format=helicase.settings`, `version=1`, `active_bank`, followed by
thirteen Global values. Unknown keys are deliberately ignored. Missing files
successfully retain defaults.

`filesystem_saveGlobals_tick()` currently opens `settings.cfg` with `"w"`,
streams the allowlisted lines, closes the file, and completes only after the
shared filesystem flush gate. It is only started by explicit Settings Save;
there is no background settings writer.

The existing writer truncates the live file in place. This plan deliberately
reuses it and does not add a temporary-file/rename transaction. That existing
power-loss characteristic must be recorded as an explicit non-goal rather than
silently mistaken for atomic settings persistence.

### Boot ordering defect relevant to this feature

The comments before initial Bank selection say `settings.cfg` has already been
loaded, but the actual code does the opposite: `main.c` scans/indexes the card,
chooses `bank_restoreBankSlot()`, loads the Bank/fallback, and only then calls
`preset_loadGlobals()` at diagnostic stage 13.

That order currently makes `active_bank` too late to choose the boot Bank. It
would also make `AutoSave=off` too late to prevent boot-time inspection or
creation of `/.hcprms1` and `/.hcprms2`. The settings load must therefore move
to immediately after successful mount, `menu_init()`, and the existing
post-mount settle, before any initial Bank selection and before autosave setup.

### Successful operation boundaries

The filesystem Bank state machines update `bank_restoreBankSlot()` inside
their private phases. Those phases are not the public completion boundary:
later HCNAMES/index/flush work can still fail. The reliable hooks are the four
Preset filesystem completion callbacks, where `filesystem_status()` still
reports `FS_STATUS_DONE` or `FS_STATUS_ERROR`:

- `on_scene_load_complete()`;
- `on_scene_save_complete()`;
- `on_bank_load_complete()`;
- `on_bank_save_complete()`.

Root Scene Load already retains its destination mask in
`pm_kit_request_scene_mask`. Scene Save retains the target library slot but
does not currently retain its resident source Scene for the callback. Bank
Load retains the requested mask, but only `filesystem.c` knows the final mask
after intersecting it with children actually present in the selected Bank.
Bank Save passes a mask to the filesystem but does not retain that mask in
Preset for completion.

### Current autosave authorization

`filesystem_ensureAutosaveFilesBlocking()` is the only setup/authorization
boundary. It disables mutation production, creates missing records for a
resident Bank, then enables tracking and arms one recovery pass. The idle
scheduler thereafter validates/reads and drains records. There is currently no
user preference gate and no asynchronous runtime setup path.

## Exact persistent schema

Keep `settings.cfg` at `version=1`. The new firmware supplies defaults for
missing new keys, while the existing unknown-key behavior lets older firmware
ignore appended keys. There is no incompatible reinterpretation that requires
a format version bump.

Append these keys to the existing writer:

```text
autosave=1
scene_source_00=65535
scene_source_01=65535
...
scene_source_15=65535
```

`autosave` accepts only `0` or `1`; absent means `1`.

Each Scene source is one `uint16_t` with this complete encoding:

```text
0..999       root Scene library slot `/Scene/NNN ...`
1000..1999   root Bank slot `/Bank/NNN ...`
65535        unknown / no recorded source
```

For a Bank source, the child Scene number is implicit: resident Scene `S` came
from or was saved as Bank child `S`. This matches the current Bank loader and
writer, which map selected bit `S` to the same `00..15` child coordinate. No
additional source-kind byte or child-index byte is required.

Values `2000..65534` are invalid. A recognized `scene_source_NN` key with a
malformed suffix or invalid value must fail the settings parse like other
recognized malformed settings rather than silently inventing provenance.
Missing keys and the missing-file case remain valid and produce `65535`.

The resulting writer has 33 logical lines: format, version, active Bank,
thirteen existing Global values, `autosave`, and sixteen Scene sources.

## Required invariants

1. The Scene-source allocation is exactly `16 * sizeof(uint16_t) = 32` bytes.
2. No parallel Scene-source type array exists.
3. The AutoSave preference occupies one logical byte in the existing
   `parameter_values[NUM_PARAMS]` allocation and defaults to one.
4. Provenance changes never set an `.hcprms` mutation bit; provenance is
   metadata in `settings.cfg`, not Bank payload data.
5. Failed or rejected Load/Save operations never change a source value and
   never request a settings write.
6. Partial root Scene operations update their accepted destination mask only.
7. Partial Bank Load updates the final loaded-child mask only, not merely the
   originally requested mask.
8. Partial Bank Save updates the successfully saved selected mask only.
9. A successful empty-Bank Load updates `active_bank`, but updates no Scene
   source.
10. All settings writes remain asynchronous after audio startup.
11. A one-second debounce is trailing: each later settings change restarts the
    deadline so a burst coalesces into one file write.
12. A setting changed while `settings.cfg` is already being streamed cannot be
    lost; it causes a second complete write after the active write finishes.
13. AutoSave OFF prevents every new autosave ensure, validation, recovery,
    mutation-drain, CRC, and commit operation.
14. AutoSave OFF never deletes, truncates, regenerates, or otherwise opens
    `/.hcprms1` or `/.hcprms2` for its own transition.
15. A record transaction already open when the user turns AutoSave OFF is
    allowed to reach its existing close/flush boundary. Aborting an AsyncFATFS
    write mid-transaction or clearing its canonical mask underneath the copy
    would risk corruption. Mutation production stops immediately, mask
    disposal is deferred to that transaction's completion callback, and the
    disabled state suppresses all continuation/recovery/setup work after that
    boundary.
16. Re-enabling AutoSave is asynchronous at runtime and re-establishes the
    current resident Bank session before ordinary mutation tracking resumes.
17. `settings.cfg` persistence remains active when AutoSave is OFF; the toggle
    itself and later Global/source changes still need to be saved.

## Detailed code-change plan

### 1. `config.h` — define the settings debounce

Add `SETTINGS_AUTOWRITE_DEBOUNCE_MS 1000u` beside the existing autosave timing
configuration, with a full adjacent comment block.

What it does: supplies the one-second trailing delay used by the background
settings scheduler.

Why it must exist: the delay is policy, not a filesystem state-machine magic
number, and must remain independently tunable from the five-second `.hcprms`
debounce and 250 ms continuation interval.

Inputs: the millisecond `time_sysTick` captured whenever settings become
dirty. Output: the next eligible settings-save deadline.

Affiliates: `filesystem_markSettingsDirty()`, the new settings scheduler in
`filesystem_tick()`, and a compile-time assertion in `filesystem.c` that the
interval is nonzero and below the half-range of the wrapping 16-bit tick.

### 2. `Core/Bank/Scene/Preset/ParameterArray.h` — add the AutoSave value

Append `PAR_AUTOSAVE_ENABLED` after `PAR_VOICE_DECIMATION_ALL` and before the
fixed `NUM_PARAMS = 384` value. Add an adjacent ownership/default/persistence
comment.

What it does: allocates one existing flat parameter byte for the Global
AutoSave on/off value without renumbering any established parameter id.

Why it must exist: Menu dtypes, text pages, settings parsing, and runtime policy
need one shared logical setting. Appending it preserves all earlier enum values
and consumes no additional `parameter_values[]` BSS because that array is
already fixed at 384 bytes.

Inputs: settings default/load and Global-page edits. Outputs: a normalized
zero/one value consulted by the filesystem lifecycle.

Affiliates: `menu.c`, `menuPages.h`, and settings parsing/formatting in
`filesystem.c`.

Add a compile-time bound assertion where the current parameter-table checks
live so future enum growth cannot silently cross `NUM_PARAMS`.

### 3. `Core/Menu/menu.h`, `MenuText.h`, `menu.c`, and `menuPages.h` — expose
`ats` / Global / `AutoSave`

#### `Core/Menu/menu.h`

Append `TEXT_AUTOSAVE`, `LONG_AUTOSAVE`, and `SHORT_AUTOSAVE` to their existing
text enums, with an adjacent comment tying all three to
`PAR_AUTOSAVE_ENABLED`.

What it does: gives the new cell stable indices into the three parallel text
tables.

Why it must exist: the menu renderer indexes these tables by enum value; raw
strings in `menuPages.h` would bypass the existing menu contract.

Inputs: the Global page's text id. Outputs: the appropriate short/category/long
lookup coordinate.

Affiliates: the parallel arrays in `MenuText.h` and `valueNames[]` in `menu.c`.

#### `Core/Menu/MenuText.h`

Append `"ats"` to `shortNames[]` and `"AutoSave"` to `longNames[]`. Add an
adjacent comment recording the exact requested spelling and the Global
category ownership supplied by `valueNames[]`.

What it does: renders the requested three-character page label and long edit
label.

Why it must exist: both normal page rendering and the single-parameter edit
view consume these tables.

Inputs: `SHORT_AUTOSAVE` and `LONG_AUTOSAVE`. Outputs: flash-resident display
text only.

Affiliates: `menu.h` and `menu.c`.

#### `Core/Menu/menu.c`

Add `{SHORT_AUTOSAVE, CAT_GLOBAL, LONG_AUTOSAVE}` to `valueNames[]`, add
`[PAR_AUTOSAVE_ENABLED] = DTYPE_ON_OFF` to `parameter_dtypes[]`, and set
`parameter_values[PAR_AUTOSAVE_ENABLED] = 1u` in `menu_init()`.

What it does: selects the requested category and on/off display/editor, and
ensures no-card/default startup also begins with AutoSave logically enabled.

Why it must exist: `filesystem_resetSettingsToDefaults()` runs only during a
settings-file load, while `menu_init()` owns the base `parameter_values[]`
state on every boot including no-card boots.

Inputs: new text/parameter ids. Outputs: menu metadata and the cold default.

Affiliates: `menu_getParameterDisplayValue()`, static menu-cell resolution, and
the settings default overlay.

Add an explicit `PAR_AUTOSAVE_ENABLED` no-DSP case to
`menu_parseGlobalParam()`. The case documents that filesystem lifecycle is
applied only at the user-commit or boot boundary below, not from
`menu_sendAllGlobals()`. This prevents a settings load from unexpectedly
starting autosave file work while boot or a manual Settings Load still owns
the filesystem.

Extend the static branch of `menu_cellCommitValue()` to compare the old and
normalized final byte. After `menu_sendEditedParameter()`, when and only when
the value changed and `menu_activePage == MENU_MIDI_PAGE`:

1. call `filesystem_markSettingsDirty()` to restart the one-second debounce;
2. for `PAR_AUTOSAVE_ENABLED`, call
   `filesystem_setAutosaveEnabled(value)` to apply the runtime policy.

Keep ordinary UI repaint/commit behavior unchanged even when a clamped edit
lands on the old value; only persistence and lifecycle hooks require a real
change.

What this does: captures both encoder and endless-pot Global edits at the
single static-cell commit boundary.

Why it must exist: scattering persistence calls through every Global
parameter's DSP case would miss future Global cells and would also run during
bulk settings apply. The page check makes future Global parameters inherit
persistence automatically while excluding PERF, Pattern, and other static
cells.

Inputs: static cell parameter id, old byte, normalized final byte, active page.
Outputs: one debounced settings-dirty event and, for the toggle, one immediate
in-memory autosave policy transition.

Affiliates: encoder/knob edit paths that already converge in
`menu_cellCommitValue()`, `filesystem_markSettingsDirty()`, and
`filesystem_setAutosaveEnabled()`.

#### `Core/Menu/menuPages.h`

Replace the first empty cell after `PAR_RUNTIME_CPU_USE` on the second Global
row with `TEXT_AUTOSAVE` / `PAR_AUTOSAVE_ENABLED`. Keep the final empty cell at
the end of that row.

What it does: exposes AutoSave on the existing Global settings page.

Why it must exist: this table is the actual page layout. The new cell must be
inserted before the first terminator because current traversal treats an early
`TEXT_EMPTY/PAR_NONE` pair as the end of reachable settings.

Inputs: text and parameter ids. Outputs: a selectable `ats` cell in category
Global.

Affiliates: the existing warning comment above `MENU_MIDI_PAGE` and the static
cell resolver in `menu.c`.

### 4. `Core/Bank/Scene/SceneData.h` and `.c` — own exactly 32 bytes of Scene
source SRAM

#### `Core/Bank/Scene/SceneData.h`

Define named source constants without exposing storage-file arithmetic to
callers:

```text
SCENE_SOURCE_LIBRARY_BASE = 0
SCENE_SOURCE_BANK_BASE    = 1000
SCENE_SOURCE_LIMIT        = 2000
SCENE_SOURCE_UNKNOWN      = UINT16_MAX
```

Declare bounded APIs to:

- reset all sources to unknown;
- set one Scene from a root Scene library slot;
- set one Scene from a root Bank slot;
- read one encoded source value;
- optionally decode the kind and underlying `0..999` slot for future reload UI.

What this does: centralizes the 2,000-value encoding and Scene bounds.

Why it must exist: Preset completion and settings parsing must not duplicate
the Bank-base addition, sentinel handling, or `SCENE_COUNT` checks.

Inputs: resident Scene index and a root library/Bank slot. Outputs: one encoded
two-byte source or the unknown sentinel.

Affiliates: `storageTypes.h`'s two 1,000-slot maxima, Preset callbacks, and the
settings parser/writer.

#### `Core/Bank/Scene/SceneData.c`

Add one file-static `uint16_t scene_sources[SCENE_COUNT]` and a static assertion
that its size is exactly 32 bytes. Do not add the source to `scene_t` and do not
add a parallel type array.

What it does: retains all sixteen source values after `settings.cfg` closes.

Why it must exist: callers can answer provenance and future reload requests
without reopening the settings file, while keeping the approved allocation
exact and independent of `scene_t` alignment.

Inputs: settings load and successful Scene/Bank completion calls. Outputs:
bounded encoded values returned by accessors and serialized on settings save.

Affiliates: `scene_initAll()`, `filesystem_resetSettingsToDefaults()`, and
`filesystem_nextSettingsLine()`.

Initialize all entries to `SCENE_SOURCE_UNKNOWN` from `scene_initAll()` and
from the settings-default reset path. Setters accept only `0..999` source
slots; invalid resident/source coordinates leave storage unchanged. These
metadata setters must not call any `autosave_mark*Dirty()` function.

### 5. `Core/Hardware/SD/filesystem.c` — parse and serialize the expanded
settings schema

Extend `filesystem_resetSettingsToDefaults()` to set AutoSave ON and reset all
Scene sources to unknown in addition to the current Global and active-Bank
defaults.

What it does: gives missing/legacy `settings.cfg` files deterministic new-field
defaults before keyed lines overlay them.

Why it must exist: the parser intentionally allows absent keys and missing
files; neither case may retain stale SRAM provenance from an earlier manual
Settings Load.

Inputs: start of every settings load. Outputs: default parameter/source state.

Affiliates: `scene_resetSources()`, `PAR_AUTOSAVE_ENABLED`, and load phase 0.

Extend `filesystem_sanitizeLoadedGlobals()` to clamp
`PAR_AUTOSAVE_ENABLED` to zero or one. Preserve the current MIDI-channel
sanitization.

What it does: guarantees the runtime policy sees a boolean even if a legacy or
future bulk path supplies another byte.

Why it must exist: the parser is not the only path that can populate the flat
Global array.

Inputs: loaded parameter bytes. Outputs: normalized Global state.

Affiliates: settings parsing and `menu_sendAllGlobals()`.

Add a strict helper that recognizes exactly `scene_source_00` through
`scene_source_15`, parses the decimal value with the existing U16 parser, and
calls the typed SceneData setter/unknown setter. Add `autosave` to the explicit
Global-key allowlist with a maximum value of one.

What it does: overlays the sixteen source values and AutoSave preference from
the keyed file.

Why it must exist: a generated suffix avoids sixteen repeated `strcmp()`
branches while still rejecting malformed recognized keys. Unknown unrelated
keys remain forward-compatible and ignored exactly as today.

Inputs: one parsed key/value line. Outputs: one retained source or Global byte,
or `FS_STATUS_ERROR` for a malformed recognized assignment.

Affiliates: `filesystem_parseSettingsLine()`,
`filesystem_parseSettingsU16()`, and SceneData's source API.

Extend `filesystem_nextSettingsLine()` after the thirteen existing Global
lines. Emit `autosave` first and then `scene_source_00..15` in resident Scene
order. Use a bounded local key formatter/helper rather than a table of sixteen
strings.

What it does: writes the complete live settings/provenance image on every
explicit or automatic settings save.

Why it must exist: the current streaming state machine already owns line
ordering and bounded output; adding another writer would create competing
`settings.cfg` schemas.

Inputs: `op_write_line_index`, the current AutoSave byte, and
`scene_sourceValue(scene)`. Outputs: one complete text line per foreground
tick, ending after line index 32.

Affiliates: `filesystem_saveGlobals_tick()` and the existing text-line helper.

### 6. `Core/Hardware/SD/filesystem.c` and `.h` — add the debounced background
settings writer

Add private scheduler state separate from the `.hcprms` writer:

- a dirty flag;
- a runtime-ready gate;
- a wrapping one-second due tick;
- a monotonically wrapping settings-change revision;
- a transaction-local revision captured when a settings write begins.

Expose and fully document in `filesystem.h`:

```text
filesystem_markSettingsDirty()
filesystem_enableRuntimeSettingsWrites()
```

`filesystem_markSettingsDirty()` sets the dirty flag, advances the revision,
and replaces the deadline with `time_sysTick +
SETTINGS_AUTOWRITE_DEBOUNCE_MS`. It performs no I/O.

What it does: coalesces Bank/Scene provenance and Global settings changes into
one pending complete-file save.

Why it must exist: callbacks and Menu must report changed metadata without
opening a file synchronously or taking ownership of AsyncFATFS.

Inputs: a successful source operation or changed Global menu byte. Outputs:
only small scheduler state.

Affiliates: Preset completion callbacks, `menu_cellCommitValue()`, and the idle
scheduler.

`filesystem_enableRuntimeSettingsWrites()` is called once after the complete
pre-audio load/fallback/autosave-setup ladder. It opens no file. If boot Bank
Load marked settings dirty, it establishes a fresh full one-second deadline so
the file is updated after runtime begins rather than starting a hidden writer
inside boot setup.

Add `filesystem_settingsWriterSchedule_tick()` and call it from
`filesystem_tick()` whenever the facade is idle, before the existing autosave
scheduler. It starts `FS_INTERNAL_OP_SAVE_GLOBALS` with a private completion
callback only when:

- the card is mounted and ready;
- runtime settings writes are enabled;
- settings are dirty;
- the one-second deadline has expired;
- no other facade operation owns the filesystem.

The settings writer does not borrow the name cache and therefore does not need
the autosave writer's Load/Save-page pause. It still starts only from a truly
idle facade, so an accepted foreground Menu/Preset operation always retains
ownership until completion.

What this does: reuses the existing asynchronous settings state machine and
normal final media-flush gate.

Why it must exist: `preset_saveGlobals()` would create a fake user-visible
Preset operation and completion lifecycle. The filesystem is the correct owner
for an invisible autonomous write.

Inputs: dirty/deadline/revision state and the facade status. Outputs: one
ordinary `FS_INTERNAL_OP_SAVE_GLOBALS` transaction.

Affiliates: `filesystem_start()`, `filesystem_saveGlobals_tick()`,
`filesystem_tick()`, and `filesystem_ack()`.

At settings-save phase 0, capture the current change revision. At successful
phase 4, clear the dirty flag only if the revision is still equal to the
captured revision. If a setting changed during streaming, the mismatch leaves
the new dirty event and its new deadline intact for a second full write. On an
automatic-write error, acknowledge the autonomous completion but retain dirty
state and restart a one-second retry deadline. The existing explicit Settings
Save callback remains user-visible; a successful explicit save may clear the
same dirty flag only under the same revision-equality rule.

What this does: prevents a line-by-line write from acknowledging values changed
after their line was already emitted.

Why it must exist: clearing a single dirty boolean unconditionally at close
would lose edits made while AsyncFATFS was still streaming the old snapshot.

Inputs: captured and live revisions plus terminal filesystem status. Outputs:
clean state only for a complete latest snapshot, otherwise a queued retry.

Affiliates: both explicit and autonomous `FS_INTERNAL_OP_SAVE_GLOBALS`
requests.

Initialize/reset this scheduler state in `filesystem_initAfterCardReady()` and
in the boot-log facade recovery reset so stale ownership cannot survive a FAT
facade destruction/remount. Preserve the Scene sources themselves; only a real
settings load resets/overlays those.

### 7. `Core/Hardware/SD/filesystem.c` and `.h` — expose the exact completed
Bank Load mask

Add `filesystem_lastBankLoadSceneMask()` next to
`filesystem_lastBankLoadLoadedScene()`.

What it does: returns `op_bank_scene_load_mask`, after the Bank loader has
intersected the requested mask with actual child presence.

Why it must exist: Preset cannot accurately assign Bank provenance from its
request mask. Missing Bank children must not overwrite the prior source of an
unmodified resident Scene.

Inputs: the just-completed successful Bank Load scratch. Output: a 16-bit mask
of resident Scenes actually committed by that operation; zero for an empty
Bank.

Affiliates: Bank Load phases that form `op_bank_scene_load_mask`,
`on_bank_load_complete()`, and the existing loaded-Scene boolean accessor.

Document that the value is only consumed by the immediate Preset completion
callback before another operation reuses generic scratch.

### 8. `Core/Bank/Scene/Preset/presetManager.c` and `.h` — update provenance
only after successful public completion

Add a private helper that iterates a 16-bit resident Scene mask and calls the
appropriate SceneData root-Scene or Bank-source setter. Add a second small
helper that marks settings dirty once per completed operation, not once per
Scene bit. Keep these private; no new Menu-facing Preset API is required.

#### Root Scene Load

In `on_scene_load_complete()`, when `filesystem_status() == FS_STATUS_DONE`,
set every Scene in `pm_kit_request_scene_mask` to root Scene source
`pm_request_slot`, then mark settings dirty once. Preserve the existing
successful resident-presence update and ordinary Preset completion.

Inputs: accepted root Scene slot and destination mask. Outputs: one encoded
source per destination and one debounced settings event.

Why it must exist: one root Scene can currently fan out to several resident
Scenes, and all successful destinations share that source.

Affiliates: `preset_markRequestedScenesPresentOnSuccessfulLoad()` and
`scene_setSourceLibrarySlot()`.

#### Root Scene Save

When `preset_saveScene()` successfully posts its request, retain
`source_scene` in the already existing `pm_instrument_request_scene` byte for
the asynchronous callback lifetime. In `on_scene_save_complete()`, on DONE,
set only that resident Scene's source to root Scene slot `pm_request_slot` and
mark settings dirty once.

Inputs: target root Scene slot and resident source Scene. Outputs: the saved
resident Scene now points to that root source.

Why it must exist: `pm_request_slot` alone cannot distinguish the library
target from the resident Scene whose provenance changed.

Affiliates: the request function and `scene_setSourceLibrarySlot()`.

#### Bank Load

In `on_bank_load_complete()`, on DONE:

- leave the filesystem's existing `bank_setRestoreBankSlot()` result as the
  single SRAM Bank source owner;
- obtain `filesystem_lastBankLoadSceneMask()`;
- set each bit in that actual mask to Bank source `pm_request_slot`;
- mark settings dirty once even if the mask is zero, because the Bank itself
  loaded successfully.

Inputs: successful root Bank slot and exact committed child mask. Outputs:
`active_bank` plus only the loaded Scenes' provenance become pending for
`settings.cfg`.

Why it must exist: a valid empty Bank and a partial Bank are both successful,
but neither permits applying the originally requested all-Scenes mask to
provenance.

Affiliates: `filesystem_lastBankLoadSceneMask()`, BankData, and
`scene_setSourceBankSlot()`.

#### Bank Save

When `preset_saveBank()` successfully posts its request, retain the submitted
normalized 16-bit `scene_mask` in `pm_kit_request_scene_mask`. In
`on_bank_save_complete()`, on DONE, set every selected resident Scene to Bank
source `pm_request_slot` and mark settings dirty once for both Bank and Scene
metadata.

Inputs: target Bank slot and selected saved-Scene mask. Outputs: `active_bank`
and each saved Scene point at the promoted Bank directory.

Why it must exist: the filesystem maps each selected resident Scene to the
same-number Bank child, but Preset currently discards the mask before its
public completion callback.

Affiliates: `filesystem_requestSaveBank()`, BankData, and SceneData provenance.

Add adjacent comments to the request-state fields in `presetManager.c` and the
relevant public operation declarations in `presetManager.h` explaining that
their immutable coordinates now also drive successful-completion provenance.
Do not mark settings dirty at request acceptance or inside filesystem private
phases.

### 9. `Core/Bank/Scene/Autosave.c` and `.h` — add explicit disable/session
reset helpers

Add a bounded, interrupt-safe `autosave_discardDirtyMask()` lifecycle helper.
It clears the one canonical 3,856-byte SRAM mutation record only when no
autosave transform is consuming it. Because the mutation producer gate is
already off at that point, the implementation may use one bounded clear rather
than holding interrupts off across the whole array. It performs no filesystem
work.

What it does: drops pending autosave work when the user deliberately disables
AutoSave or when a new resident Bank session must replace the old mask owner.

Why it must exist: merely stopping the scheduler would retain dirty bits owned
by the previous enabled interval and could write stale session work after a
later re-enable.

Inputs: an already-disabled mutation producer gate. Outputs: an empty SRAM
mask; both card files remain byte-for-byte untouched.

Affiliates: `autosave_setMutationTrackingEnabled(0)`, filesystem policy
transition, and the single-owner invariant.

Add `autosave_markResidentBankDirty()` as a named lifecycle scope built from
the existing typed Bank-field and Scene-without-Pattern markers. It marks all
live Bank fields and all present resident Scenes after tracking is enabled.

What it does: makes runtime re-enable converge the current complete live Bank
parameter image instead of saving only mutations that happen after the toggle.

Why it must exist: mutations made while AutoSave is OFF are intentionally not
tracked, so re-enable needs one explicit current-session snapshot boundary.

Inputs: BankData's present-Scene mask and current retained data. Outputs: the
existing canonical dirty mask contains all gettable Bank and present-Scene
payload bytes. Names remain governed by the existing HCNAMES/baseline and Bank
identity paths; no second name cache is added.

Affiliates: the existing whole-scope markers, BankData, and the runtime ensure
completion described next.

This helper is also the lifecycle affiliate that later Pass 2 load/session
hooks in `AUTOSAVE_PARAM_HOOK.md` must reuse rather than introducing a second
mask or another Bank-session API.

### 10. `Core/Hardware/SD/filesystem.c` and `.h` — gate and transition all
autosave I/O

Expose a documented `filesystem_setAutosaveEnabled(uint8_t enabled)` policy
entry point and, if useful to keep `main.c` independent of parameter storage,
a read-only `filesystem_autosaveEnabled()` accessor. Add private
`fs_autosave_enabled`, runtime setup-pending state, and a
discard-after-active-transaction flag.

#### Disable transition

On a normalized transition to OFF:

1. publish the disabled preference first;
2. call `autosave_setMutationTrackingEnabled(0)`;
3. clear setup/recovery/armed continuation flags;
4. if no autosave transaction is active, call
   `autosave_discardDirtyMask()` immediately; otherwise set the deferred
   discard flag;
5. do not start, cancel, open, remove, truncate, validate, or regenerate either
   autosave file.

If an autosave operation is already BUSY, let its current safe transaction
finish and let its private completion callback acknowledge it. That callback
must see OFF, discard the canonical mask after the transform no longer consumes
it, clear the deferred flag, and clear all future scheduling instead of
rearming. This prevents a runtime toggle from changing mask bytes while they
are being copied/CRC-covered. Document this transaction-boundary exception
beside the API in both `.c` and `.h`.

#### Enable transition

On a transition to ON:

- if no resident Bank exists, retain the preference but perform no autosave
  file I/O;
- if boot has not reached runtime, let `main.c` use the existing blocking
  pre-audio ensure;
- at runtime with a resident Bank, set an asynchronous setup-pending flag.

Extend the idle scheduler so setup-pending starts the existing
`FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` state machine with a private callback,
never a blocking wrapper. Pause this autosave setup start on LOAD/SAVE pages,
as is already done for the autosave drain and shared name-cache contract.

On successful runtime setup completion:

1. acknowledge the operation;
2. publish autosave boot/session ready;
3. arm the existing one-time file-mask recovery;
4. enable mutation tracking;
5. call `autosave_markResidentBankDirty()` so values changed while disabled
   are captured;
6. allow the ordinary five-second writer debounce to begin.

On setup failure, keep tracking/setup authorization off and leave files as the
existing ensure state machine left them; a later explicit OFF/ON transition or
new Bank session may retry. No tight autonomous retry loop is added.

What this does: applies the user preference at one lifecycle boundary for boot
and runtime.

Why it must exist: checking only the ordinary drain scheduler would still let
boot/runtime ensure or recovery read/create files while the setting says OFF.

Inputs: normalized preference, resident-Bank lifecycle, facade state, and Menu
page. Outputs: either no autosave activity or the existing safe setup/recovery/
drain chain.

Affiliates: `filesystem_ensureAutosaveFilesBlocking()`,
`filesystem_autosaveWriterSchedule_tick()`,
`filesystem_autosaveWriterCompleted()`, `bank_hasResidentBank()`, and
Autosave's tracking/mask APIs.

Add an OFF guard at every autonomous entry boundary, not only the Menu hook:

- the start and authorization portions of
  `filesystem_ensureAutosaveFilesBlocking()`;
- runtime ensure scheduling/completion;
- `filesystem_autosaveWriterSchedule_tick()`;
- `filesystem_autosaveWriterCompleted()` continuation/error branches.

The internal drain state machine does not poll the preference mid-copy; it
finishes the already-started transaction safely, as required by invariant 15.

Reset policy/scheduler flags in normal filesystem initialization and boot-log
facade recovery. The logical default remains supplied by Menu/settings and is
applied again by `main.c` after the settings load.

### 11. `main.c` — load settings before Bank choice and apply the AutoSave boot
gate

Move the existing blocking `preset_loadGlobals()` / `filesystem_tick()` /
`menu_pollPresetStatus()` block from after the Bank/fallback ladder to
immediately after successful mount, Menu initialization, and post-mount settle.

After that load completes, call
`filesystem_setAutosaveEnabled(parameter_values[PAR_AUTOSAVE_ENABLED])` before
any Bank is selected. Do not enable runtime settings writes yet.

What it does: makes `active_bank`, Scene provenance, and AutoSave policy
available before their first consumers.

Why it must exist: the current late load causes Bank slot 000/default SRAM to
drive boot regardless of the saved `active_bank`, and it cannot prevent
boot-time autosave I/O.

Inputs: mounted card and loaded/default `settings.cfg`. Outputs: Global runtime
values applied through the existing `menu_pollPresetStatus()` path, correct
Bank restore slot, restored Scene sources, and an in-memory autosave policy.

Affiliates: `menu_startGlobalApply()`'s pre-audio synchronous branch, Preset
settings completion, and the later Bank selection.

Gate `filesystem_ensureAutosaveFilesBlocking()` on both a resident Bank and
AutoSave ON. The wrapper itself retains its defensive OFF guard.

After the complete Bank/fallback and optional autosave-ensure ladder, call
`filesystem_enableRuntimeSettingsWrites()`. A successful boot Bank Load/Save
callback can therefore mark source settings dirty during boot, but no hidden
settings writer starts until all pre-audio filesystem ownership is released.

What it does: schedules the required post-Bank provenance update without
interleaving it into boot scans/indexes.

Why it must exist: successful boot Bank Load is still a Bank Load and must
eventually update `settings.cfg`, but autonomous runtime work must not race the
blocking boot ladder.

Inputs: retained boot dirty event. Outputs: a new one-second runtime deadline.

Affiliates: Preset Bank completion and the settings scheduler.

Renumber the `boot_showFilesystemStage()` calls and their adjacent diagnostic
comments so the newly early Settings Load has a monotonic unique stage and the
following scan/index/Bank/autosave/pre-audio stages remain interpretable. This
is a diagnostic-label-only change; it must not add any LCD call, filesystem
operation, or delay beyond the Settings Load that already exists and is merely
moved.

On no-card boot, retain Menu's AutoSave default but do not enable a settings or
autosave filesystem scheduler because there is no mounted facade.

## Operation sequencing after implementation

### Boot with AutoSave ON

```text
mount -> menu_init -> load/apply settings.cfg
      -> scans/indexes -> load selected Bank/fallback
      -> successful Bank callback updates SRAM provenance + settings dirty
      -> blocking autosave ensure only for a resident Bank
      -> enable runtime settings writer
      -> after 1 s idle: settings.cfg rewrite
      -> ordinary autosave recovery/drain cadence
```

### Boot with AutoSave OFF

```text
mount -> menu_init -> load/apply settings.cfg (autosave=0)
      -> scans/indexes -> load selected Bank/fallback
      -> successful Bank callback updates SRAM provenance + settings dirty
      -> no .hcprms existence check, open, validation, creation, or write
      -> enable runtime settings writer
      -> after 1 s idle: settings.cfg rewrite only
```

### Successful root Scene operation

```text
filesystem flush completes -> Preset callback sees DONE
    -> update exact resident Scene source(s)
    -> mark settings dirty once / restart 1 s deadline
    -> normal Preset/Menu acknowledgement
    -> background settings save at first eligible idle deadline
```

### Global AutoSave OFF edit

```text
Global static cell changes 1 -> 0
    -> mark settings dirty
    -> disable mutation production and future autosave starts
    -> discard canonical SRAM dirty work immediately when idle,
       or at the active autosave transaction's completion boundary
    -> leave both .hcprms files untouched
    -> finish an already-open transaction only if one existed
    -> save settings.cfg after the 1 s debounce
```

## Verification plan

### Static/build checks

1. Build the firmware with warnings treated exactly as the current Makefile
   does and run `git diff --check`.
2. Verify enum/table alignment for `TEXT_AUTOSAVE`, `SHORT_AUTOSAVE`, and
   `LONG_AUTOSAVE`; verify `PAR_AUTOSAVE_ENABLED < NUM_PARAMS`.
3. Verify the linker map shows exactly 32 bytes for `scene_sources` and no
   second provenance/type allocation.
4. Verify all new `.c` and `.h` changes have adjacent comment blocks describing
   purpose, reason, inputs, outputs/effects, and affiliates, matching the
   documentation convention used by the autosave work.

### Settings schema tests

1. Missing `settings.cfg`: defaults are Bank 000, sixteen unknown sources, and
   AutoSave ON; the next dirty event writes all new keys.
2. Existing version-1 file without new keys: it loads successfully with the
   same new defaults.
3. Full new file: all source encodings and AutoSave state round-trip exactly.
4. Values 999, 1000, 1999, and 65535 round-trip; 2000..65534 and malformed
   recognized suffixes fail closed.
5. Older/unknown unrelated keys remain ignored.

### Bank and Scene provenance tests

1. Put `active_bank` at a nonzero populated slot and verify that exact Bank is
   selected at boot, proving settings load now precedes Bank choice.
2. Root Scene Load into several selected Scenes: all and only selected entries
   become the same `0..999` root Scene value.
3. Root Scene Save: only the resident source Scene becomes the target root
   Scene value.
4. Partial Bank Load with requested children absent: only
   `filesystem_lastBankLoadSceneMask()` bits become `1000 + bank_slot`.
5. Empty Bank Load: `active_bank` updates and no Scene source changes.
6. Partial Bank Save: all and only saved resident Scenes become
   `1000 + bank_slot`.
7. Force each operation to ERROR before public completion and verify neither
   provenance nor settings dirty state changes.
8. Verify source updates do not alter the `.hcprms` canonical mutation mask.

### Debounce/concurrency tests

1. Change one Global setting and verify no settings write starts before 1,000
   ms, then one complete write occurs.
2. Change several Global values inside one second and verify there is one
   write one second after the last change.
3. Complete Bank and Scene operations inside one debounce window and verify one
   file contains all final sources.
4. Change a Global setting while `settings.cfg` is actively streaming; verify
   revision mismatch schedules a second write and the second file image has the
   final value.
5. Verify automatic settings persistence does not create a Preset busy/result
   message and does not block audio in a synchronous loop.
6. Cause an automatic settings-save error and verify the dirty state is
   retained for a delayed retry rather than a tight loop.

### AutoSave gate tests

1. Set `autosave=0`, remove both hidden files, and boot: neither file is
   created.
2. Set `autosave=0` with valid existing hidden files, boot and edit parameters:
   compare complete file hashes before/after and verify no change.
3. While OFF, verify Bank/Scene provenance and Global settings still update
   `settings.cfg`.
4. Toggle OFF while idle and verify no autosave validation/recovery/drain starts.
5. Toggle OFF during an active autosave transaction and verify that transaction
   reaches a valid close/flush, then no continuation starts.
6. Toggle ON with a resident Bank and verify asynchronous ensure/recovery occurs
   without blocking the UI; the current resident Bank is marked for a complete
   live-parameter drain.
7. Toggle ON without a resident Bank and verify no hidden-file I/O occurs.
8. Power-cycle after OFF and verify boot performs no hidden-file reads or
   writes, confirming the setting is applied before autosave setup.

## Acceptance boundary

This side pass is complete when:

- the expanded settings schema round-trips;
- Bank and Scene source changes are recorded only after successful operations;
- the 32-byte Scene source allocation is verified;
- Global edits coalesce through the one-second asynchronous settings writer;
- `ats` appears as Global / `AutoSave`, defaults ON, and persists;
- AutoSave OFF prevents all new hidden-record I/O while leaving the two files
  present and unchanged;
- runtime re-enable safely re-establishes the resident Bank autosave session;
- build/static checks pass;
- implementation notes and hardware results are appended to this document.

No Scene reload-from-source behavior and no other autosave parameter-hook pass
is authorized by this plan.

## Implementation record — 2026-08-02

### Landed source changes

- `config.h` now owns `SETTINGS_AUTOWRITE_DEBOUNCE_MS = 1000`. Its compile-time
  consumers reject zero and wrapping-ambiguous intervals.
- `ParameterArray.h` appends `PAR_AUTOSAVE_ENABLED` without shifting any prior
  parameter ID and asserts that it remains inside the existing fixed
  `NUM_PARAMS` storage.
- the Global page exposes `ats` / `AutoSave` as an on/off cell. Its cold default
  is ON. A changed Global-page value queues settings persistence; changing this
  particular value also applies the hidden-record policy immediately.
- `SceneData` owns one `uint16_t scene_sources[16]` array and typed setters for
  root Scene slots, Bank slots, encoded values, and unknown. Provenance changes
  do not produce `.hcprms` mutation bits.
- the version-1 keyed settings parser/writer now round-trips `autosave` and
  exactly `scene_source_00..15`. The source parser accepts `0..1999` and
  `65535`, rejects reserved values `2000..65534`, and rejects malformed keys
  that use the reserved `scene_source_` prefix. Missing keys retain AutoSave ON
  and unknown Scene-source defaults.
- successful Preset completion callbacks are the only Bank/Scene provenance
  writers. Root Scene operations update their retained resident destination or
  source. Bank Load uses the filesystem's final loaded-child mask; Bank Save
  uses the request-time retained mask. ERROR completions change neither source
  state nor settings dirty state.
- settings persistence uses the existing asynchronous `SAVE_GLOBALS` stream
  and final shared flush. A trailing one-second deadline coalesces edits. A
  monotonically increasing change revision prevents an edit made during the
  stream/flush from being incorrectly acknowledged by the older write. The
  retained settings-write marker survives the switch to the shared flush
  operation, so dirty state clears only at the durable completion boundary.
- automatic settings-save errors retain dirty work and schedule a delayed
  retry. They do not create a Preset result; explicit Settings Save retains its
  existing Preset completion behavior.
- AutoSave OFF disables mutation production and prevents every new hidden-file
  setup, validation, recovery, and drain start. It leaves both files untouched.
  If a transform is already active, that bounded transaction completes before
  the canonical SRAM mask is discarded. AutoSave ON queues asynchronous setup
  only after runtime authorization and only with a resident Bank; successful
  setup marks the current resident Bank for a complete live drain.
- settings load now occurs before Bank selection and before optional boot
  autosave setup. The autonomous settings and AutoSave schedulers are enabled
  only after the complete pre-audio Bank/fallback filesystem ladder releases
  ownership. A manual successful Settings Load applies the loaded AutoSave
  policy at its Preset completion boundary.

Every added or changed `.c`/`.h` behavior above has an adjacent ownership and
operation comment describing its inputs, output/effects, reason, and relevant
affiliates. Settings remains `version=1`; no version migration or alternate
file was added, per direction.

### SD-card fixture

`SD_CARD/settings.cfg` now contains the complete 33-line writer schema. It has
`autosave=1` and `scene_source_00..15=1000`. Encoding `1000` means Bank slot
000, and all sixteen entries are appropriate because the fixture's
`SD_CARD/Bank/000 Full/` contains Bank-local Scene children `00..15`.

### Completed static verification

- `make -j4` completed successfully. Final image size is 367,732 bytes text,
  400 bytes data, and 78,476 bytes BSS.
- `make img` packaged the rebuilt firmware as
  `build/LXRV2_lxr02.img` (368,132 bytes).
- the linker symbol for `scene_sources` is exactly `0x20` / 32 bytes.
- the existing canonical autosave mask remains one `0x0f10` / 3,856-byte
  allocation; no second mutation/provenance mask was introduced.
- `SD_CARD/settings.cfg` contains exactly 33 lines in implemented writer order.
- `git diff --check` reports no whitespace errors.

Hardware tests in the verification sections above remain pending; no hardware
result is claimed by this source implementation record.

### Boot-display follow-up — 2026-08-02

Hardware testing showed the first VOICE parameter page immediately after the
early settings load, before Bank/fallback Scene data had populated the resident
Instrument owners. The source was the synchronous pre-audio branch of
`menu_startGlobalApply()`: settings now correctly loads first, but its ordinary
`repaintAll` request cleared the splash at that earlier boundary.

The pre-audio Global apply now applies settings without repainting. The first
normal parameter repaint remains owned by the existing Bank/Scene/Kit sound
apply, after `preset_sendDrumsetParameters()` has completed; if no sound source
loads, `menu_start()` releases the splash at the normal final boot boundary.
Runtime Settings Load behavior is unchanged, and no filesystem operation,
delay, state allocation, or public interface was added.
