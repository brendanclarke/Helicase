# LXR-02 Open Helicase — Feature Scoping & Implementation Roadmap

*Compiled from `putting it together`, the follow-up architecture discussion, `MEMORY.md`, `README.md`, `BURST_REDUCTION.md`, and a direct read of the current `Core/` source tree. Where a number or struct layout is stated as fact below, it was checked against the actual code (file/line references included). Where something is a genuine open question — usually "how much headroom is actually free in DTCM/ITCM" — it's flagged as such instead of guessed at, because the previous pass through this document invented specific numbers in places the code doesn't support.*

## How this document is organized

The work is grouped into six phases, ordered around the trajectory the code is
actually following after Session 039. Phase 1 is complete foundation cleanup.
Phase 2 landed the first real filesystem/Scene bridge: root Kit directory
loading into descriptor-backed instrument images. Phase 3 now finishes the
directory/text filesystem bridge before the sequencer storage rewrite:
instrument parameter load/runtime coverage, descriptor modulation and
automation, Morph, menu/load-save work, Scene and Bank structures, and
new-format load/save operations. Sessions 033-039 landed the runtime/Morph
path, Instrument Load, Kit/Instrument Morph Load, descriptor-domain LFO repair,
LFO `self` storage, normal and Morph new-format Kit Save, root Instrument and
InstrumentMrp Save, asyncfatfs LFN/case support, direct `000..999` slots,
recursive Kit/Scene slot replacement rules, Load/Save hardware menu repair,
Scene-owned mix settings, root Scene Load/Save, the first root Bank
scan/load/save bridge, Bank-first boot fallback, and draft Scene/Bank
`pattern.pat` persistence. Sessions 040-044 completed the real 16-Scene Bank
workspace, compact bitmap Pattern bridge, bounded identity/cache ownership,
cold-boot tagged-runtime activation, and harmonized root Scene/Bank Load
completion. The remaining Phase 3 emphasis is descriptor-aware automation,
Effect placeholders, and a fresh autosave design; the currently committed
autosave experiment is explicitly rejected and is not a baseline.
Phase 4 is the dynamic stack Pattern implementation that used to be scoped as
Phase 3. Phase 5 is user-facing performance workflow, MIDI cleanup, copy/clear
helpers, and menu controls. Phase 6 is DSP expansion.

Within each phase, features are grouped by **where they live in the codebase**, per your original request, so a given implementation pass touches a small, coherent set of files.

1. **Phase 1 — Foundation Refactors** (no new features; your four listed refactor/reorg tasks, including burst reduction)
2. **Phase 2 — Directory Kit Loading & Descriptor Scene Bridge** (`Core/Hardware/SD/`, `Core/Bank/Scene/`, `Core/DSP/Instruments/`)
3. **Phase 3 — Finish Filesystem, Instrument Runtime, Morph & Menus** (`Core/Bank/Scene/`, `Core/Hardware/SD/`, `Core/Menu/`, `Core/Bank/Scene/Preset/`)
4. **Phase 4 — Dynamic Stack Pattern Implementation** (`Core/Bank/Scene/Pattern/`, dynamic event pool)
5. **Phase 5 — MIDI, UI & Performance Workflow Cleanup** (`Core/Menu/`, `Core/MIDI/`, `Core/Hardware/frontPanel/`)
6. **Phase 6 — DSP Expansion** (`Core/DSPAudio/` — new voices, oscillators, FX bus)

Every phase ends with **Open Engineering Questions** (things that need a decision or a measurement before/during implementation) and **Suggested Complementary Features** (ideas adjacent to what you asked for, flagged clearly as suggestions, not commitments).

---

## Session 043 implementation baseline (2026-07-25)

The storage prerequisites for later Pattern and DSP work are complete and must
be treated as the current baseline, superseding older roadmap estimates:

- Every resident Scene has only a 112-byte `PatternSet` on/off bitmap
  (`7 x 128` bits); v3 `pattern.pat` writes seven 32-hex-character rows. The
  former `Step`/automation/timing Pattern allocations and legacy binary stream
  are retired. This is a deliberately minimal bridge, not the later dynamic
  event-pool Pattern design.
- The slider attenuator LUT is 1,024 native `float` entries (4,096 B), indexed
  with raw ADC `>> 2` and no LUT interpolation. Mixer block smoothing remains
  a separate behavior.
- Six 1,176-B tagged InstrumentManager runtime slots (7,056 B total) replace
  every former per-engine voice/pool allocation. Future instruments must fit
  the reserve; increasing it expands all six slots and requires explicit user
  approval of the SRAM allocation.
- `transientData` is a 26,460-B FLASH ROM at `0x08053264`; DTCM static use is
  now 12,280 B, leaving 118,792 B reserved exclusively for future delay-line
  buffers. SRAM1 static use is 66,780 B, leaving 310,052 B reserved exclusively
  for future Pattern data. Neither reservation is general headroom, and every
  future RAM increase requires the user's byte/region/owner acknowledgement.
- The flash transient placement is build/ELF verified; its full hardware audio
  stress matrix remains pending. The complete measured baseline is in
  `knowledge_files/specification_reference/SRAM_MANIFEST.md` and the durable
  decisions are in Session 043's handoff.

## Session 044 Phase 3 load/runtime baseline (2026-07-28)

- SceneData initializes all retained Instrument types before tagged DSP runtime
  construction. Scene activation clears outgoing destinations, image-applies
  every incoming type, then performs one all-source LFO/velocity rebind. Cold
  boot starts the exact ordinary Scene-switch worker after audio startup, and
  hardware confirmed the initially selected Scene and both reported LFO targets.
- Top-level Load:Bank loads its root index, immediately previews the unchanged
  highlighted Bank's `00..15` children, and gates input until the destination
  mask is valid. The explicit OK request alone enters command state.
- Accepted OK/OW operations display `...`, suppress all cursors, and remain
  locked through their real terminal work before returning to the bracketed
  type row.
- Pure root Scene/Bank Loads commit payload and HCNAMES, apply DSP, then reload
  their unchanged `.hcindex` read-only as the final command step. Only Saves
  that mutate Kit/Scene/Bank namespaces physically rescan/rebuild an index.
- The final linked image uses 12,280 B DTCM and 66,776 B SRAM1. The RAM
  reservation/approval policy is unchanged.
- Four pre-audio SD pacing holds remain in the source after an intermittent
  boot report. The hang is not reproducible or localized; these holds are not
  evidence of a verified root-cause fix.

---

## Phase 1 — Foundation Refactors

**Location:** `Core/MIDI/frontPanelParser.c`, `Core/Sequencer/sequencer.c` → `Core/Bank/Scene/Pattern/`, `Core/Preset/` → `Core/Bank/Scene/Preset/`

**Current status:** completed across Sessions 027-029. Burst reduction,
frontPanelParser removal, PatternData ownership, and the Preset folder move are
landed. This section remains as historical rationale for the order of work.

These are the four refactor/reorg tasks you already flagged before any of the new feature work. None of them change behavior; they change where code lives and how it's called, so that every phase after this one is being built on the layout you actually want rather than being built once and then dragged through a rename later. Do these first, in this order, each as its own compileable/testable commit.

### 1.1 Burst reduction

`BURST_REDUCTION.md` already has the full plan for this — the chunked kit-load apply burst in `menu_pollPresetStatus()`, the parameter-application state machine, and the per-tick budget. Nothing in this document changes that plan; it's called out here only so it stays first in the sequence, because every later phase (bigger scenes, bigger patterns, more parameters) makes an unbounded foreground burst worse, not better. Implement it as written there before touching anything else.

One correction to flag: the intermediate draft of this document (and the "RED TEAM" draft before it) both suggested shrinking `AUDIO_DMA_FRAMES` from 96 to 64 "to buy headroom." That's already been tried — the comment directly above the `#define` in `config.h` says 64 was tested and "reduced slack enough to freeze/glitch during pattern load testing," and 96 is documented as "the measured stable session-023 compromise." Don't re-litigate that without new data; if headroom is still tight after burst reduction lands, the documented path to more slack is enlarging the 4KB `.dma_nocache` MPU window to allow 128, not shrinking to 64.

### 1.2 Remove `frontPanelParser.c`, wire functions directly

This is a bigger job than the earlier draft made it look. The real numbers:

- `frontPanelParser.c` is 816 lines with **70 `case` labels** across three internal `switch(command)` blocks (opcode dispatch for standard front-panel messages, an LFO-target sub-switch, and a sysex path) plus the `frontPanel_sendData()` encoder itself.
- At the time this phase was scoped, `frontPanel_sendData()` was called **105
  times** across the codebase. Session 028 removed that bridge and replaced it
  with direct owner calls.

This was not "remove a file and add a few direct calls" — it was 105 call sites,
each of which packed its payload into `(status, data1, data2)` MIDI-CC-shaped
bytes, sent it through `frontPanel_sendData()`, and relied on the parser's
`switch` to unpack and route it to the real target function (sequencer, DSP
voice, LED handler, modulation node, etc.). Session 028 completed this
mechanical bridge removal. The original practical checklist was:

1. For each of the 70 opcodes, find the `case` body in `frontPanelParser.c` and the function(s) it ultimately calls.
2. At each of the 105 call sites, replace the `frontPanel_sendData(OPCODE, data1, data2)` call with a direct call to that same target function, passing the already-known values instead of re-encoding them into a byte pair and decoding them back out.
3. Delete the opcode's `case` once no call site still routes through it.

This is mechanical but not risk-free — some opcodes fan out to more than one target function, and a few (the sysex path, `LED_QUERY_SEQ_TRACK`, the pattern/euklid "request params" opcodes) exist purely to trigger a *response* back through the same protocol, which won't make sense once the protocol is gone; those need to become direct reads of the relevant state rather than a send/receive round trip. Budget this as file-by-file (start with `presetManager.c`'s 6 call sites as a small dry run, then `sequencer.c`, then `buttonHandler.c`, then `menu.c` last since it's the largest).

### 1.3 Move pattern storage out of `sequencer.c` into `/Pattern/`, `pat_*` prefix

Current real layout, for reference: `sequencer.h` defines `NUM_TRACKS 7`, `NUM_PATTERN 8`, `NUM_STEPS 128`, a 7-byte `Step` struct (`volume`, `prob`, `note`, `param1Nr`, `param1Val`, `param2Nr`, `param2Val`), and the `PatternSet` struct that holds `seq_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS]`, `seq_mainSteps[NUM_PATTERN][NUM_TRACKS]`, `seq_patternSettings[NUM_PATTERN]`, and `seq_patternLengthRotate[NUM_PATTERN][NUM_TRACKS]`. `seq_tick()` (the per-4kHz-tick step advance, `sequencer.c:775`) is called from `TIM3_IRQHandler()` in `sequencerTimer.c:91-102`.

Since Phase 4 is about to replace this entire pattern data model (8 fixed patterns of up to 128 sub-steps → up to 128 real steps per track with no sub-steps, addressed through the dynamic event pool), there's a sequencing question worth deciding explicitly: **do the mechanical move and the data-model rewrite in the same pass, or move first and rewrite in place second?**

Recommendation: move first, rewrite second, as two separate commits. Move everything pattern-storage-and-servicing-related (the structs above, the `Step`/`PatternSet` types, load/save helpers, step-advance logic, euklid/patgen generators) into `Core/Sequencer/Pattern/`, rename the moved functions with the `pat_*` prefix, and get it compiling and behaving identically to before. Then do the Phase 4 rewrite inside that new location. Two reasons: first, a pure rename-and-move is easy to verify byte-for-byte (same behavior, different file/name), so if something breaks you know it's the move, not new logic; second, `seq_tick()` and the timer wiring stay in `sequencer.c` (they're timing/scheduling, not pattern storage), so the move needs a clean line between "what moves to `Pattern/`" and "what stays in `sequencer.c` as the scheduler that calls into `Pattern/`" — deciding that boundary is easier without simultaneously redesigning the data the boundary is passing around.

### 1.4 Move `Core/Preset/` into Scene/Bank-owned Preset

Completed in Session 029. `presetManager.c/.h` and `ParameterArray.c/.h` now
live under `Core/Bank/Scene/Preset/`; public function prefixes intentionally remain
`preset_*`, `parameterArray_*`, and `paramArray_*`.

The include-path knock-on was handled in the same session: `Makefile` uses
`-ICore/Bank/Scene/Preset` and source paths under `Core/Bank/Scene/Preset/`.

**Suggested complementary step:** since 1.2, 1.3, and 1.4 all touch `frontPanelParser.c`'s call sites in overlapping files (`presetManager.c` and `sequencer.c` both `#include "frontPanelParser.h"`), doing 1.2 *before* 1.3/1.4 means the direct-wiring pass only has to happen once, against the pre-move file layout, rather than being redone against new paths. The order above (1.1 → 1.2 → 1.3 → 1.4) reflects that.

### Open Engineering Questions

- **Opcode fan-out audit:** before starting 1.2, worth a quick pass building a table of all 70 opcodes → which function(s) each currently calls → which of the 105 call sites use it, so the "delete once unused" step in 1.2 has a checklist instead of relying on the compiler catching orphaned cases.
- **`pat_*` vs `scene_*` naming collision:** `PatternSetting` (the struct) and pattern-settings-adjacent names in `sequencer.h` may collide semantically with "pattern" as used in the *old* 8-pattern-slot sense once Phase 4 removes that concept. Worth deciding up front whether `pat_*` refers to "the per-track step data" (the new sense) or keeps meaning "one of the old 8 slots" during the transition, so Phase 4 doesn't have to rename things a second time.


## Phase 2 — Directory Kit Loading & Descriptor Scene Bridge

**Location:** `Core/Bank/Scene/` (post-move), `Core/Hardware/SD/filesystem.c`, `Core/Hardware/SD/asyncfatfs/`

This phase is now the completed bridge that made the later filesystem work
possible. It did not implement the full Bank/Scene hierarchy. It settled the
target filesystem shape, implemented root `Kit/NNN Name/` loading, and moved
instrument parameter ownership into Scene descriptor images.

Implemented through Sessions 030-032:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` defines the
  authoritative future filesystem layout.
- Root `Kit/` scanning supports preferred `NNN Name`, compatibility
  `NNN_Name`, and FAT short-alias fallback such as `001SLA~1`.
- Normal kit load opens `kitset.kcg` plus six descriptor-keyed instrument files.
- `kitset.kcg` owns only format/version plus per-slot `type`, `file`, and
  `audio_out`.
- Instrument files own descriptor-keyed `[params]` and optional `[morph]`
  endpoint values.
- Instrument values load into `scene_t.kit`, indexed by descriptor ID.
- VOICE pages are populated from descriptor-owned layouts in
  `Core/DSP/Instruments/*/*Parameters.c`.
- Preset/InstrumentManager apply descriptor image values into DSP runtime.
- Pattern and container load/save remain bridge formats and will be ripped out
  when the final Pattern storage lands.

Explicitly not completed in Phase 2:

- New-format save operations.
- Scene folder load/save. Implemented later in Session 039.
- Bank folder load/save. Implemented later in Session 039 as a one-resident-Scene bridge, not the final 16-Scene workspace.
- Effect load/save.
- Root `settings.cfg`; globals still use legacy `glo.cfg`.
- Final new-format Morph load/save was not part of Phase 2; Kit/Instrument
  Morph Load arrived later, and Kit/Instrument Morph Save was completed in
  Session 038.
- Descriptor-aware step automation. Sessions 033-034 completed descriptor
  Morph, direct descriptor velocity/LFO application, and the Instrument Load
  runtime transaction; step automation remains Phase 3 work.

### 2.1 Current Bridge Shape

`filesystem.c` began as a flat file loader: `filesystem_makeFilename()` builds
an 8.3 name like `p000.snd` directly into a buffer, and file type is an enum
such as `FS_FILE_KIT`, `FS_FILE_PATTERN`, `FS_FILE_MORPH`,
`FS_FILE_PERFORMANCE`, `FS_FILE_ALL`, `FS_FILE_GLOBALS`, and
`FS_FILE_SAMPLES`.

The current exceptions are `FS_FILE_KIT` load/save and the new-format
Kit/Instrument Morph save paths. Normal kit load no longer opens a flat `.snd`;
it scans `Kit/`, enters the cached numbered kit folder, parses `kitset.kcg`,
and loads the six listed instrument files. Normal Kit Save and KitMrp Save
write the same directory shape through the descriptor text writer. Legacy
`FS_FILE_MORPH`, Pattern, Performance, All, and Globals remain bridge paths
internally. The user-visible Load/Save menu no longer offers Pattern, MorphKit,
Perform, or All, because those choices do not have a valid current workflow.

### 2.2 Phase 2 Verification Anchors

- Boot with `SD_CARD/Kit/001 Slak`.
- Confirm kit scan shows the folder name from `Kit/001 Slak/`.
- Confirm `kitset.kcg` slot type/file/audio routing is honored.
- Confirm canonical Choke descriptor keys such as `amp_envelope_decay` and
  `amp_envelope_decay_choke` parse, while legacy HiHat spellings remain
  compatible with converted or older files.
- Confirm VOICE pages display descriptor layouts rather than static
  `menuPages.h` voice cells.
- Confirm editing audible descriptor values changes DSP runtime state.

### Open Engineering Questions

- **Kit save target path:** new-format Kit save must write the same folder shape
  the loader accepts, not the current legacy `.snd` path.
- **Bridge removal timing:** Pattern/container bridge storage should remain only
  until Phase 4's dynamic stack Pattern format replaces it.

### Suggested Complementary Features

- **Focused kit save smoke test:** once new-format Kit save exists, round-trip
  one hand-edited kit folder and one converted legacy kit before implementing
  Scene/Bank saves on top of it.

## Phase 3 — Finish Filesystem, Instrument Runtime, Morph & Menus

**Location:** `Core/Bank/Scene/`, `Core/Hardware/SD/filesystem.c`,
`Core/Hardware/SD/storageTypes.c`, `Core/Bank/Scene/Preset/`, `Core/Menu/`,
`Core/DSP/Instruments/`

This phase finishes the work that Phase 2 intentionally exposed but did not
complete. The target is to leave the project with descriptor-backed instruments
fully loadable, morphable, modulatable, automatable, and saveable inside the
new filesystem hierarchy, with Scene and Bank structures defined before the
dynamic Pattern rewrite begins.

### 3.1 Instrument Runtime Completion

Finish descriptor-backed instrument load/apply coverage:

- Status after Session 044: cold boot and later Scene switching now share the
  same type-safe activation lifecycle. SceneData exists before
  InstrumentManager constructs tagged members; every incoming slot type/image
  is valid before the all-source two-LFO-pair/velocity rebind; no physical
  Drum/Snare/Cymbal/HiHat arrangement is assumed.
- Status after Session 034: the main descriptor runtime path is live for the
  current Drum/Snare/Cymbal/HiHat rows, including the LFO expansion,
  voice-local decimation, velocity amount, per-voice Morph, and Scene
  modulation targets. Instrument registry metadata now classifies each type as
  Basic, Advanced, and/or Choke; the Instrument Load type browser enforces the
  two-Advanced limit while allowing any number of Basic voices.
- HiHat uses one canonical closed-decay descriptor (`amp_envelope_decay`) and
  an optional Choke sibling (`amp_envelope_decay_choke`). VOICE track 7
  resolves the sibling dynamically for Choke types. A non-Choke instrument in
  slot 6 instead receives a generated Scene-owned track-7 decay plus morph
  endpoint; if its descriptor table has no base decay, track 7 remains the
  regular slot-6 page.
- Root Instrument replacement is now a staged Preset-owned transaction. The
  filesystem validates a private payload first; the active Scene commit clears
  every outgoing runtime modulation target, replaces the descriptor image and
  type, resets the slot runtime, reapplies Morph/runtime images for all six
  slots, and rebinds every modulation source. UI target-changing gestures are
  locked until it finishes.
- Continue verifying every loaded descriptor key has a correct runtime binding
  or explicit special writer as instruments become swappable and new file work
  starts.
- Keep `ROW_NOBIND_IMAGE` parameters as morphable/modulatable/automatable image
  values with explicit runtime handling.
- Keep target selector rows as `ROW_NOBIND` supplemental values.
- Confirm menu edits write active Scene descriptor images and apply the audible
  runtime value immediately.
- Confirm new loads do not depend on old `parameter_values[]` instrument cells.

### 3.2 Descriptor Modulation and Automation

Replace the remaining legacy target runtime path for descriptor-backed targets:

- Status after Session 034: velocity and LFO destinations are
  descriptor-aware for direct descriptor targets, supplemental voice-local
  decimation, per-voice Morph Scene targets, and Scene Decimation.
- Make `AutomationNode` descriptor-aware instead of emitting legacy MIDI CC/CC2
  into `midiParser_ccHandler()`.
- Stop narrowing step automation destinations to `uint8_t`; preserve canonical
  descriptor IDs.
- Make `preset_applyInstrumentRuntimeValueInternal()` honor automation
  recording where appropriate.
- Keep target display helpers enumerating the active Scene descriptors and
  filtering by descriptor flags.
- Session 035 corrected direct descriptor LFO writes by routing them through
  InstrumentManager descriptor-domain adapters and runtime writers. Keep that
  adapter path as the model for any future modulation/automation writer.
- Replace fixed global-node scans in `modNode_resetTargets()` and
  `modNode_directOriginalValueChanged()` with InstrumentManager's dynamic
  node ownership. The Instrument Load transaction explicitly clears all
  owners, but ordinary runtime operations still need the same complete view.

### 3.3 Morph and Per-Voice Morph

Status after Session 033: Morph works against Scene-owned descriptor images and
has been extended to per-voice Morph. This section remains the contract for
future file save/load, MIDI cleanup, and automation follow-through.

Current and future Morph values are 0..255 user-facing parameters. Menu storage
and file storage should preserve 0..255. MIDI CC and step automation remain
7-bit input paths, so they need explicit conversion:

- Input `0..126` maps to `value * 2`.
- Input `127` maps to `255`, so the morph endpoint is reachable.

Work items:

- Keep main endpoint writes, morph endpoint writes, `scene->settings`
  storage, `morph_interpolation[]`, and `instrumentManager_writeRuntime()` in
  sync as file save/load work lands.
- Preserve the one-parameter-per-foreground-pass worker model.
- Add per-voice morph amounts to Scene file save/load.
- Receiving a global morph message overwrites all per-voice morph values.
- Velocity modulation can set per-voice morph and update the visible value.
  Step automation must do the same once descriptor automation lands.
- LFO-to-voice-morph is a background overlay and does not update the visible
  PERF-menu morph value.

### 3.4 Menu and Morph UI Completion

Complete the menu path required for descriptor-backed instruments:

- Keep VOICE pages descriptor-generated.
- Keep `SHIFT+VOICE` morph endpoint edit/view behavior for descriptor cells.
- Ensure static non-voice pages still resolve through `menuPages.h`.
- Visible/editable per-voice Morph controls in PERF are implemented.
- Rebuild load/save/reload menus around the typed filesystem hierarchy instead
  of the old flat slot list.
- Status after Sessions 038-039: Kit, KitMrp, Scene, and Bank are promoted on
  the Load/Save type cycler where applicable. File/Dir/sDir diagnostics remain
  compiled but normally hidden behind `CONFIG_DEV_MODE`. Kit Load/Save are
  restored on the asyncfatfs LFN/case foundation, and Kit Save recursively
  replaces all physical directories for the target slot before writing the new
  folder. VOICE press on Load enters nested Instrument Load; VOICE press on Save
  enters root Instrument Save and InstrumentMrp Save. Hardware pots now navigate
  the Load/Save hierarchy directly: pot 1 walks main Load items, per-voice
  Instrument Load items, main Save items, and per-voice Instrument Save items;
  pot 2 changes slot/item; pot 3 moves or deselects the cursor; pot 4 edits
  characters.
- Status after Sessions 035-038: Kit Morph, nested Instrument, and same-type
  Instrument Morph Load are usable in code. KitMrp Save and InstrumentMrp Save
  are live new-format writers: they snapshot the current interpolated
  per-voice Morph value and write it into both normal `[params]` and morph
  `[morph]` endpoint storage, without changing resident kit or instrument
  names/stems. Root `Instrument/` scans `.drm`, `.snr`, `.cym`, and `.hat`
  pools per type in alphanumeric order; lower-row browsing loads immediately,
  while type changes do not replace the current kit-member display/source. The
  display index is one-based and saturates at 999. Session 042 later removed
  retained `kit_t`/Scene name and filename stems; HCNAMES and the one active
  `.hcindex` cache now own display identity.
- Load-context SEQ LEDs now use `pat_sceneHasActiveSteps()`: in Kit Load they
  are multi-Scene toggles and every selected target blinks; in Instrument Load
  exactly one Scene is selected and blinks. All 16 Scenes are resident.
- Status after Session 039: Scene and Bank are promoted top-level Load/Save
  entries with explicit OK/OW confirmation. File/Dir/sDir diagnostics are
  compiled but hidden unless `CONFIG_DEV_MODE != 0`. Scene and Bank operations
  return the cursor to the top-row type field when they complete. Load Bank
  shows an OK affordance and does not load while scrolling.
- Status after Session 044: successful Bank index entry continues into the
  highlighted Bank's child preview and gates input until its mask is resident.
  Accepted OK/OW commands show `...` with no cursor through payload, apply, and
  terminal cache work, then always reset to the bracketed type row.
- Keep scene-level MIDI note/channel and `voice_decimation_all` out of
  `kitset.kcg` and instrument files.

### 3.5 Scene and Bank Structures

The real Scene and Bank structures and their filesystem ownership are now
implemented. Keep this phase open for verification and cleanup while the
future dynamic Pattern model is still being designed:

- `SCENE_COUNT == 16` is the resident Bank workspace. There is no separate
  seventeenth staging Scene in the current product shape; asynchronous load
  staging remains private filesystem operation state.
- `sceneset.scg` contents and validation are implemented. It stores Scene
  settings and never object names.
- `bankset.bcg` version 2 contents and validation are implemented, including
  `active_scene`, the 16-bit child-present/edit masks, and the selected child
  persistence rules.
- Root `Scene/` and `Bank/` folders are numbered with gap-tolerant browsing;
  root library slots are direct `000..999`.
- Root `Scene/` is a user library/pool like root `Kit/` and root
  `Instrument/`: explicit Scene Save writes there, explicit Scene Load imports
  from there, and root Scene files are not autosaved.
- Scene embedded kits are folders named `Kit <kit name>/`; the second word is
  the kit name, and that name is not stored anywhere else.
- Store MIDI note/channel and `voice_decimation_all` as Scene settings.
- The 16-Scene Bank workspace, multi-Scene edit masks, Bank-local Scene
  selection, Bank Save/Load staging, and child re-entry reset are implemented.
  Bank-local Scene folders are two-digit `00..15`, not root three-digit
  `000..999` library folders.
- Object names are directory/file names. `sceneset.scg`, `bankset.bcg`, and
  instrument files must not acquire `name=` fields.

### 3.6 Load and Save Operations

Implement load/save operations for the settled file types in
`knowledge_files/specification_reference/FILESYSTEM_SPEC.md`:

- Session 034 completed voice-6/track-7 Choke storage and dynamic menu/runtime
  resolution, plus staged arbitrary Instrument replacement without hardcoded
  parameter lists. Preserve those semantics in every save implementation.
- Session 035 completed normal Kit Save. Session 036 repaired its filesystem
  naming path: Kit Save now creates VFAT LFN entries for the visible Kit folder
  and six instrument files, while retaining returned 8.3 aliases for
  `kitset.kcg` and open paths.
- Session 036 corrected numbered library slots to direct `000..999`; slot
  `000` is real for all filetypes. Kit/Scene library slots use `uint16_t`
  plumbing through filesystem, presetManager, menu, and kitBrowser. Instrument
  file voice coordinates remain one-based `1..6`.
- Session 035 added storage-only LFO `self` routing: load resolves `self` on
  `lfo_target_voice`/`lfo_target_voice_2` to the destination slot, and Kit Save
  emits `self` only when an LFO voice selector points at the saved instrument's
  own slot.
- Standalone root Instrument Save is implemented. It uses the same
  descriptor-keyed writer and `self` serialization rule used by Kit Save and is
  entered from Save-page VOICE press.
- Instrument pool load copies a descriptor-keyed instrument file into a kit
  voice slot. Instrument Morph Load copies source normal endpoint values into
  the current slot's morph endpoint only when the type matches.
- KitMrp Save and InstrumentMrp Save are implemented. Both use the normal text
  schemas but write the current interpolated parameter value into both
  `[params]` and `[morph]` endpoint storage for morphable values; non-morphable
  values remain normal-only. Morph Save is therefore a flattened current-state
  snapshot, not an inverted endpoint export.
- Scene Load/Save is implemented. It writes/reads `sceneset.scg`,
  `Kit <kit name>/`, `pattern.pat`, and `effects.fx` through the Session
  036-039 asyncfatfs foundation. Root `Scene/` load/save is library/pool
  exchange only and is not part of the autosave workspace.
- Add an FX slot shim so Scene folders can validate/store `effects.fx` before
  Phase 6 implements full effects.
- Bank load/save is implemented for the 16-Scene workspace with SEQ-button
  Scene masks, selected/present child intersection, preservation of unselected
  resident payload/identity, one-child-at-a-time rescan, and shared Scene
  parsing. Session 044 repaired unchanged-slot Load:Bank admission and made the
  active loaded Scene apply immediately during playback.
- Pattern load/save stays bridge-only until Phase 4 replaces the Pattern file
  format. Session 043's Scene/Bank `pattern.pat` v3 stores exactly the 112-byte
  128x7 on/off bitmap as seven 32-hex-character rows; v1 is accepted empty and
  v2 imports only its final bit field.
- Effect load/save may initially validate placeholders; real FX parameters land
  in Phase 6.
- `settings.cfg` replaces `glo.cfg` for system settings and active-bank number
  selection. A `.settings.cfg` backer remains target design only; no accepted
  autosave/backer implementation currently updates it.

Session 041 name-index/cache completion:

- Every Instrument type and each root Kit, Scene, and Bank library now has a
  directory-local `.hcindex` generated from the physical directory scan.
- One shared 1,000-row, nine-byte SRAM name cache is reused across all four
  browser families. Instrument rows are sorted; Kit/Scene/Bank rows are direct
  slot rows and preserve blanks. No per-type or per-library cache exists.
- Menu entry, type changes, and exit dispose/reload that one cache. Kit, Scene,
  and Bank saves rescan the written parent and rewrite its complete `.hcindex`
  before the save callback is released, then refresh the current Save display.
- Boot writes the three root library indexes and the four Instrument indexes one
  at a time, then reloads `/Bank/.hcindex` before initial Bank selection because
  the shared cache was disposed during Instrument index generation.
- The old 128-entry Instrument limit and the dedicated Kit/Scene/Bank
  presence/display/alias arrays are retired. The remaining 2,013-byte
  `kitBrowser` compatibility bridge was removed in Session 042.

Session 044 Load/index completion:

- HCNAMES completion is metadata-neutral and does not infer that a root
  namespace needs rebuilding.
- Kit/Scene/Bank Save owns one physical parent scan plus complete index rebuild
  because Save may mutate the numbered namespace.
- Pure Scene/Bank Load instead publishes the completed payload/result, applies
  the active Scene through the shared clear/image/rebind worker, then reloads
  the unchanged root index read-only before ending the explicit command.

asyncfatfs note for future save code:

- Session 036 adds asyncfatfs LFN component creation and object iteration;
  Session 038 adds documented filename sanitization expectations and
  filesystem-level recursive directory cleanup used by Kit-slot replacement.
  Session 039 proves Scene/Bank save must scope recursive deletion by parent
  directory and visible product-name parser; future save code should
  reuse/extend that filesystem boundary, not recreate FAT file writers. See
  `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`.
- Missing core primitive before autosave/power-loss-safe replacement is atomic
  rename/replace for temporary-file promotion.

### 3.7 Debounced Autosave and Reload

Implement debounced autosave after explicit Bank save paths exist. The original
one-file wording is not sufficient once a resident Bank has sixteen editable
Scenes: menu parameter edits may target any subset of Scenes, and one gesture
can dirty multiple embedded Kits/Instruments at once.

**Current status after Session 044: target only, not implemented.** The
committed `Core/Bank/Scene/Autosave.c/.h`, `AUTOSAVE_IMPLEMENTATION.md`, and
draft blob schema were explicitly rejected by the user as incorrectly
implemented. Do not extend, validate, or describe that code as partial Phase 3
completion; restart from the ownership and durability requirements below when
autosave is explicitly resumed.

Architecture decision:

- Bank is the only autosaved workspace. Root `Scene/`, `Kit/`, `Instrument/`,
  `Pattern/`, and `Effect/` folders are libraries/pools and remain explicit
  load/save/copy/import/export targets.
- Treat the active Bank as a working set with per-file dirty records, not as one
  active file. Resident `scene_t` memory is authoritative; autosave is delayed
  persistence from that memory into dot-file backers inside the active Bank
  folder.
- The currently playing/viewed Scene and the Scene edit target set are separate
  concepts. Voice mode keeps one active Scene for audition/playback focus, but
  SEQ buttons may toggle any subset of the 16 Bank Scenes as the target set for
  Voice/Kit/Instrument parameter edits. A single encoder gesture may therefore
  mutate one Scene, all 16 Scenes, or any subset such as Scenes 5-8.
- Multi-Scene Voice/Kit/Instrument edits are binding first-class behavior, not
  copy-after-edit convenience. They support workflows such as using the Bank as
  one shared conceptual Kit with 16 Patterns, or reconciling a parameter change
  across a selected Scene range. On disk these remain Scene-local Kit and
  Instrument copies unless a later feature explicitly adds shared-file/link
  semantics.
- Track dirty state by `(bank_scene_index, file_domain, optional_slot)`.
  Minimum domains:
  - `bankset.bcg`
  - `scene/sceneset.scg`
  - `scene/kit/kitset.kcg`
  - `scene/kit/instrument[0..5]`
  - `scene/effects.fx`
  - `scene/pattern.pat`
- Autosave applies only to the 16 committed resident Bank Scene slots. There is
  no seventeenth resident landing Scene in the current product shape; any
  future staging remains private operation storage unless separately designed
  and approved.
- The active Bank is identified by number, not by folder display name. Root
  `settings.cfg` records that number; `.settings.cfg` is its autosave/backer
  file. Closing the global settings menu or loading/saving a Bank rewrites both
  root settings files so the active-bank pointer and settings snapshot agree.

Dirty ownership:

- Add a small Scene/Bank dirty-ledger module rather than scattering filesystem
  calls through Menu. Mutation owners mark dirty after they successfully change
  retained Scene/Bank memory.
- Parameter editing code must pass a Scene edit mask into the mutation owner.
  The owner applies the same logical edit to every selected resident Scene and
  marks dirty records for every affected Scene/file. Repeated knob movement
  updates the same records and refreshes their debounce timers; it must not
  enqueue one write per encoder tick.
- Descriptor main endpoint edits dirty only
  `scene/kit/instrument[slot]`.
- Descriptor Morph endpoint edits dirty only
  `scene/kit/instrument[slot]`, because `[morph]` lives in the same instrument
  file.
- Supplemental descriptor image values such as LFO target selectors,
  per-instrument decimation, and velocity amount dirty
  `scene/kit/instrument[slot]` unless the storage spec later moves a specific
  value elsewhere.
- Kit-level settings, slot type/file membership, audio routing, and generated
  slot-6/track-7 decay values dirty `scene/kit/kitset.kcg`; if the edited value
  lives inside a specific instrument file, dirty that instrument file instead.
- Scene settings such as MIDI note/channel, global Morph, per-voice Morph
  amounts, and `voice_decimation_all` dirty `scene/sceneset.scg`.
- Effect parameter edits dirty `scene/effects.fx`.
- Pattern edits dirty `scene/pattern.pat`, but pattern editing is not part of
  the multi-Scene parameter-toggle behavior. Pattern autosave can therefore stay
  active/viewed-scene scoped until Phase 4 changes the Pattern model.
- Bank-level metadata edits dirty `bankset.bcg`.
- Instrument, Kit, and Scene copy/paste inside the active Bank are batch
  resident-memory mutations. They mark destination dirty records for dot-file
  autosave, but they do not update committed non-dot Bank files until explicit
  Bank SAVE promotes the selected dot-file backers.

Debounce policy:

- Each dirty record stores `first_dirty_tick`, `last_dirty_tick`, and an
  `in_flight` flag.
- A write becomes eligible after 5 seconds with no further edits to that record.
- A continuously edited record is forced eligible after 30 seconds from
  `first_dirty_tick` even if edits continue.
- The filesystem writer remains single-operation serialized. When several
  records are eligible, write oldest forced records first, then oldest idle
  records. Keep the record dirty if the write fails.
- A successful autosave clears only the record that was written. Other dirty
  records in the same Scene remain pending.
- Autosave must not change live DSP state. It serializes retained Scene/Bank
  data only.

Committed files, dot-file backers, SAVE, and RELOAD:

- Inside an active Bank, the non-dot filename is the committed save/load file:
  `sceneset.scg`, `kitset.kcg`, `pattern.pat`, `effects.fx`, instrument files,
  and `bankset.bcg` are the user's explicit SAVE/LOAD truth.
- The matching dot-file is the autosave working backer:
  `.sceneset.scg`, `.kitset.kcg`, `.pattern.pat`, `.effects.fx`, `.slakd1.drm`,
  `.bankset.bcg`, etc. Autosave writes dirty records to these dot-files, not to
  the committed non-dot files.
- Bank SAVE is the commit operation. It waits for the autosave scheduler to
  finish all selected dirty dot-file writes, then copies/promotes the selected
  dot-file backers over the matching non-dot committed filenames.
- Bank LOAD reads from non-dot committed files. Bank load/save operations start
  with all Scenes selected; SEQ buttons can restrict the operation to a subset.
- Startup/resume should normally load valid dot-file backers for the active
  Bank so unsaved autosaved work returns frictionlessly. If a dot-file is
  missing or fails validation, fall back to the matching committed non-dot file.
  The committed non-dot file remains the explicit SAVE/LOAD truth, while the
  dot-file is the resumable working state.
- RELOAD applies to Scene scope. It reads the selected Scene's non-dot committed
  files into resident memory and resets that Scene's dot-file backers to match
  the committed files. It is therefore "discard autosaved working edits for this
  Scene and return to the committed Scene state."
- If a RELOAD Scene contains dirty or in-flight autosaves, cancel, drain, or
  supersede those jobs before replacing memory and dot-file backers from the
  committed non-dot files.
- Dot-file autosave should eventually write through a temporary file and
  rename/replace it over the dot-file after close/flush. On startup, a leftover
  `.tmp` indicates an incomplete temporary write. Ignore/delete that `.tmp`,
  then use the previous dot-file if it validates; only fall back to non-dot if
  the dot-file itself is missing or invalid.

Implementation sequencing:

- First implement explicit Bank file writers and a reusable write-job API per
  file domain. Root Scene Save/Load can reuse Scene serializers, but it remains
  a library/pool operation outside the Bank autosave ledger.
- Then add a dirty-ledger API, for example
  `sceneDirty_markSceneSettings(scene)`,
  `sceneDirty_markKitset(scene)`,
  `sceneDirty_markInstrument(scene, slot)`,
  `sceneDirty_markPattern(scene)`, and
  `sceneDirty_markEffect(scene)`.
- Wire dirty marks into Preset/SceneData/Pattern/Effect mutation owners, not
  directly into generic Menu paint/edit code.
- Add the autosave scheduler after explicit writes are proven.
- Add dot-file promotion/copy and Scene RELOAD after asyncfatfs has atomic
  rename/replace or an equivalent safe copy/replace primitive. Until then,
  autosave may write dot-files with current overwrite semantics only behind an
  explicit "not power-loss safe yet" development gate.

### Open Engineering Questions

- **Rename/replace primitive in `asyncfatfs`:** confirm or add a safe async
  primitive before implementing `.tmp` replacement.
- **SRAM budget for any future staging/Scene expansion:** the current product
  has exactly 16 resident Scenes and a separate 2,048-byte non-Pattern
  filesystem stage. Any seventeenth Scene or larger stage is a new retained
  allocation and requires exact measurement plus explicit user approval.
- **Descriptor automation/runtime ownership:** migrate `AutomationNode` from
  legacy byte CC/CC2 targets to canonical descriptor/Scene targets, correct
  raw float LFO adapter writes, and make modulation-node enumeration dynamic
  before treating automation as feature-complete.
- **Effect placeholders:** decide how strict `effects.fx` validation should be
  before Phase 6 has real FX stacks.

### Suggested Complementary Features

- **Scene reload shortcut:** bind the future Scene RELOAD primitive to a
  shortcut for reverting the currently playing Scene from its committed
  non-dot Bank files and resetting that Scene's dot-file backers.
- **Bank load indicator:** show a small persistent indicator while background
  bank loading locks load/save/reload.

## Phase 4 — Dynamic Stack Pattern Implementation

**Location:** `Core/Sequencer/Pattern/` (post-1.3 move)

This is the biggest single architectural change in the whole roadmap: replacing the current `seq_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS]` static array (8 fixed pattern slots × 7 tracks × 128 sub-steps × 7-byte `Step` struct) with a per-scene, per-track pointer array into a dynamic, bit-packed event pool. "Patterns" as a selectable, separate concept go away — a scene now just *has* a pattern (one per track), and scene-switching is what pattern-switching used to be.

### 4.1 Step/bar model

8 bars, 16 steps per bar, 128 steps max per track, no sub-steps. What used to be sub-step resolution is now handled by **microtiming offset** (a per-step fine-timing value) and **roll modes** at the step level, rather than by subdividing the step grid itself. In step mode, the transport through bars is serviced by the `BAR <>` control and the `SELECT` buttons — `SELECT` no longer addresses sub-steps (there aren't any), it addresses which bar of the 8 is currently shown/edited, matching how you described it.

### 4.2 The dynamic event pool

**Address array.** Every scene has 7 tracks × 128 steps = 896 possible steps, each represented by a fixed `896 × 2 bytes = 1,792 bytes` per-scene array. Each 16-bit entry is **MSB (bit 15): on/off, next bit (bit 14): has specials, trailing bits 13–0: a 14-bit offset address** — three independent pieces of state, not a single pointer with reserved values doing double duty:

- **MSB, bit 15 — on/off.** A set bit means the step is **on**: its `SEQ` LED is lit and the note at that step triggers. A clear bit means the step is off: its `SEQ` LED is not lit and its note does not trigger. This trigger state is independent of the offset address and has-specials fields: an off step can still apply automation and retain special values.
- **Next bit, bit 14 — has specials at address.** A set bit means the dynamic block at this step's offset address carries a special-flags byte (note/velocity/timing/roll/probability overrides, or the automation-hold flag). A clear bit means the step uses every default value for those fields. This bit remains meaningful when the on/off MSB is clear: assigned special values remain stored and are not cleared or discarded when the step is turned off or back on.
- **Trailing bits 13–0 — offset address**, with exactly **one** reserved value: `0x3FFF` (all 14 bits set) means **no lookup at all** — there is no dynamic block for this step, full stop. Combined with the on/off MSB, that one reserved address covers both of the old "free" cases:
  - `0x3FFF` + on/off MSB clear → step is off, nothing stored (the old "blank").
  - `0x3FFF` + on/off MSB set → step is on, plays the track's default note/velocity/timing/probability (the old "default").
  - Any other address (`0x0000`–`0x3FFE`, 16,383 real values) → look up a dynamic block regardless of the on/off MSB. If the MSB is clear, the step's note does not trigger but every automation entry in that block is still applied. If bit 14 is also set, its special values remain assigned and persist across later on/off toggles. This lets a parameter change land on a beat without also firing a trigger there, and lets turning a programmed step off remain a reversible performance edit rather than deleting its settings.

  One combination the encoder must never produce: bit 14 set (claims a special-flags byte exists) together with address `0x3FFF` (claims there's nothing to look up) — these are contradictory, and should be an assertion/invariant check rather than a case the decoder has to handle.

**Only one address is reserved now, not two** (the earlier pass at this design spent two addresses — `0x0000` and `0x3FFF` — because on/off and "has data" weren't yet separated). That makes the usable pool `2¹⁴ − 1 = 16,383` bytes, a one-byte difference from before that doesn't move any of the capacity figures below in practice.

**The dynamic block itself**, at any real address:

- **First 2 bytes (always present):** 10-bit step-ID — a back-reference to which of the 896 steps owns this block, needed for O(1) defragmentation (4.3), sized for `2⁹ = 512 < 896 ≤ 1,024 = 2¹⁰` — plus a **6-bit automation count, 0–63, always meaningful**. Zero is a legitimate count now (not reserved or avoided): a step can have custom special values and zero automation, which is exactly the case a fixed `count − 1` encoding would have made awkward. The ceiling drops from the earlier design's 64 to 63 as the direct cost of making zero usable — a fair trade for not needing a special case.
- **If bit 14 (has-special) was set in the static entry**, byte 3 is **always** the special-flags byte, in a fixed position — not conditionally placed depending on the automation count. The rule that matters at this scoping level, and the only one this document needs to commit to: **each set bit in the special-flags byte means one more value byte follows**, immediately after it, in flag order — except for any flag later designated a pure binary condition (`automation hold` is the one named so far), which needs no value byte since the flag bit alone *is* the information. Exactly which fields exist, how many of the 8 bits get used now versus reserved for later, and their order — note, velocity, timing, roll, probability were the working example earlier in this process — is implementation-phase detail, not a scoping decision; what's fixed here is only the structural rule (flag byte → N value bytes → automation entries).
- **Automation entries follow**, 2 bytes each, unchanged: 9-bit target parameter ID (up to 512 addressable parameters — see 4.4), 7-bit value. The count from the header says how many.

`automation hold` stays defined at the storage layer only as "a flag that exists"; see 4.3a for how it relates to the default playback/record behavior that's now settled for ordinary (non-held) automation.

**Worst case, for illustration only** (using the 5-value-flag working example, not a commitment to that exact field list): a step using all 5 example special fields and 4 automations costs `2 header bytes + 1 special-flags byte + 5 override bytes + 4×2 automation bytes = 16 bytes`. If every one of the 896 steps hit that simultaneously, the whole scene costs `896 × 16 = 14,336 bytes` — comfortably inside the 16,383-byte pool, with 2,047 bytes of headroom left over even in that extreme case. In the more realistic "every step has at least one automation, nothing else" case, the pool holds **7,295 total automation entries**, independent of exactly how many special fields end up defined, since that case never touches the special-flags byte at all.

### 4.3 Background defragmentation & real-time-safe live recording

This has two genuinely different jobs, because there are two different ways a step's dynamic-stack block gets written, with very different timing pressure:

- **Parameter locks** (manual, menu-driven edits) are slow and human-paced — effectively unlimited time, on audio-thread timescales, between one edit and the next. These can synchronously request more room for a step, relocating its block if needed, without anyone noticing a stall.
- **Live recording** captures automation in real time while the pattern plays. It must never be made to wait on the background defragmenter — a stall here is an audible or lost value, not a UI hiccup.

That split drives the design: a fast, allocation-free path for the common case, kept ahead of by a cheap local "top-up" operation, with the existing global defragmenter demoted to separate, lower-priority housekeeping.

**Fast path — the common case touches compaction not at all.** A live-record write for a parameter that **already has** an automation entry on that step is a single in-place value-byte overwrite: no growth, no allocation, no interaction with compaction, no matter how fast values are streaming in. This covers the ordinary case of sweeping an already-automated parameter and needs nothing further.

**Reserved slack — covers the case that does grow.** A live-record write for a parameter with **no existing entry on that step yet** needs 2 new bytes. Rather than reserving the bare 2-byte minimum, reserve a full 4-byte chunk of trailing slack after every block whenever it's written or relocated — matching the free-space bitmap's own 4-byte chunk granularity below, so this costs nothing extra to track. A 4-byte chunk absorbs **two** first-time automation writes before it's exhausted, not one — worth having, because a single live pass can put more than one newly-automated parameter onto the same step (sweeping two knobs at once); a bare 2-byte reservation would force an immediate relocation on the second one.

**Micro-relocation — refilling slack, one step at a time.** When a step's trailing slack is used up, that step is queued (low priority) for a slack refill: relocate just that block to a new spot with a fresh 4-byte slack chunk appended, using the same write-new/swap-pointer/free-old mechanic as the main defragmenter, just scoped to a single step instead of a pool-wide sweep. That narrow scope is what makes it cheap enough to run reactively, a few per background tick, well inside the bounded-per-tick-work approach the rest of the project already leans on (`BURST_REDUCTION.md`). At realistic musical tempos, the gap between one new-parameter live-record event on a given step and the next is on the order of a step's duration — tens to low hundreds of milliseconds — which is enormous headroom on a 216MHz core for the background maintainer to stay ahead of.

**Safety net.** The pending-write input queue from the original design stays as the backstop for the pathological case — several new-parameter automations landing on the same step within a single tick, faster than the maintainer can refill slack. A write that finds its step's slack already exhausted queues instead of blocking, and drains within the next tick or two. This should be rare, not the normal path.

**Global defragmenter — separate, lower priority.** Micro-relocations leave small holes scattered through the pool over a long editing session, the same way any allocator does. The pool-wide defragmenter (find the most fragmented region, consolidate it) is what turns scattered holes back into large contiguous free runs. It's periodic housekeeping, not on any real-time path — the reserved-slack mechanism above is what actually guarantees live recording never stalls, independent of whether the global sweep has run recently.

**Free-space tracking.** A 1-bit-per-chunk occupancy bitmap over 4-byte chunks, covering the full `2¹⁴` nominal address space rather than just the 16,383 usable bytes — chunking the full nominal range is what gives a clean `2¹⁴ ÷ 4 = 4,096` chunks `= 4,096 bits = 512 bytes`, with the single reserved address (`0x3FFF`) and the 3 unusable trailing bytes (`16,384` isn't a multiple of 4, so the top partial chunk is permanently unusable — too small to hold even a minimal 2-byte header plus anything, so the allocator should just never hand it out) both marked permanently occupied. Both micro-relocation and the global defragmenter use this to find a new home for a block: a linear scan is cheap on this core (worst case ~128 32-bit word tests across 4,096 bits, faster still with a count-trailing-zeros instruction to skip whole free/full words at once). The bitmap doesn't need to track *whose* block occupies a chunk — that's what the block's own step-ID header is for: "whose block is this" is answered by reading the 10-bit ID, not by anything in the bitmap.

**The write protocol — creating and clearing steps, not just relocating them.** The same write-new/swap-pointer/free-old discipline that governs defrag governs every mutation of a step's static entry, with no exceptions:

- **Creating or growing a step's data:** the step-writer (menu edit or live-record) sends the stack servicer a request — the content to store, and how much room it needs (one or two 4-byte chunks, typically). The servicer allocates space, writes the full content into place, and *only then* reports the real address back. The step-writer does not touch the static array (offset address, on/off bit, has-specials bit) until it has that confirmed address — there is no intermediate state where the static entry points at a block that isn't fully written yet.
- **Clearing a step's stored data:** the step-writer atomically writes `0x3FFF` (preserving the current on/off MSB and clearing has-specials) to the static entry first, so the sequencer immediately stops looking up the old dynamic block, and *separately* notifies the servicer that the old address is now free, to be added back to the bitmap whenever convenient. This operation is distinct from turning a step off: an ordinary on/off toggle changes only bit 15 and must preserve the real offset address, automation, has-specials bit, and special values. Nothing reads a freed block again once its static entry no longer points at it, so there is no urgency to that second step.
- **The invalid combinations flagged in 4.2** (has-specials set together with the reserved address, in either on/off state) should never be producible by construction if the offset address, on/off bit, and has-specials bit are always updated **together, as a single atomic 16-bit write** — never as separate partial updates to the same entry. That single discipline is also what rules out both of the "nasty" half-updated states worth naming explicitly: a special-flags byte being claimed by the static entry while the pool still holds old, differently-shaped content underneath it (avoided because the static entry only ever flips *after* the new pool content is fully in place), and a reader misinterpreting pool bytes because it checked the wrong bit or checked out of order (avoided by the reader always consulting bit 14 before deciding how to interpret byte 2 onward, never inferring shape from content).

This protocol is sound as described, and worth stating as three explicit invariants for whoever implements it, since they're the kind of thing that's easy to violate accidentally later without them being written down:

1. **Single-owner access to the pool.** The stack servicer must be the only code path that ever mutates pool bytes or bitmap state — including its own background defrag and micro-relocation work — so that "the servicer processes requests in order" is actually true and not just usually true. If anything else ever pokes the pool directly (e.g., a future "fast path" optimization that bypasses the servicer for some case), the ordering guarantees above stop holding.
2. **Stale-request resolution.** The protocol has a real window — between "step-writer sends a request" and "servicer reports back" — during which the true state is in flight. If the user edits the same step again before the first request completes (a quick toggle, a second CC), a late-arriving response for the *first* request could otherwise get applied after a *newer* edit has already superseded it. A per-step generation counter, incremented on every edit and checked before applying a servicer response, is the standard fix — worth specifying now so it's not discovered as a bug later.
3. **Bitmap visibility timing.** A chunk the servicer has reserved but not yet finished writing into shouldn't look "just free space" to the global defragmenter's bitmap scan, but it also shouldn't look like a normal, relocatable, already-linked block (its step-ID back-reference may not be valid yet). The clean rule: the bitmap only marks a chunk occupied *as part of* the same operation that makes it real (fully written, step-ID set, ready to be pointed at) — never before. Anything the defragmenter finds occupied should always have a valid step-ID it can look up in the static array; if it doesn't, that's a bug, not a case to handle gracefully.

**Cross-scene safety, per your note:** mutating step data on a track is disabled if that track's *scene* isn't the one currently playing — so only one scene's pool is ever under concurrent read (playback) + write (edit, live-record, micro-relocation, defrag) pressure at a time.

**Atomicity:** unchanged — the static array entry is a 2-byte, 2-byte-aligned field, so the pointer swap that makes a relocated or newly-written block live is a single aligned `STRH` on Cortex-M7, atomic with respect to interrupts and DMA without additional locking.

**Save format:** because the pool stays defragmented — both by the periodic global sweep and continuously by the micro-relocation slack top-ups — it writes to the `.pat` file close to as-is.

### 4.3a Automation hold and the default record/playback model

This is now settled as the default playback behavior for ordinary automation, not just an open question:

**Playback:** when a step automates a parameter, that value holds through subsequent steps — unchanged — until the next **active** step, meaning a step whose on/off MSB is set and whose note triggers. At that active step, the held parameter resets to its default value, *unless* that same active step also automates the parameter again, in which case it takes on the new automated value instead of resetting. Inactive automation-only steps are not reset boundaries for unrelated parameters: if an inactive step has a valid dynamic-block address and applies automation for another parameter, the previously-held parameter remains untouched. If that inactive step automates the same parameter, the held value is simply replaced by the new automated value and continues holding until the next active step.

**Recording:** the record process has to actively maintain this behavior, not just log raw CC changes as they arrive. Per your spec: while record is active, a parameter change (a "diff") is watched until the user stops recording, changes the selected track/voice, or another voice's CC automation arrives. At the end of each step:
- If the watched parameter diffed during that step, write (or overwrite) the step's automation entry with the parameter's **final** value at step-end — not every intermediate value, just one write per step regardless of how many CC messages arrived during it.
- If the step isn't active and nothing diffed during it, the writer does nothing — no automation entry gets created.
- If the step is inactive but has other automation, the writer still does nothing for this watched parameter unless this parameter diffed during the step. Other inactive automation must not force a re-stamp, because it is not a reset boundary for this parameter.
- If the step **is** active, the writer stamps the currently-held value onto it **even if nothing diffed during that step**. This is the part that makes the playback rule above actually work as intended: without re-stamping, a value set several steps back would reset to default at the next active trigger even though the user never told it to change. The re-stamping is what keeps a live-recorded hold matching what the user actually heard while recording it.

The direct consequence for 4.3's slack/defrag design: live-record's write frequency isn't just "the first time a new parameter touches a given step" — during an active recording pass with a held parameter, potentially **every subsequent active step** gets a fresh stamp, at the sequencer's step rate, until recording stops. Inactive automation-only steps consume slack only for parameters that actually receive new automation on those steps. That's still comfortably slow relative to a 216MHz core (tens to low-hundreds of milliseconds between steps at musical tempos), so the conclusion in 4.3 doesn't change — the background maintainer still has ample headroom — but it's worth being explicit that active-step re-stamping, not unrelated inactive automation, is the real sizing driver for how often slack gets consumed during a busy recording pass.

**One reconciliation this raises, worth flagging rather than resolving here:** 4.2's `automation hold` flag was introduced as a separate, explicit per-entry marker, before this default hold-and-reset behavior existed as a concept. Now that ordinary automation already holds by default until the next active step, it's not yet clear what `automation hold` adds on top of that — whether it means "persist even *through* the next active step, don't reset there either," or something else entirely. Not something to resolve in this document (per your note below), but worth flagging so the two aren't built as quietly-overlapping, half-redundant mechanisms.

### 4.3b Risks and cross-feature tradeoffs

Per your framing of what this document is actually for — catching features that would otherwise step on each other's toes during implementation, not designing every feature in full — here's what 4.3a's hold/record model touches that's worth having on record now, without resolving any of it:

- **Storage cost vs. playback simplicity, as an explicit tradeoff, not an accident.** The re-stamping behavior means a value held steady across a long run of active steps costs one automation entry *per active step it crosses*, not one entry for the whole run. Against 4.2's capacity figures (7,295 total automation entries in the realistic case), a single sustained knob-hold recorded across, say, 20 active steps in a busy pattern consumes 20 of those entries, not 1. The alternative — store only the moment of change plus a hold marker, and have playback search backward for "the last time this parameter was set" — would be far cheaper on pool space but adds real search cost and complexity to the per-tick playback read path. Nobody needs to pick between these now, but whoever implements 4.3a should know this fork exists rather than discovering it mid-implementation.
- **Interaction with per-track step scale and rolls (4.7, 4.8).** "The next active step" is a step-grid concept; a scaled track (where one step might represent a whole bar) or an active roll (many rapid hits inside what's nominally one step) both change what "the next step" means in real time. Whether a roll's individual hits count as separate "active" events for hold-reset purposes, or the whole roll cell counts as one, isn't addressed by either feature's description alone and needs a decision when both are actually implemented together.
- **Interaction with per-voice morph and its LFO overlay (Phase 3 Morph work).** Per-voice morph is recordable the same way any parameter is (via step automation), but it can *also* change continuously from an internally-generated LFO overlay that's explicitly meant to be invisible and non-recorded. The record-watcher described above needs to distinguish "an external CC/knob diff" from "the morph value moved because its own LFO overlay is running" — otherwise background LFO wiggle could get recorded as a dense pile of step automations. This should be a straightforward guard (watch the CC/knob input stream, not the resulting parameter value), but it's a real seam between two features built somewhat separately, worth a specific check when both exist.
- **Interaction with copy operations (4.5).** Because a step's automated value is only meaningful in the context of "whatever was held going into it," copying a *portion* of a pattern (rather than the whole thing) can sound different once pasted somewhere else, if the copied region didn't happen to start right after an active-step reset point. This is a real, if minor, consequence of the hold-based model that a naive "each step is fully self-contained" mental model wouldn't have — worth a note in the eventual copy-operation implementation rather than a surprise bug report.

### 4.4 Parameter ID space (shared with the FX sequencer)

The 9-bit target parameter ID in each automation entry addresses up to 512 distinct parameters. Your budget: roughly 32 FX parameters plus up to 80 parameters per voice × 6 voices = 480, for a 512 total. Current drum voices sit around 32 parameters, so this leaves real headroom for the new voice types in Phase 6 (which will have more parameters than the current drum/snare/cymbal/hihat set) without running out of address space. This same 9-bit ID space is reused by the FX sequencer's automation encoding (Phase 6), so it's worth fixing this parameter-ID scheme once, here, rather than each subsystem inventing its own.

### 4.5 Copy operations

Per your note, the full set: copy scene, copy instrument (single voice part), copy track sequence (one track's step data between patterns/scenes), copy FX (FX stack + its per-scene settings), copy bar, copy step. The existing "copy step, copy sub-step, copy single-voice track between patterns" flow described in `putting it together` becomes the template for all of these — hold COPY, press a source selector, press a destination selector — extended to the new selectable units (scene, instrument, FX) on top of the ones that already exist (step, track).

### 4.6 Automation always runs; trigger is a separate bit

Automation on a step plays back regardless of whether that step has a trigger — this is a change from the old velocity-0-as-automation-only-step model, and 4.2's final storage format makes it a literal, direct consequence of the design rather than a special case to handle: the on/off MSB and the automation offset address are independent fields in the static entry, so a step's trigger state and its automation content were never coupled to begin with. Only steps with bit 15 set light their `SEQ` LED and trigger their note. Pressing a step button in step mode toggles that on/off bit alone — it does **not** touch the step's offset address, automation, has-specials bit, note, velocity, timing, roll, probability, or any other stored data. Automation therefore continues to apply while the step is off, and special assignments persist so they are restored unchanged when the step is turned back on. Additionally: holding `SHIFT+COPY/CLEAR` while the menu is up, then pressing sequence and `SELECT` buttons, clears just a step or just a bar of all automation/settings (distinct from clearing the on/off bit).

### 4.7 Per-track step timing scale

Per-track length (up to 128 steps) and per-track scale, accessible from the second page under the transient-voicing ("click") sub-page. Since there are no sub-steps in this paradigm, scale is expressed relative to the base step (1 step = 1/16th note): scaling a track up to ×16 means 1 step on that track = 1 bar, in `/2` increments down to `/16` (1 step = 1/128th note). Dot and triplet subdivisions are flagged by you as open — see below.

Session 031 landed a bridge version of this concept before the dynamic-pool
rewrite: STEP front-page track settings now expose length, scale, MIDI channel,
MIDI note, and per-track shuffle, and Sequencer derives scaled/shuffled timing
from an absolute 96-PPQ master clock. This bridge uses the existing `Step`
storage and should be treated as behavior/spec discovery for the final Phase 4
implementation, not as the final storage model.

### 4.8 Roll overhaul

Roll rate becomes independent of pattern length (previously tied to it). Rolls become recordable in three modes — full (pitch + velocity), note-only, or velocity-only — configured from the `SHIFT+RECORD` menu. That same menu's existing "automation lane" selector doesn't make sense anymore (there's no separate automation lane in the dynamic-pool model — automation lives on the step it was recorded on) and is replaced by a **record-to-track** option: `slf` (default — record a voice's live automation onto its own track) or any other track, in which case live parameter automation gets recorded onto the specified track instead of the source voice's own.

### 4.9 Patgen/Euklid reset

On the Patgen/Euclidean page (`SHIFT+PERF`), pressing `SHIFT+PERF` twice reverts the pattern to its state from before entering the page; exiting normally commits. Flagged caveat from the spec: if the pattern *length* parameter was changed on the page, a revert may leave residual track-offset artifacts even after the revert, since length changes can shift step alignment in ways a simple content-revert doesn't undo — worth a specific test case once this is implemented.

### 4.10 Triplet/ternary scale mode ("notes from others")

A borrowed idea worth folding in here since it's sequencer-scale-adjacent: a pattern-level scale selector with `12a`/`12b` modes that skip specific steps of a 16-step grid on a fixed schedule (`12a` skips steps 2, 6, 10, 14; `12b` skips 3, 7, 11, 15) to convert a 16-step binary grid into a 12-step ternary (triplet) feel and back, without needing a genuinely different step count. This is a cheap way to get triplet feel without touching the 128-step/8-bar architecture — worth doing as a scale/display mode on top of Phase 4 rather than a structural change.

### 4.11 Final LED state consolidation pass

As the last Phase 4 subphase before the Phase 5 UI cleanup, consolidate the front-panel LED
state rules without changing the public LED API. The current UI work has several
temporary layers that can overlap: base lit/unlit state, persistent blink,
group flash, one-shot pulse, and sequencer chase/highlight. The intended
priority and restore fallback order is:

`base lit/unlit < blink < flash < pulse`

The consolidation should make that order explicit inside `ledHandler`: base
writes update the remembered mode-owned state, blink/flash/pulse render as
overlays, and expiry/cancel of any layer re-renders the next-highest active
layer rather than blindly restoring to base. A pulsed LED that was blinking
should fall back to its blinking render state; a flashed LED whose base changed
during the flash should fall back to the latest base; unrelated LED groups
should not be disturbed. Include BAR1/SW43 and sequencer chase in the same
render rules, either by placing chase explicitly in the priority stack or by
folding it into an existing temporary layer.

### Open Engineering Questions

- **Manual roll triggering:** you flagged needing "a smart way of triggering manual rolls" now that rolls are decoupled from pattern length — this needs a concrete UI proposal (which button/hold-gesture initiates a manual roll, and at what rate) before Phase 5's UI work can wire it up.
- **Dot/triplet subdivisions for per-track scale:** flagged as "maybe" in the source doc — worth a decision before 4.7 is implemented, since it affects the scale-value encoding (a plain `/2..×16` power-of-two range doesn't accommodate dotted/triplet values without extra encoding bits).
- **`automation hold` vs. the 4.3a default hold-and-reset behavior:** as flagged in 4.3a, it's not yet clear what the dedicated `hold` flag adds on top of the now-default "holds until next active step" behavior for ordinary automation. Left open deliberately (per your note that this can wait for the implementation session), but worth resolving before both are built as potentially-overlapping mechanisms.

### Suggested Complementary Features

- **Pool usage meter.** A percentage-used indicator in the global settings, next to the CPU meter, for the active scene's event pool — you specifically asked for this ("0-99% like there is for cpu use") and it's cheap to compute (pool bytes used ÷ pool size) and genuinely useful for knowing when you're approaching the automation ceiling on a dense pattern.
- **Per-scene pool high-water mark, not just live usage** — showing "peak used this session" alongside live usage would help catch a scene that briefly spiked into heavy automation and then got scaled back, which a live-only meter would hide.

## Phase 5 — MIDI, UI & Performance Workflow Cleanup

**Location:** `Core/Menu/menu.c`, `Core/MIDI/MidiParser.c`,
`Core/Hardware/frontPanel/buttonHandler.c`,
`Core/Hardware/frontPanel/ledHandler.c`, `Core/DSPAudio/lfo.c`

This phase gathers the user-facing and control cleanup after the filesystem,
Morph, and dynamic Pattern foundations are in place. MIDI rework belongs here,
alongside the rest of the performance workflow, copy/paste, clear helpers,
automation views, load/save UI polish, and front-panel feedback consolidation.

### 5.1 MIDI and External Control Cleanup

`midi_MidiChannels[8]` already exists: one channel per voice plus one global
channel. Follow-up work:

- Add a `0` sentinel meaning MIDI input disabled for that voice or global slot.
- Verify chromatic note handling on a voice's own channel.
- Route CC1 global-channel Morph and per-voice-channel Morph through the
  0..255 Morph conversion defined in Phase 3.
- Decide how MIDI CC0/Bank MSB maps onto Bank/Scene/instrument loading once the
  Phase 3 file operations are implemented.
- Keep MIDI storage ownership aligned with Scene settings, not `kitset.kcg` or
  instrument files.

### 5.2 One-shot LFOs

Current `lfo.h`/`lfo.c` already has free-running sine, triangle, saw, rect,
noise, exp-up, and exp-down waveforms with a phase accumulator and an overflow
test. One-shot variants can hook that overflow test:

- Add `si1`, `tr1`, `sq1`, `rmp1`, `rnd1`, and `xt1`.
- In one-shot mode, `offset` becomes a pre-trigger delay scaled to the LFO rate.
- One-shot noise holds one random value for the one-shot cycle.
- One-shot rect is phase-inverted relative to the free-running rect behavior.
- Add an idle/delayed/running state field to `Lfo` and hook retrigger through
  `lfo_retrigger()`.

### 5.3 Automation view redesign


This replaces the current step-view automation display (parameter assignment/amount shown under step view) with two connected new views.

**View A — scrollable automation list**, reachable from step view when editing step automation:
- A non-looping, scrollable list of `parameter – voice – amount` entries, showing "end" once you scroll past the last entry rather than wrapping.
- **Knob 1:** cycle the automation parameter (shown as a 3-character short name in the leftmost column).
- **Knob 2:** cycle by voice (shown as `vo1`–`vo6` in the second column).
- **Knob 3:** change the amount (third column).
- **Knob 4:** change the "function" to apply (fourth column) — cycling through: view automation (jumps to View B below), remove just this automation, remove all automation from this step, remove all automation of this type from the track, set all automation of this type on this track to this value, and room for more to be added later.
- Clicking the encoder **executes** whichever function is showing in the fourth column — for anything mutating (remove/set), that means a confirmation screen first; for "view automation," it jumps straight into View B.

**View B — the automation editor itself**, reachable either from View A or directly from the voice page:
- On entry, the current automation for the selected parameter, across every step in the track, is copied into a temporary working array. All edits in this view happen against that working copy — nothing is committed to the real pattern data until the view is exited normally.
- Holding `SHIFT` and pressing `COPY/CLEAR` brings up a "cancel automation edit?" confirmation; confirming discards the working copy entirely, reverting to whatever was there on entry.
- Screen layout: top line shows the voice (long name) and parameter (long name) being edited. Voice LEDs show the currently selected track but **don't** function as track selectors in this view except as a copy destination — you can't switch which track you're editing automation for mid-view by pressing a voice button, only by copying to a different track. `SELECT` LEDs show the current bar (of the 8); pressing a `SELECT` button switches which bar is shown.
- Bottom row (16 characters, one per step of the visible bar): blank if the step has no automation for this parameter, otherwise a `0`–`9` digit representing the automation amount on a relative 0–127 scale.
- **Knob 1:** cycle voice. **Knob 2:** cycle parameter. Sequence buttons light up for any step that currently has automation for the selected parameter.
- **Multi-step editing:** holding any combination of the 16 sequence buttons switches the bottom-row readout to `avg:` (average value across the held steps, left side) and `mod:+0` (a running modification delta, right side). Turning the encoder while steps are held does two things at once: (1) it adds automation at the currently-shown average value to any held step that doesn't already have automation for this parameter, and (2) it increments/decrements the automation value of *every* held step by ±1 per detent, updating the displayed average live. You can keep adjusting by continuing to turn the encoder, by changing which steps are held, or by switching bar/track via the bar/track buttons — the temporary working array tracks all of it.
- **Copy:** from this view you can copy steps, bars, or copy the whole automation lane to another track.
- **No dedicated clear operation inside this view** — the only ways out are the normal exit (commits) or the `SHIFT+COPY/CLEAR` cancel (discards). Clearing automation is a View-A-and-below operation (per 5.3 View A's "remove" functions, or the step-view `SHIFT+COPY/CLEAR`+button wipe from Phase 4.6).

This is a genuinely large piece of UI state machine — four knobs with context-dependent meaning in View A, a temporary-array-with-commit-on-exit model in View B, and a multi-step "hold N buttons, turn one knob, average updates live" interaction that doesn't have a close analog elsewhere in the current menu code. Worth prototyping the state machine (what's "current view," "held steps," "working array," "dirty" state) as its own small module before wiring it into `menu.c`'s existing page-dispatch structure, rather than growing it inline.

### 5.4 Morph quick access & automation indicator

- While viewing a single parameter in the encoder click-in view, holding `SHIFT` toggles between editing that parameter's normal value and its morph-target value, avoiding a save/reload round trip just to set morph endpoints. A further "lock" mode to keep the whole voice interface showing morph-target values for every parameter (rather than needing to hold `SHIFT` per-parameter) is called out as wanted too.
- The voice page should **only** service voice and scene editing — no step-editing functions belong there (that's step view's job). On the voice page, the 16 sequence buttons become **scene toggles**: each one toggles whether that scene is included in the current voice-parameter edit, so a parameter change can be applied to all 16 scenes, a subset, or just one, depending on which are toggled on. This is a genuinely different meaning for those 16 buttons than they have anywhere else in the UI (scene *selection* elsewhere, scene *inclusion-in-edit* here) — worth a clear visual distinction (different LED color/blink pattern) so it's not confused with PERF-mode scene switching.
- The LXR-02 hardware has dedicated shift-labeled functions already printed on the sequence buttons — new shift functions should avoid piling onto those buttons where another control is reasonably available, per your explicit note.
- The LCD's underline indicator should show when the currently-displayed parameter is automated anywhere in the currently playing scene/pattern — a quick "is this being moved by something" signal without needing to open the automation view to check.

### 5.5 PERF mode: scene switching & per-track assignment

- **Instant scene switching:** a global menu option makes scene switching (via `SEQ` buttons or MIDI program change) take effect at the next step rather than waiting for the end of the bar, preserving sequencer position through the switch. This also carries whatever kit/morph/parameter changes the new scene brings, not just pattern data — it's a full scene swap, not just a pattern swap, which is a bigger behavioral change than the pattern-only version originally described in `putting it together`. Default behavior (end-of-bar) is preserved when the option is off.
- **Per-track scene assignment:** hold a voice button and press a `SEQ` (scene) button to assign that individual track to play from a different scene than the rest — same gesture as the old "per-track pattern assignment" idea, retargeted at scenes.
- **Per-voice morph in the PERF page:** each voice gets a direct morph control on the PERF page, full 0–255 range. Step automation and velocity automation update it in real time and it's visible; LFO-to-voice-morph modulation does **not** update the displayed value (per Phase 3.3 — it's background-only). Changing global morph updates every per-voice value shown here.

### 5.6 Looper

Moves to the `SELECT` buttons (confirmed correction from your round-2 answer — the original `putting it together` draft used `SEQ` 9–16, which conflicts with `SEQ` now meaning scene-select in PERF mode). The original spec describes 8 divisions from a half-bar down to 1/64th note, halving at each button, with holding one additional button ("button 9" in the original 16-button numbering) adding a dotted 50% to whichever other loop button is held, and releasing all loop buttons returning the sequencer to the position it would have reached without looping.

That division range was originally expressed in **sub-step** terms (64 sub-steps = 1/2 bar, down to 1 sub-step = 1/64th), which no longer exists as a unit after the Phase 4 dynamic Pattern rewrite. This needs re-deriving in step/bar terms before it can be implemented — flagged below as an open question, since a naive re-mapping (halving from "1/2 bar" down through 8 buttons) lands on 1/256th at the bottom with no sub-steps to represent it, which doesn't match the original "down to 1/64th" intent.

### 5.7 Load/save UI rework

The original `putting it together` draft proposed a specific knob remapping for the load/save menus (knob 1 = type, knob 2 = number/cursor, knobs 3/4 = character entry with capitals/numbers/lowercase split across them). Per your note, this is superseded — "we have bigger file changes in mind" — because the whole load/save menu needs rebuilding around the Phase 3 file model: banks, scenes, kits, patterns, samples, wavetables, effects, instruments, and root `settings.cfg`. The specific knob assignment idea is worth keeping as a starting point for that rebuild, but the menu structure itself (what "type" even means, what "auto-load" means per type) needs designing fresh against the authoritative filesystem spec rather than patched onto the current flat slot menu.

### 5.8 External MIDI sequencing tracks

From "notes from others" in `putting it together`: doubling the sequencer's track count (6 or 7 additional tracks) purely for sequencing external MIDI gear via program-change/CC, using the same UI and workflow as the internal voice tracks, reached through a shift function that opens a second page mirroring the internal-voice page layout. This is naturally deferred until after Phase 4 lands, since it's most straightforward to build as "the same per-track step/automation machinery Phase 4 already built, pointed at a MIDI-out target instead of a DSP voice" rather than a parallel implementation.

### Open Engineering Questions

- **Looper division mapping without sub-steps.** Needs a concrete answer before 5.6 can be built: is 1/64th represented via a track-scale-style subdivision (Phase 4.7's `/2`..`×16` scale applied to a virtual "loop track"), or is the shortest loop division now coarser (e.g., 1/16th, one full step) given sub-steps no longer exist? This changes both the encoding and the UI.
- **Manual roll trigger gesture** (carried over from Phase 4) needs a home in this UI redesign — likely a `SHIFT`+something on the step buttons or a dedicated control, per the "try not to put shift functions on the SEQ buttons" constraint from 5.4.
- **Automation view performance:** View B's temporary working array (automation for one parameter, all steps in a track) needs to be read from and written back to the Phase 4 dynamic pool efficiently — worst case, entering the view triggers up to 128 individual pool lookups (one per step) to populate the array, and exiting triggers up to 128 pool writes. Should be fine given the pool is designed for O(1)-ish per-step access, but worth confirming against Phase 4's actual implementation once it exists.

### Suggested Complementary Features

- **Automation "eraser" mode** (from the earlier draft, still reasonable): a shortcut — e.g., holding `CLEAR` while turning a parameter's knob — that wipes all step automation for that specific parameter across the active track in one gesture, complementing but distinct from View A's per-step "remove" functions.
- **Scene-inclusion visual on the voice page (5.4):** since toggling scene inclusion for a parameter edit is a new interaction, consider a brief on-screen summary ("editing: 3/16 scenes") when a parameter is touched, so it's obvious at a glance how broad the edit's blast radius is before committing to a knob turn.

## Phase 6 — DSP Expansion

**Location:** `Core/DSPAudio/`

**Session 043 correction:** the DTCM figures and transient-relocation proposal
in the historical Phase 6 discussion below predate the tagged-slot migration
and implemented FLASH move. Do not use those numbers for allocation. Current
DTCM use/free/reservation is the Session 043 baseline above and the linked
`SRAM_MANIFEST.md`; `transientData` is already in FLASH and `sine_table`
remains DTCM-resident. Any concrete delay/advanced-buffer allocation requires
the user's explicit byte/region/owner acknowledgement before code is written.

The heaviest phase computationally, and the one where the earlier drafts did the most guessing. This version tries to separate what's confirmed by the current code, what's confirmed by your answers, and what genuinely needs a measurement or a decision before implementation — rather than asserting specific byte counts that sound precise but aren't backed by anything.

### 6.1 Voice tiers

Currently: `DRUM`, `SNARE`, `CYMBAL`, `HIHAT` instrument types, freely swappable between any track (tracks 6 and 7 always choke each other, per the existing spec), instrument type itself is not modulatable or morphable. Per your answer, this becomes three CPU/memory tiers rather than a flat list:

- **Basic** — drum, snare (and similar low-DSP-cost voices).
- **Advanced** — cymbal, hi-hat, and other voices that need meaningfully more DSP per sample.
- **Advanced-buffer** — granular, drone, Karplus-Strong, convolution chamber. Per your clarification: **only one instrument in a given kit will ever use this tier**, and that one instrument gets a dedicated **0.25-second, 16-bit, mono buffer in DTCM** (not ITCM — see below). This is a separate, distinct buffer from the FX-stack's BBD delay (6.7, 8-bit stereo, 1.0+ seconds) — the two are unrelated allocations with fixed, static sizes decided once during implementation, not dynamically resized. (A future idea worth remembering but explicitly out of scope now: an instrument that reads the BBD delay's own buffer directly, as an oscillator — a different feature from anything in this phase, not something to design around today.)

**ITCM is off the table for this buffer, per your call**, and that's the right decision independent of the reasoning that follows: ITCM is only 16KB total and already holds the oscillator hot-path code (`calcSineBlock`, `calcFmBlock`, and the rest of the dozen or so `INITCM`-tagged functions in `Oscillator.c`) — keeping it reserved for code, as you said, avoids a real resource conflict rather than trying to measure exactly how tight a squeeze it'd be.

**Historical pre-Session-043 DTCM analysis (superseded).** The following
numbers and proposal are retained only to preserve the design rationale; use
the Session 043 baseline and linked manifest above for every current decision.

- `sine_table` (`Core/DSPAudio/wavetable.c:43`) is `const int16_t[TABLESIZE+1]` with `TABLESIZE = 4096`, so **4,097 × 2 bytes = 8,194 bytes**.
- `transientData` (`Core/DSPAudio/transientTables.c:62`) is `const int8_t[NUM_TRANSIENTS][TRANSIENT_SAMPLE_LENGTH]` with `NUM_TRANSIENTS = 12` and `TRANSIENT_SAMPLE_LENGTH = 2205`, so **12 × 2,205 = 26,460 bytes**.
- Together: **34,654 bytes (~33.85KB)** — right at the top of your own 26–34KB estimate.

Both are currently DTCM-resident via the `INCCM`/`INCCMZ` macros — which, per `config.h`'s own comment, are aliases for `INDTCM`/`INDTCMZ` (a carryover naming convention from the original LXR's CCM-RAM). This is worth flagging on its own: my Phase 6.7 draft below had claimed DTCM was almost entirely free (~3.2KB used) based on grepping only the literal `INDTCM`/`INDTCMZ` token — that missed everything tagged via the `INCCM`/`INCCMZ` alias, which turns out to include not just these two tables but every drum/snare/cymbal/hi-hat voice struct, the six `ModulationNode` velocity modulators, and the mixer's per-voice state arrays. That's corrected below.

Both tables are also good, low-risk candidates for moving to flash specifically because samples already stream from the same internal flash region (`0x08080000+`, per `MEMORY.md`) successfully today — confirming that this part's internal flash, with its ART accelerator prefetch/cache, is fast enough for real-time audio access, at least for the sequential-access pattern sample playback uses:

- **`transientData`** is read sequentially, start to finish, once per transient hit (`transientGenerator.c`'s `phase`-indexed access) — the same access shape as sample playback, which is already proven to work from this exact flash region. High confidence this moves cleanly.
- **`sine_table`** is read at true audio rate, per-sample, inside the oscillator block-calc functions (`Oscillator.c`), with a phase-accumulator index that's usually local/incrementing but can jump further per sample at high pitch — a somewhat less favorable pattern than `transientData`'s sequential one, and more like the pattern actual *code* execution from flash already relies on (which also works today, on the same ART cache). **Decision: leave it in DTCM.** `transientData` alone frees enough headroom (6.7) without taking on `sine_table`'s less-proven access pattern; if more DTCM room is ever needed later, moving `sine_table` is still there as a lever, with the polyphony stress test below as the thing to check before flipping it.
- Implementation is likely as simple as removing the `INCCM`/`INCCMZ` tag from `transientData`'s declaration — with no section attribute, `const` data falls through to the linker's default `.rodata` placement in app flash, the same place all the rest of the firmware's constant data and code already lives. No new linker section should be needed, but worth confirming the default `.rodata` target is genuinely the app-flash region (`0x08008000–0x0807FFFF`) and not anywhere near the runtime-erasable sample sectors (6–11), so a future sample write/erase can never touch it.
- Two smaller `INCCM`-tagged `const` tables exist too (`squareRootLut`, 128 floats = 512 bytes; `transientVolumeTable`, 69 floats = 276 bytes) and are candidates for the same move if a little more headroom is ever wanted, on the same reasoning as `transientData` — not needed for the current target, so not part of the current plan.

**What's actually in DTCM, confirmed by a real build.** The project compiles cleanly with `arm-none-eabi-gcc` (13.2.1) as checked out — `make` succeeds with no errors, only pre-existing minor warnings unrelated to this analysis. The linker's own `.map` file gives the real, no-guessing numbers:

- **`.itcm`** (the `INITCM`-tagged oscillator code from earlier): **3,776 bytes** used of 16,384 total — 12,608 bytes free. Confirms your instinct to keep ITCM code-only was the right call independent of this measurement, and also confirms there was never much pressure there to begin with.
- **`.dtcm`** (`INDTCM`/`INCCM` — const data loaded from flash at startup): **35,168 bytes**. This is `sine_table` + `transientData` + the two smaller tables (`squareRootLut`, `transientVolumeTable`) + one small static float — matches the source-level tally closely.
- **`.dtcmz`** (`INDTCMZ`/`INCCMZ` — zero-initialized, runtime-mutable state): **6,092 bytes**. This is the six voice structs, the six `ModulationNode`s, the mixer's per-voice arrays, and the audio/oscillator interpolation buffers — all genuinely small; the six voice structs turned out to be compact (a few hundred bytes each), not the large unknown I'd flagged as needing measurement.
- **Total DTCM in use today: 41,260 bytes (40.3KB) of 131,072 (128KB) — 89,812 bytes (87.7KB) free before moving anything.**

This resolves the open question from the previous pass at this document: the real total was measurable, and it's good news — DTCM was never close to full, just not fully accounted for by source grepping alone.

### 6.2 Granular instrument

Built as a complete instrument (advanced-buffer tier), not an oscillator variant — this was your explicit correction to the initial framing. Reads directly from the internal sample flash region (the same one that already backs regular sample playback, so the flash-read-speed question is answered by "it already works for samples as-is," per your answer). Pitch parameters, per your spec: assign a scale/interval, fine detune, and a "distance" value that moves up/down that scale/interval — rather than free continuous pitch, grain pitch is quantized to a chosen scale and stepped through it.

A feedback path with a short delay/decay is also wanted, using the dedicated 0.25-second buffer from 6.1 — confirmed as its own separate allocation, not routed through the 6.7 FX-stack's BBD delay. Since only one advanced-buffer-tier instrument exists per kit, this buffer is exclusively the active granular (or drone/Karplus/convolution) instrument's own resource.

Grounding from actual granular-synthesis practice, since this is new DSP territory for the project: the standard approach windows each grain with an amplitude envelope to avoid clicks at non-zero-crossing boundaries, and the shape of that window is itself a real timbral control, not just anti-click housekeeping — an equal-power/Hann-style crossfade gives the smoothest, most "fused" texture, while sharper (near-rectangular) windows give a more clicky/metallic character and are cheaper to compute. Grain parameters worth having beyond pitch (standard across granular implementations): grain length, density (grains per second / overlap amount), and position jitter (randomizing the read-start point slightly for a less mechanical texture) — these map naturally onto the existing per-parameter automation/morph infrastructure once they exist as real parameters.

### 6.3 New voices

- **West Coast.** Sine/triangle oscillator core, wavefolder (depth + symmetry), FM ratio/index, and an LPG-style decay envelope in place of a standard ADSR. Two pieces of this reuse existing code directly: the sine/triangle core is already `SINE`/`TRI` in `Oscillator.h`, and 2-operator FM already exists (`calcFmSineBlock`/`calcFmBlock` in `Oscillator.c`) — the FM ratio/index parameters are largely exposing controls on code that's already there, not writing new FM synthesis from scratch. The wavefolder and LPG are genuinely new. For the wavefolder: the cheapest embedded-appropriate approach is a triangle-style fold (mirror the signal back down once it crosses a threshold, piecewise-linear, computationally trivial), which is the same family of technique used in Serge/Buchla-style analog wavefolders being modeled in current DSP research — worth noting that any digital wavefolder aliases hard at high fold depth on high-pitched material, and this platform has no spare CPU budget for oversampling the wavefolder stage, so fold depth may need a soft ceiling (or an explicit "this gets aliasy at extreme settings" acceptance, which is arguably in keeping with an intentionally lo-fi/8-bit-adjacent voice anyway). For the LPG: real Buchla-style LPGs are a combined VCA+lowpass filter driven by one control signal with a distinctly *asymmetric* response — fast to open, slow/lazy to close — which is what gives the characteristic percussive "ring." The computationally cheap way to get that same asymmetric behavior digitally is a one-pole smoother on the strike/decay envelope with two different time constants depending on whether the envelope is rising or falling, driving both the VCA gain and the filter cutoff from that single smoothed value simultaneously (rather than two independent envelopes) — this is a well-documented digital model of the real Buchla 292 circuit and is cheap enough to run per-voice on this part.
- **Drone.** Interacting sub-oscillators, slow chaotic LFOs, internal bit-crush, a short delay/feedback loop (the same dedicated 6.1 buffer as granular — only one advanced-buffer-tier instrument exists per kit, so whichever one is in use owns it exclusively), integrated ring modulation, bit inversion, and some extra pre-wired or limited-selection internal LFOs (i.e., not the full general-purpose LFO routing the main voices get — a smaller, purpose-built set). No transient/envelope in the normal sense — it's meant to sit and drone once triggered.
- **Karplus-Strong.** This is worth calling out as the *cheapest* of the new voices to implement well: the algorithm is a short noise burst fed into a delay line of length `N = Fs / f0` (sample rate over target fundamental), read back through a simple one-pole averaging filter (`y[n] = (y[n-N] + y[n-N-1]) / 2`, or a loss-factor-weighted version for controllable decay time), fed back into the delay line. A single delay line plus a one-pole filter per voice is far cheaper than any of the other new voice types, and the classic extension for better pitch accuracy at higher notes (an allpass filter correcting the fractional part of the delay length that a purely integer-sample delay line can't represent) is a small, well-documented addition if pitch accuracy on higher-pitched plucks turns out to matter.
- **Convolution chamber.** A pitch-enveloped transient or user sample run through a simulated resonant chamber (spring reverb, cabinet, etc.) via convolution. This is real convolution reverb, which is the most CPU-expensive item in this entire phase — a true convolution against an arbitrary-length impulse response scales with IR length, and even a short IR (a few hundred samples) is meaningfully more expensive per sample than anything else in this document. This needs an explicit CPU budget decision (how long an IR is affordable per voice, whether it's one shared chamber IR set or per-kit-selectable, whether a cheaper structured/algorithmic reverb approximation is an acceptable substitute for true convolution) before committing to "convolution" as the literal implementation rather than as the description of the desired *sound*.

### 6.4 New oscillators

- **Wavetable.** Reads `.wav` files of any length from numbered folders under root `Wavetable/` (confirmed by your answer — not a fixed single-cycle format, no Serum-style multi-frame container). Morphable (interpolating between two selected waves) but not modulatable directly; scanning through a wavetable set happens via LFO or envelope targeting the wavetable-position parameter, same as any other modulatable parameter.
- **PWM.** You flagged this as "probably wavetables, but suggest another method if you have one" — a direct suggestion: implementing PWM as a genuine variable-duty-cycle square calculation (compare the oscillator's phase accumulator against a duty-cycle threshold instead of the fixed 50% used by `REC`) is cheaper than storing a set of wavetable frames at different duty cycles, and only needs one new parameter (duty cycle) rather than wavetable memory. The tradeoff is the same aliasing consideration as any hard-edged digital waveform on this platform (no oversampling budget), so it inherits the same character as the existing `SAW`/`REC` waveforms rather than being cleaner than them — which is likely fine, since they're presumably an accepted part of the current sound already.
- **Buffer oscillator.** Reads directly from the L or R DTCM buffer described in 6.7, using the same scanning parameters as the granular oscillator/instrument (position, loop size, retrigger, rate/sync, retrigger randomization). This is effectively "granular, but reading from the BBD buffer instead of sample flash" and can likely share most of its scanning-parameter code with 6.2's granular instrument rather than being a wholly separate implementation.
- **Swarm / hypersaw.** A detune-and-phase-spread oscillator stack (multiple copies of a base waveform, each slightly detuned and phase-offset) — standard "supersaw" technique, computationally is just N oscillator instances summed, so its cost scales linearly with however many stacked voices are budgeted per instance.
- **Open-ended items from the source doc worth a decision, not an implementation yet:** "some other smart FM arrangements, still keeping the filter" (a natural extension once 2-op FM's existing code, 6.3, gets exposed as a first-class oscillator option — 3-operator or feedback-FM are the obvious next steps) and "other places to put wavefolding" (the FX stack in 6.7 is one obvious answer — wavefolding as an insert effect rather than only an oscillator-stage effect).

### 6.5 FX bus & send routing

Currently `mixer.c` has no FX bus or send concept at all — `mixer_audioRouting[6]` is a flat per-voice output-destination array (which of the physical outputs each voice's dry signal goes to), and there's no shared send/return path anywhere in the DSP chain. This entire subsystem is new, not an extension.

Per your spec: a stereo send per voice, with send amount as a parameter alongside the voice's existing volume parameter. Three selectable fader modes:
- **Pre-FX (normal):** the voice's main fader attenuates both the dry mix and the FX-send mix together.
- **Post-FX:** the fader only controls the dry/main-mix amount; the FX send is unaffected by fader position.
- **FX:** the fader controls FX-send amount as an additional stage after the send point, and the voice's normal volume parameter effectively becomes a send-only control to the voice's usual output assignment rather than controlling a dry signal directly.

The FX send itself gets its own output assignment (stereo 1/2, L1/L2/R1/R2 — the same destination set voices already route to) and its own output volume, and can be summed back into the main mix if the FX send and a voice share an output destination.

Each FX **stack** (a chain of FX types — the 6.7 tech-demo stack is the first one) is its own file, with up to 64 arbitrary parameters, remembered in both kit and morph endpoints (so FX parameters morph the same way voice parameters do), and tagged with a stack-type identifier so a kit remembers *which* FX stack type it was using even as stacks are swapped between kits — mirroring how instrument type is handled for voices.

### 6.6 FX sequencer

A dedicated 16-step sequencer for FX parameter automation, kept deliberately **static** rather than routed through the Phase 4 dynamic event pool — per your answer, this is small and fixed enough (16 steps × 24 possible automated parameters × 1 byte per automation slot = exactly 384 bytes) that dynamic allocation would be overhead for no benefit. FX parameters are also automatable from the regular track steps, using the same 9-bit parameter ID space from Phase 4.4 (so an FX parameter and a voice parameter are addressed the same way from track-step automation — only the FX sequencer's own dedicated 16-step array is a separate, static structure).

Run modes, per `putting it together`: **Off** (pressing the FX sequencer's `SEQ` button shows its current settings rather than running it), **Fwd** (steps 1–16 straight through), **Rnd** (a random step per division), **FirstX** (the first X steps play in a fixed random order each cycle, the remaining steps run straight), **LastX** (mirror of FirstX — the last X steps are randomized, the preceding steps run straight). Plus a scale and length setting, matching the track-level scale/length concept from Phase 4.7.

### 6.7 The 8-bit tech demo stack

The first real FX stack, meant to double as a proof of concept for the FX bus itself. Three stages: bit-crush (bit-off/invert per bit — a literal bitmask/bit-toggle effect on the sample word, not a bit-depth-reduction crusher), a wavefolder (same technique as 6.3's West Coast wavefolder, available here as an insert effect), and an 8-bit stereo BBD-style delay, followed by a selectable multimode filter at 16-bit resolution — reusing the same filter DSP the voices already use (`Core/DSPAudio/ResonantFilter.c` already has the state-variable filter core — `SVF_recalcFreq`, plus `fastTanh`/`tanhXdX`/`softClipTwo` saturation helpers — tagged `INITCM_EFFECT`, currently disabled by default via `ENABLE_EFFECT_INITCM_CODE 0` in `config.h`; this filter stage is a real, existing, tested building block, not new DSP).

**Memory — now confirmed by a real build, not estimated.** DTCM is 128KB (131,072 bytes) total; a real `make` shows 41,260 bytes already in use (35,168 in `.dtcm`, 6,092 in `.dtcmz` — see 6.1), leaving **89,812 bytes (87.7KB) free today**, before moving anything.

Target sizes: the BBD delay at a full 1 second, stereo, 8-bit is `44,108 × 2 = 88,216 bytes (86.15KB)`; the 6.1 advanced-buffer-tier buffer at 0.25 seconds, mono, 16-bit is `44,108 × 0.25 × 2 = 22,054 bytes (21.54KB)`. Combined target: **110,270 bytes (107.7KB)**.

**That doesn't fit in the 87.7KB free today — it's short by about 20KB.** This is exactly what the 6.1 flash-relocation plan is for: moving `transientData` and `sine_table` out of DTCM frees `26,460 + 8,194 = 34,654 bytes (33.85KB)`, bringing free DTCM to `89,812 + 34,654 = 124,466 bytes (121.6KB)` — **enough for both buffers at their full target sizes, with about 13.9KB left over.** If the two smaller tables (`squareRootLut`, `transientVolumeTable`) move too, that margin grows to roughly 15.2KB.

So the concrete answer: **the flash move isn't optional headroom, it's the difference between the two buffers fitting at their stated sizes or not.** Given the decision to move `transientData` only and leave `sine_table` where it is (6.1 — `sine_table`'s audio-rate access pattern is the riskier one, `transientData`'s sequential pattern is the safe, already-proven one), the real number is: `89,812 + 26,460 = 116,272 bytes (113.55KB)` free against a `110,270-byte (107.69KB)` target — **it fits, with 5,858 bytes (5.86KB) to spare.** That's a real margin, not a rounding error, but it's tighter than moving both tables would give (13.9KB), so it's worth keeping in mind if any other Phase 6 feature also wants a DTCM allocation later.

**A design choice worth surfacing rather than deciding silently:** real BBD hardware pairs its delay line with companding (compress going in, expand coming out) and pre/post low-pass filtering specifically to keep an 8-ish-bit-equivalent signal path usably clean — raw *linear* 8-bit PCM, with no companding, is considerably noisier and more aliased than that. Given this is explicitly framed as an "8-bit tech demo," the gritty raw-linear character might be exactly the point rather than a flaw — worth deciding whether this stack ships as intentionally lo-fi (raw 8-bit, cheapest to implement, matches the "tech demo" framing) or as a more faithful-sounding BBD emulation (adds a compander stage, more DSP cost, cleaner result) — possibly as a toggle, since the compander is a small addition on top of a working raw-8-bit delay rather than a different architecture.

### Open Engineering Questions

- **`sine_table`-in-flash, if revisited later** — not part of the current plan (6.1: leaving it in DTCM, moving `transientData` only), but if DTCM pressure from some other future feature ever makes it worth reconsidering, the thing to test first is several simultaneous sine-based voices at widely different, high pitches — the access pattern most likely to pressure the ART cache, unlike `transientData`'s already-proven sequential one.
- **Convolution chamber CPU budget (6.3)** — needs a decision on maximum affordable IR length before "convolution" is locked in as the literal technique rather than a cheaper structured-reverb approximation of the same target sound.
- **FX stack file format** — 64 arbitrary parameters per stack, remembered in kit and morph endpoints, with a stack-type tag: this needs the same kind of parameter-ID-space decision Phase 4.4 made for step automation (is FX-stack-parameter-64 the same "9-bit ID" space, or a separate per-stack-type namespace?) before the file format can be finalized.

### Suggested Complementary Features

- **Grain windowing (granular, 6.2):** selectable grain envelope shapes — sharp/rectangular (clicky, cheap), Hann/equal-power (smooth, the standard default), and a "windowed/bell" middle ground — as a single parameter, since the window shape is one of the most audible and cheapest-to-implement granular controls available.
- **LPG "ping" modifier (West Coast, 6.3):** a velocity-sensitive strike control that governs how hard the LPG's envelope is hit, independent of note velocity's usual volume role — since the asymmetric-envelope LPG model in 6.3 is naturally sensitive to how its input transient is shaped, this is a small addition on top of that model rather than new DSP.
- **Shared wavefolder stage (6.3/6.4/6.7):** since the same triangle-fold technique shows up in the West Coast voice, the "other places to put wavefolding" open question, and the 8-bit FX stack, implementing it once as a shared inline function (gain-in, fold-depth, symmetry parameters) rather than three separate copies keeps behavior consistent and is less to maintain.
- **BBD compander toggle (6.7):** as discussed above — a cheap way to get both the "authentic-tech-demo-crunch" and "cleaner vintage delay" versions of the same delay line without building two delay effects.

---

## Note on this revision

The version of this document that existed before this pass was generated by spawning ~19 parallel sub-agents against a phase outline that had already drifted from what you'd actually specified, and several of its specifics don't hold up against the real source:

- It claimed `frontPanelParser.c` has "178+ switch cases" to delete. The real number is 70.
- Its granular-buffer code allocates the *entire* 16KB ITCM region to one `int16_t[8192]` buffer (16-bit samples, not the 8-bit mono buffer you specified) — which would leave no room for the oscillator hot-path functions (`calcSineBlock`, `calcFmBlock`, etc.) that are already `INITCM`-tagged into that same 16KB region today, and likely wouldn't link.
- Its wavetable memory plan invents a `.sram1_bss` section attribute that doesn't exist in this codebase — the real attributes are `INDTCM`/`INDTCMZ`/`INITCM`, defined in `config.h`, and ordinary `static` data already lands in SRAM1 via `.bss` without needing a special section at all.
- It recommended shrinking `AUDIO_DMA_FRAMES` to 64 "to buy headroom," despite the code's own comment above that `#define` documenting 64 as already tried and rejected for causing freezes during pattern-load testing.

This revision replaces those with figures checked directly against `Core/` and the linker script where they could be verified, and flags the rest as open questions with a clear path to resolving them (mainly: build once, read the `.map` file) rather than asserting numbers the code doesn't support.

It also goes back to the two rounds of architectural questions you answered earlier in the process — the 128-step/8-bar sequencer redesign, the dynamic event pool's exact bit layout and defragmentation approach, the three-tier voice model, the FX sequencer's static 384-byte structure, and the morph engine's one-parameter-per-cycle drain order — and uses your actual answers as the source of truth throughout, rather than the compressed one-line paraphrases those answers got reduced to in the intermediate draft.
