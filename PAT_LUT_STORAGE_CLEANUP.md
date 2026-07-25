# Pattern and slider-LUT storage cleanup plan

## Execution log

Implementation is in progress. This log is updated only after the corresponding
source change is made and checked; it is deliberately separate from the plan
so the source-audited design remains readable as a future reference.

- **2026-07-25 — started:** beginning with the PatternData representation and
  its sequencer users. No source implementation had been changed at this log
  entry.
- **2026-07-25 — implemented and built:** `PatternSet` is now the asserted
  112-byte `step_on[7][16]` bitmap and `scenes` is 20,992 B (16 × 1,312 B) in
  the fresh ELF. Fixed-grid playback uses on-bits only; live recording writes
  an on-bit; per-step automation, Step snapshots, scale/length/rotation/shuffle
  playback, and the sequencer-only `automationNode` module were removed from
  the link. Scene/Bank `pattern.pat` now emits v3 hexadecimal bitmap rows;
  v1 remains empty and v2 imports on/off bits only. Legacy binary pattern
  streams are explicitly rejected/disabled rather than decoded.
- **2026-07-25 — LUT implemented and built:** `slider_lut` was reduced to 2,048 native
  floats (8,192 B, confirmed as symbol size `0x2000`). It uses raw `>> 1` with
  no LUT interpolation; the shared refresh helper is used by initialization and
  foreground polling. The mixer retains only its existing block gain smoothing.
- **2026-07-25 — second LUT reduction:** `slider_lut` now contains 1,024 native
  floats (4,096 B, confirmed as symbol size `0x1000`). It uses raw `>> 2` with
  no interpolation, so only attenuator step density changes; selected gains and
  all downstream mixer calculations remain native `float` values.
- **2026-07-25 — measurement:** `make -j2` completed successfully. The fresh
  ELF reports total `.bss` of 79,920 B. This is higher than the earlier
  arithmetic projection because the final linked image retains unrelated
  filesystem/runtime buffers; the authoritative target pattern and LUT symbols
  are 20,992 B and 8,192 B respectively. Hardware audio and SD round-trip
  testing remain required before release.
- **2026-07-25 — Bank conversion:** converted all sixteen
  `SD_CARD/Bank/000 Full/*/pattern.pat` files from v2
  `<length>,<scale>,<128 bits>` rows to the emitted v3 32-hex-character bitmap
  rows. The Bank config now uses the writer's bare `0040` mask spelling; the
  loader also accepts legacy `0x0040` input so existing cards remain loadable.
- **2026-07-25 — SRAM manifest:** added `SRAM_MANIFEST.md`, generated from the
  fresh ELF/map/symbol table. It includes region capacity/use/free figures,
  section placement, major owners, and the full linked-address-`0x200*` symbol
  inventory. The current normal SRAM1 `.bss` is 70,104 B; the ELF-wide
  zero-initialized total is 79,920 B because it also includes DTCM and DMA
  sections.

## Purpose

Make the retained pattern representation deliberately minimal so the linked
SRAM measurement reflects only the currently wanted pattern capability, then
reduce the slider-transfer LUT without reducing the float precision consumed by
the mixer. This is a planning document only; it does not change firmware
behavior yet.

## Baseline and target

The checked-in ELF currently has these relevant allocations:

| Allocation | Current | Target | Static SRAM1 reduction |
|---|---:|---:|---:|
| One `PatternSet` | 10,796 B | 112 B | 10,684 B/Scene |
| Sixteen resident PatternSets | 172,736 B | 1,792 B | 170,944 B |
| `slider_lut` | 4,096 `float` = 16,384 B | 2,048 `float` = 8,192 B | 8,192 B |
| **Combined** | **189,120 B** | **9,984 B** | **179,136 B** |

With no unrelated link-layout changes, this changes the current measurements as
follows:

| Object/section | Current | Expected after cleanup |
|---|---:|---:|
| `sizeof(PatternSet)` | 10,796 B | 112 B |
| `sizeof(scene_t)` | 11,996 B | 1,312 B |
| `scenes[16]` | 191,936 B | 20,992 B |
| SRAM1 `.bss` | 249,552 B | about 70,416 B |
| total static SRAM1 | 253,060 B | about 73,924 B |

The section figures are predictions, not acceptance evidence: the final fresh
ELF and its symbols are authoritative.

## Scope and non-negotiable storage contract

Replace the current `Step`, `PatternSetting`, `LengthRotate`, main-step shadow,
and all per-step automation records with exactly this retained pattern payload:

```c
typedef struct {
    uint8_t step_on[NUM_TRACKS][NUM_STEPS / 8u];
} PatternSet;
```

This is 7 tracks × 16 bytes = **112 bytes per Scene**, and therefore 1,792
bytes for the 16-Scene resident Bank workspace.

`step_on[track][step >> 3]` owns the bit for `step`; bit 0 represents the
lowest step in the byte. This ordering must be documented and covered by tests
at steps 0, 7, 8, and 127. There must be no secondary main-step mask, cached
expanded Step array, staged PatternSet, sequencer shadow, or replacement
per-Scene fields that recreate removed pattern state.

The existing `scene_settings_t.midi_channel[7]` and `midi_note[7]` remain
Scene settings, not PatternSet members. They are already independently owned
and are not a place to move any removed step or timing state.

### Resulting musical behavior

This is intentionally a capability reduction for the measurement build.

- A stored bit means only “trigger this track on this 1/16-grid step.”
- An active step always uses the current empty-pattern defaults: probability
  127, velocity 100, and `MIDI_DEFAULT_TRIGGER_NOTE` (63).
- Track timing is fixed: 16 steps, no rotation, 1:1 scale, and no shuffle.
- Step note, velocity, probability, automation lanes, pattern next/change,
  track length/rotation/scale/shuffle, and their file persistence are removed.
- Live note recording records an on-bit only. Step automation recording and
  held-step automation are disabled cleanly rather than retaining a hidden
  data path.
- Copy/clear, bar copy, LEDs, and Euclidean generation continue to operate on
  on/off bits. Copying a bar means copying its two bitmap bytes; copying a
  track means copying its sixteen bytes.

The STEP menus must not leave editable controls that silently do nothing.
They should expose only the supported on/off workflow and display fixed/default
values only where the existing UI cannot yet remove a cell. The UI cleanup is
part of this storage change, not a later cosmetic pass.

## Implementation sequence

### 1. Establish a compile-time and link-time baseline

1. Build the unmodified tree and record `arm-none-eabi-size -A` plus the
   `scenes` and `slider_lut` symbol sizes from `arm-none-eabi-nm -S`.
2. Add temporary/guarded compile-time assertions in the production headers for
   `sizeof(PatternSet) == 112` and, after checking alignment on the target
   compiler, `sizeof(scene_t) == 1312`. Keep the assertions as regression
   guards if the assumptions hold.
3. Use the fresh ELF rather than source arithmetic to update
   `SRAM_DTCM_MANIFEST.md` after implementation.

### 2. Reduce PatternData to bitmap ownership

Touch `Core/Bank/Scene/Pattern/PatternData.h/.c` first.

1. Define the packed `PatternSet` above and remove `Step`, `PatternSetting`,
   `LengthRotate`, `STEP_ACTIVE_MASK`, the main-step shadow, and live pointer
   accessors that expose those deleted representations.
2. Replace them with narrow bitmap helpers: validate coordinates, get/set/toggle
   one on-bit, clear/copy one track, clear/copy a pattern, and copy a 16-step
   bar. Initialize by zeroing the 112 bytes.
3. Make playback-facing accessors return the explicit fixed defaults; then
   simplify the sequencer to call the constants directly where that produces a
   clearer contract. Do not retain dummy `Step` records merely to preserve an
   old function signature.
4. Delete the per-step note/velocity/probability/automation writers and their
   backing state. Make the record path set an on-bit; make automation arming
   and record attempts safe no-ops with their UI state cleared.
5. Delete timing-state accessors and update the scheduler to fixed 16-step,
   unrotated, unshuffled, 1:1 timing. Remove now-unused scale tables and
   re-alignment hooks rather than retaining them as inert PatternData state.

### 3. Update all PatternData clients in the same change

Audit and update these clients before deleting the old interfaces:

- `Core/Sequencer/sequencer.c`: trigger an active bit at default velocity/note
  and eliminate probability, step snapshot, automation, stored rotation,
  variable length, scale, and shuffle reads.
- `Core/Menu/menu.c`, `Core/Hardware/frontPanel/buttonHandler.c`, and
  `Core/Hardware/frontPanel/ledHandler.c`: keep bit-based step select/toggle
  and LED refresh; remove or neutralize deleted STEP/PATTERN edit cells and
  long-press automation behavior.
- `Core/Menu/copyClearTools.c` and
  `Core/Bank/Scene/Pattern/EuklidGenerator.c`: use bitmap-range mutation only.
- `Core/MIDI/MidiParser.c` and recording paths: preserve normal MIDI routing,
  but make step recording on/off-only and remove dependence on stored note or
  automation data.

The sequencer and UI must not introduce a new static array to hold the removed
values. Any state necessary only while processing one event stays automatic
(stack) or is an already-existing transport/UI cursor, not retained Scene
pattern content.

### 4. Replace all pattern persistence with the bitmap schema

Touch `Core/Hardware/SD/storageTypes.h/.c`, `filesystem.c`, and the filesystem
specification as one compatibility boundary.

1. Make the emitted Scene/Bank `pattern.pat` schema version 3:

   ```text
   format=helicase.pattern
   version=3
   track1=<32 hexadecimal characters for 16 bytes>
   ...
   track7=<32 hexadecimal characters for 16 bytes>
   ```

   Hex is proposed because it is readable, has a literal 112-byte payload, and
   lets the parser stream directly into `step_on`. Each high-nibble-first hex
   pair encodes `step_on[track][byte]`, with bytes in increasing order; bit 0
   of each decoded byte is its lowest-numbered step. This is part of the file
   contract and must be tested against the API.

2. Continue to accept text v1 as an empty pattern and text v2 as a one-way
   import of only its 128 on/off characters. Ignore its length and scale; save
   back as v3. This preserves the useful current Scene/Bank fixtures without
   preserving their unwanted state.
3. Remove legacy binary Step serializers/parsers and their discard `Step`,
   main-step, setting, and length records. A legacy binary `pattern.pat` must
   fail with an explicit unsupported-format result, not be partially decoded
   into an invented in-memory Step model.
4. Remove or explicitly keep unavailable the hidden legacy Pattern,
   Performance, and All container pattern streams. They currently serialize
   nine bytes per Step and otherwise force the deleted types back into
   `filesystem.c`. The recommended decision is to retire those hidden bridge
   operations until the final dynamic Pattern format replaces them; they are
   not currently offered in the normal Load/Save workflow.
5. Preserve the current direct-to-final-Scene pattern load model—there is still
   no seventeenth full Scene stage—but initialize/clear only 112 bytes before
   parsing a pattern child.

### 5. Reduce the slider LUT while retaining native float values

Touch `Core/Hardware/frontPanel/IO/adcPots.c`, its comments/header, and
`config.h`.

1. Introduce a named `SLIDER_LUT_ENTRIES` constant of `2048u` and declare
   `static float slider_lut[SLIDER_LUT_ENTRIES]`. This preserves the current
   LUT value type and the float input to the mixer while halving its 16,384 B
   allocation to exactly 8,192 B.
2. At boot, evaluate the existing logarithmic transfer function at 2,048
   positions represented by raw codes 0, 2, ..., 4094. The existing deadzone
   remains inside the transfer function; do not remap it or change
   `SLIDER_LOG_TAPER_DB` semantics.
3. For every 12-bit raw ADC read, use `(raw & 0x0fffu) >> 1` as the LUT index
   and copy that one native `float` directly into the existing `slider_vol[6]`
   cache. Do not interpolate between LUT nodes.
4. Keep `slider_vol` as six floats and keep the mixer's existing 32-sample
   gain interpolation unchanged. No audio-sample path receives a 16-bit gain
   value directly.

The current code performs no LUT interpolation: it uses one float per raw ADC
value. This cleanup deliberately retains that no-interpolation lookup model,
but each LUT node now covers two raw ADC codes. `mixer.c` continues to smooth
the resulting gain over an audio block; this reduces control-change zippering,
but does not restore the lost input-node count. The change does not reduce ADC
hardware resolution, mixer arithmetic precision, or output sample dynamic
range; it reduces only the control-node resolution of the slider attenuator.
Its audible impact must be confirmed on hardware near silence and during slow
fades.

## Source-audited implementation ledger

This ledger is derived from the checked-in C, headers, and Makefile—not from
the specification or earlier planning material. The descriptions below are
written as implementation contracts: each can be used, with only local naming
edits, as the explanatory comment above the corresponding declaration and
definition. “Affiliates” identifies code that must change atomically so an old
representation cannot remain reachable.

### A. Canonical bitmap owner — `PatternData.h` and `PatternData.c`

**Replace the stored representation and public mutator surface.**

> PatternData owns the 112-byte on/off bitmap for one Scene. Inputs are a
> validated Scene/pattern index, track `0..NUM_TRACKS-1`, and step
> `0..NUM_STEPS-1`; the byte is `step >> 3` and the mask is `1u << (step & 7)`.
> Get returns zero for invalid coordinates; set/toggle/clear/copy make no
> change for invalid coordinates. Outputs are only the addressed bitmap bit or
> bytes—no velocity, note, probability, timing, automation, menu state, or
> sequencer shadow is allocated. Affiliates are SceneData, sequencer,
> filesystem storage parsing, copy/clear tools, Euclidean generation, and LED
> presentation. This boundary prevents a caller from depending on a `Step *`
> and therefore keeps the storage contract measurable and enforceable.

Implement `PatternSet` exactly as `uint8_t step_on[NUM_TRACKS][NUM_STEPS / 8u]`
and remove `Step`, `PatternSetting`, `LengthRotate`, `TrackScaleRatio`, active
volume-bit constants, all `Step *` / `uint16_t *` pointer accessors, main-step
bitfields, and their writes. Keep coordinate-validation helpers private where
possible. The header must expose only bounded bitmap operations needed by
clients and the `PatternSet` type needed by `scene_t`; it must not expose a
layout-leaking raw-element accessor.

Required public operations are: validate pattern/track/step; query one bit;
set or toggle one bit; initialize/clear a `PatternSet`; clear/copy a track;
copy a two-byte 16-step bar; copy a whole `PatternSet`; and test whether a
Scene has active steps. A `pat_patternSetGetStep()` / `pat_patternSetSetStep()`
pair taking `PatternSet *` is appropriate for the parser, because it keeps the
on-disk parser from indexing the struct directly. Scene-indexed UI/playback
wrappers may call those helpers after acquiring the Scene pattern.

**Delete semantic state rather than returning fake defaults.**

> This removal retires per-step note, volume, probability, parameter
> destinations/values, pattern-change settings, and variable track timing.
> Inputs formerly accepted by these APIs no longer have a persistent owner;
> consequently there is no meaningful output to synthesize. Affiliates are
> every caller listed in sections C–F. Keeping compatibility functions that
> fabricate a `Step` or cache their arguments would recreate the state this
> change is intended to measure away.

Delete `pat_get/setStepNote`, `pat_get/setStepVolume`,
`pat_get/setStepProbability`, all automation setters/readers/arming, main-step
operations, track length/rotation/scale/shuffle operations, pattern-next/change
operations, and the scale table. Do not leave a dummy `Step`, a static default
record, or “unused” storage for ABI convenience.

`pat_recordNote()` becomes a bitmap-only record operation, or is folded into
the sequencer if that yields the smaller API: its inputs then reduce to
Scene/track/quantized-step, its output is one on-bit, and incoming MIDI note
and velocity deliberately have no stored destination. `pat_eraseStep()` is
likewise a clear-bit operation. `pat_copyBar()` copies exactly two bytes and
must not extend a removed track-length field.

**Remove one redundant initialization path.**

> Scene initialization already calls `pat_initScene()` once for each resident
> Scene. The later `pat_init()` call from `seq_init()` initializes only the
> active Scene again and has no unique input or output. Removing it avoids an
> obsolete one-user lifecycle wrapper and makes SceneData the sole initializer
> of persistent pattern storage.

Delete `pat_init()` and its sole `seq_init()` call; retain `pat_initScene()`
with a single clear-112-bytes responsibility. Add `_Static_assert(sizeof
(PatternSet) == 112u, ...)` in the owning header. `PAT_DEFAULT_NOTE` must also
leave this header (section B), so deleting `PatternData.h` dependencies does
not accidentally retain PatternData in DSP-only files.

**Remove UI mirroring from the storage owner.**

> PatternData owns bitmap persistence, not the selected-step cursor or menu
> parameter cache. Inputs to `pat_setSelectedStep()`, `pat_applyStepToMenu()`,
> and `pat_applyPatternSettingsToMenu()` affect only UI values or clear values
> whose backing fields are being removed. Their outputs are therefore obsolete.
> Affiliates are button handling, menu parsing, load completion, and LED UI
> refresh. Removing the bridge preserves a single ownership direction: UI
> reads Scene MIDI settings directly and PatternData only supplies on-bits.

Delete those functions and their callers. Rework
`pat_applyTrackSettingsToMenu()` only if it is narrowed to copying retained
`scene_settings_t.midi_channel[]` / `midi_note[]`; preferably make menu code
read those SceneData accessors directly and delete the PatternData-to-menu
bridge entirely.

### B. Scene ownership and the default trigger note — `SceneData.h/.c`, `MidiNoteNumbers.h`, `Oscillator.c`, and `SomGenerator.c`

**Keep Scene MIDI routing outside the bitmap.**

> `scene_settings_t.midi_channel[NUM_TRACKS]` and `midi_note[NUM_TRACKS]`
> remain the owner of per-track MIDI routing and note overrides. Inputs are a
> Scene and track; outputs are the configured channel/note values used by MIDI
> parsing and the UI. These arrays are already present outside `PatternSet` and
> must neither be counted as bitmap storage nor moved into it. Affiliates are
> MidiParser, the retained MIDI menu cells, and filesystem scene settings.

`scene_t` continues to embed a `PatternSet`; update the expected structural
assertion to `sizeof(scene_t) == 1312u` only after compiling with the target
ABI. `scene_initAll()` retains its per-Scene `pat_initScene()` call, now the
only persistent pattern initialization. The Scene loader’s default note setup
must use the relocated default constant below.

**Relocate the generic default note and sever an inappropriate include.**

> A default trigger note is a MIDI-domain constant, not PatternData storage.
> Its value remains 63. Inputs are callers that need a deterministic note when
> no per-step note exists; output is that fixed MIDI note. Affiliates are the
> sequencer, SOM generator, MIDI parser, filesystem default Scene setup, and
> oscillator code. Moving it to a small MIDI-number header lets DSP oscillator
> code stop including PatternData solely for `PAT_DEFAULT_NOTE`.

Define a clearly named constant such as `MIDI_DEFAULT_TRIGGER_NOTE` in the
existing shared MIDI-note header, replace every `PAT_DEFAULT_NOTE` use, and
remove `#include "PatternData.h"` from `Core/DSPAudio/Oscillator.c` after
confirming it has no remaining PatternData call. This is a genuine dependency
compaction, not merely a rename.

### C. Fixed-grid playback and recording — `sequencer.h` and `sequencer.c`

**Replace variable per-track scheduling with the fixed 16-step scheduler.**

> The sequencer advances all seven tracks on the same sixteenth-note event.
> Inputs are the existing transport clock/tick and each track’s current cursor;
> output is the next `0..15` step index and, when its bitmap bit is set, one
> default trigger event. No track length, scale ratio, shuffle due-time,
> rotation, or pattern-next decision is read or cached. Affiliates are the
> transport reset/realignment functions, trigger jack clocking, LED chase
> updates, and PatternData bitmap reads.

Remove `seq_trackEventBaseTick`, `seq_trackEventCount`, scaled/due-event
scheduler state, rotation offsets, random next-pattern selection, and the
associated PatternData reads. At the existing base sixteenth-note cadence,
advance each track modulo 16 and query its current on-bit. Preserve the
existing transport-facing APIs where callers (for example trigger-jack clock
input) need them, but simplify their implementation so they reset/re-align all
track cursors to the same master 16-step position. Rename an internal
`seq_resetScaledScheduler()` to a fixed-grid name if it remains; do not retain
“scaled” state that is permanently 1:1.

The implementation must explicitly re-check first-step semantics against the
current `-1` cursor initialization and Scene-hot-swap realignment, rather than
assuming modulo arithmetic has the same pre-increment order. That is a timing
compatibility check, not additional retained pattern state.

**Make a played or recorded event bitmap-only.**

> For an active bitmap step, playback calls the existing voice trigger with
> fixed velocity 100 and `MIDI_DEFAULT_TRIGGER_NOTE`; probability is
> unconditional. In erase mode the same coordinate is cleared. Live recording
> quantizes to the current track cursor and sets its bit, deliberately ignoring
> incoming note and velocity because the new storage has no destination for
> them. Affiliates are MIDI note input, roll recording, LED record dirties,
> and PatternData’s set/clear API.

Delete the `Step` snapshot in `seq_triggerVoice()` and remove probability,
stored-note, and stored-volume reads. Audit all `seq_addNote()` call sites
before changing its signature: if callers only supply note/velocity for the
deleted fields, replace it with a clearly named `seq_recordTrigger(track)`;
otherwise retain a narrow compatibility adapter only while an external caller
still requires the old signature. Do not store ignored note/velocity in a new
array.

**Delete step automation playback/recording.**

> Step automation requires a per-step destination and value, both retired with
> `Step`. Inputs formerly passed to `seq_recordAutomation()` have no storage
> target; its output must therefore disappear rather than becoming hidden
> state. Affiliates are MidiParser CC handling, preset application, the
> sequencer automation-node array, PatternData automation APIs, and the
> automationNode compilation unit.

Remove `seq_recordAutomation()` from the header and source, the
`seq_automationNodes[NUM_TRACKS][2]` static allocation, its initialization,
and its parse/update path. After the source-wide search is clean, remove
`Core/DSPAudio/automationNode.c` from `Makefile` and delete its header/source;
the audit found its only functional client is this sequencer path. Check
`NO_AUTOMATION` separately before deleting it: remove the constant only if no
unrelated MIDI or modulation code still uses it.

### D. User interface and presentation — `menuPages.h`, `ParameterArray.h`, `menu.c`, `buttonHandler.c`, `ledHandler.c`, and `copyClearTools.c`

**Remove controls that have no persistent or runtime meaning.**

> The sequencer page exposes only controls with a retained owner. Inputs from
> the removed step velocity/note/probability/skip/parameter destination/value,
> track length/scale/shuffle/rotation, and pattern-next/change cells must not
> be accepted. Outputs are a page containing on/off step workflow plus the
> retained Scene MIDI channel/note controls, if that placement is desired.
> Affiliates are parameter IDs, menu type metadata, parser branches, button
> page switching, load-completion refresh, and PatternData menu mirrors.

Replace the two-half STEP page with one supported track-MIDI page (or blank
unused cells) and remove `menu_toggleStepTrackSettingsHalf()` and
`menu_showStepTrackSettingsFirstHalf()`. Delete the matching `PAR_STEP_*`,
automation, shuffle, pattern-beat/next, track-length/scale, and rotation
parameter IDs, their `menu.c` data-type entries, parser cases, and request-
pattern apply plumbing. Keep `PAR_ACTIVE_STEP` only if it is still the
ephemeral UI cursor; it is not Scene storage. Reindexing parameter IDs is safe
only in the same release that retires `.all` / `.prf` streams, because those
legacy files serialize positional parameter values.

`Core/Menu/MenuText.h` contains `nextPatternNames` solely as a retained display
table for the removed Pattern-next data type. Delete it with the last
`DTYPE_MENU`/parameter reference rather than retaining stale labels that make
the retired feature appear supported.

The existing retained MIDI channel/note parser branches must be rewritten to
call SceneData directly and their comments corrected from “PatternData-owned”
to “Scene settings-owned.” Remove `menu_soundApplyRequestPattern` and related
load/apply completion flags only after every call is gone; do not leave an
always-false compatibility field.

**Preserve generic long-press mechanics, remove only automation gestures.**

> A selected step is a UI cursor plus one bitmap coordinate; it no longer
> opens a per-step editor or arms automation. Inputs are front-panel press and
> release events; outputs are toggle/select/LED updates for the bitmap. The
> generic timer machinery remains where other button gestures use it.
> Affiliates are button state, menu page selection, PatternData bitmap calls,
> and LED dirties.

Remove automation arming/disarming calls, per-step page selection, and
rotation/performance-shift dispatch from `buttonHandler.c`. Do not delete
`buttonHandler_TimerActionOccured` wholesale: source inspection shows the
timer helper participates in other gestures. Delete only automation-specific
state and wording after checking each use. A removed rotation gesture must be
removed from the dispatch and blink feedback, not changed into a silent
no-op; a different replacement gesture would need a separate product choice.

**Make LEDs consume bits only and remove already-obsolete compatibility work.**

> LED rendering receives a track, viewed Scene, and step/bar context, then
> queries PatternData’s on-bit to set the corresponding physical step LED.
> It has no authority to reconstruct main-step masks, automation substeps, or
> stored rotation. Affiliates are the sequencer dirty state, button selection,
> copy/clear, and Euclidean generator.

Keep `led_updatePatternTrackView()`, `led_updateRecordedMainStep()`, and chase
display with bitmap queries. Remove its adjacent
`pat_applyTrackSettingsToMenu()` calls. `led_updateRecordedSubStep()` is
already a no-op compatibility endpoint; remove it, its header declaration,
`SEQ_LED_DIRTY_REC_SUB`, `recordSubStep`, and the foreground dirty dispatch.
Also remove `led_notifyTrackRotationReset()` and its call sites because no
rotation exists.

**Restrict copy/clear tools to bitmap semantics.**

> Copy/clear commands take validated source/destination Scene, track, and bar
> coordinates and mutate only the relevant 112-byte bitmap ranges. Outputs are
> copied or cleared bits and the existing presentation refresh; they do not
> copy automation or extend a track. Affiliates are PatternData copy helpers,
> menu choices, LEDs, and Scene index validation.

Retain track/pattern/bar copy with their new byte sizes, delete
`copyClear_clearTrackAutom()` and automation clear menu choices/cases, and
ensure Scene indices are bounds-checked rather than relying on the current
`& 0x0f` masking as validation.

### E. MIDI and preset application — `MidiParser.c`, `presetManager.h/.c`, and `SeqStep.h`

**Keep MIDI routing and live sound application, remove automation recording.**

> MIDI parsing continues to obtain the per-track channel and note from
> SceneData and routes normal notes/CCs to the existing runtime sound paths.
> Its former automation-record calls have no per-step destination after the
> bitmap migration and must be deleted. Inputs and outputs of ordinary MIDI
> handling are otherwise unchanged. Affiliates are sequencer recording,
> preset application, SceneData accessors, and the removed automation module.

Delete the two `seq_recordAutomation()` calls from CC/morph paths while
preserving their live parameter effects. Update comments that inaccurately
attribute MIDI settings to PatternData.

> Preset application applies a value to runtime sound state and emits any
> required MIDI-side effect; it no longer conditionally records that value into
> a step. Its input is the parameter/value pair, its output is live sound
> state, and its affiliates are menu edits, button reset/lock actions, and
> MidiParser. Removing the `recordAutomation` argument prevents a dead boolean
> from falsely advertising persistence.

Make `preset_applySoundParameter()` two-argument and remove the propagation of
`recordAutomation` through its internal helper once the source-wide caller
audit confirms it is unused. Remove an automation-destination variable only if
it has no remaining non-step role. `Core/MIDI/SeqStep.h` has comments describing
the retired full `Step` compatibility model and the source search finds no
include of it. Delete the dead header rather than revising an unused duplicate
of the removed layout.

### F. Generators — `EuklidGenerator.c` and `SomGenerator.c`

**Generate on/off pulses without restoring track metadata.**

> Euclidean transfer receives its existing generator parameters, clears the
> selected bitmap track, and sets bits for generated pulse positions. Its
> output is only a 128-step on/off bitmap. The generator’s own temporary
> Euclidean control arrays remain runtime generator state, not Scene pattern
> storage. Affiliates are PatternData bounded helpers, UI/LED refresh, and the
> fixed-grid sequencer.

Replace direct `Step` active-bit mutation with `pat_clearTrack()` plus bounded
bit sets. Remove the final `pat_setTrackLength()` call: Euclidean length may
still define the generator’s pulse coverage but must not create persistent
sequencer timing. `SomGenerator.c` only needs the relocated default trigger
note; it must not retain a PatternData include merely for that constant.

### G. Persistence — `storageTypes.h/.c`, `filesystem.h/.c`, `presetManager.h/.c`, and `menu.c`

**Make `pattern.pat` v3 the only emitted pattern representation.**

> The v3 text parser/writer transfers exactly seven 16-byte bitmap tracks.
> Input rows are `track1` through `track7`, each with 32 high-nibble-first hex
> characters; output is the corresponding `PatternSet` bits. Byte order is
> increasing and bit 0 is the lowest-numbered step. Affiliates are Scene/Bank
> directory load/save, PatternData’s bounded PatternSet helpers, fixture tests,
> and filesystem error reporting. This contract makes the 112-byte payload
> visible and prevents file loading from gaining raw-layout authority.

Change `storage_pattern_stub_state_t` and formatter/parser from v2
length/scale plus 128 characters to v3 track bytes. Require all seven rows and
track a seen-row mask. Continue to accept v1 as an empty pattern and v2 as a
one-way import of its on/off characters only; ignore its length/scale and emit
v3 on the next save. Parser writes must go through the PatternData bitmap
helper, not direct `step_on` indexing. Update `filesystem.h` comments at the
same time so the public contract matches code.

**Delete the hidden generic Step stream end-to-end.**

> Legacy Pattern, Performance, and All container operations encode `Step`
> records, main-step fields, pattern settings, lengths, rotations, scales, and
> positional menu parameters. Their inputs cannot be losslessly represented by
> the bitmap; retaining their stream functions would retain the deleted types
> and invalidate the storage audit. The output of a request for these retired
> formats is an explicit unavailable/unsupported result, not a partial load.
> Affiliates are filesystem file descriptors and dispatch, stream state,
> PresetManager operations and callbacks, menu completion handling, and
> parameter ID numbering.

Remove `FS_FILE_PATTERN`, `FS_FILE_PERFORMANCE`, and `FS_FILE_ALL`; their
request dispatch branches; `filesystem_patternStepAddress`, discard `Step` and
main-step objects, pointer helpers, pack/unpack helpers, and all four generic
Pattern/All/Performance save/load state machines. Remove matching PresetManager
public wrappers, operation enums/callbacks, and menu completion cases. Do not
leave unreachable helpers because merely compiling them requires the old
`Step` layout.

**Simplify the live Scene/Bank loader rather than staging a second pattern.**

> Scene directory loading writes one selected final Scene pattern through the
> v3 bitmap parser, then the existing selected-Scene copy phase copies the
> resulting 112-byte `PatternSet` to other targets. Input is a text
> `pattern.pat`; output is an initialized bitmap in the final Scene. Affiliates
> are the scene load phase machine, `filesystem_directPatternTarget()`, Scene
> stage commit, and storageTypes. No seventeenth full Scene staging object is
> introduced.

Remove binary `pattern.pat` probe/read phases and their `Step` scratch buffers;
after the format probe, reject non-text legacy binary with a clear unsupported
format error. Update `filesystem_commitSceneStage()` to use the new
`pat_initPatternSet(&target->pattern)` signature. Change its default MIDI note
to `MIDI_DEFAULT_TRIGGER_NOTE`. Keep `fs_stage_workspace` unchanged because it
does not own a PatternSet.

### H. Slider LUT — `adcPots.c`, `adcPots.h`, `config.h`, and `mixer.c`

**Halve LUT nodes without adding interpolation or reducing float precision.**

> The slider transfer cache contains 2,048 native `float` values. Input is a
> 12-bit ADC sample; the lookup index is `(raw & 0x0fffu) >> 1`, so raw pairs
> `(0,1)` through `(4094,4095)` share one node. Output is a full `float` in
> `slider_vol[ADC_POT_COUNT]`, consumed unchanged by the mixer. LUT generation
> evaluates the existing deadzone/log-taper function at even raw codes only.
> There is no interpolation between nodes. Affiliates are ADC initialization,
> foreground slider refresh, the deadzone/taper configuration, and the
> mixer's existing control smoothing.

Add `SLIDER_LUT_ENTRIES 2048u` (and, if helpful, an explicit 12-bit sample
mask) near the existing ADC configuration. Build exactly 2,048 values with
`slider_raw_to_float(index << 1)`. Both `adc_init()` and `adc_checkPots()` must
use the same masked, shifted lookup. `slider_vol` remains a float array; no
fixed-point or half-float conversion is introduced.

**Remove duplicated cache refresh code without changing its scheduling.**

> ADC initialization and foreground polling both refresh the six cached slider
> gains from DMA raw samples. Their inputs and outputs are identical, so one
> private `slider_refreshVolumes()` function should perform the bounded loop.
> Affiliates are `adc_init()`, `adc_checkPots()`, `slider_lut`, and
> `slider_vol`. Consolidation prevents one call site from later using a
> different LUT indexing rule; it does not add state, interpolation, or an
> audio-rate calculation.

Keep the existing calls in both lifecycle locations: initialization establishes
valid gains before audio use, and polling updates them in the foreground.
`mixer.c` requires no LUT change. Its 32-sample interpolation between the old
and current slider gain remains control smoothing, not LUT interpolation; the
implementation comment should say so to prevent a future “optimization” from
changing the requested no-interpolation LUT behavior.

### I. Build, source hygiene, and ordered implementation

1. First make the PatternData header/source bitmap-only and relocate the
   default note; this forces the compiler to enumerate every old user.
2. Convert sequencer, MIDI/preset, generators, UI, LEDs, and menu parameters
   in one buildable change; delete automationNode only after its last client is
   gone.
3. Replace Scene/Bank text persistence, then remove every generic legacy
   stream operation and its preset/menu hooks. This must precede parameter-ID
   reindexing.
4. Reduce the LUT independently, refactoring only the duplicated refresh loop.
5. Run `rg` for `\bStep\b`, `pat_mainSteps`, `LengthRotate`, `TrackScaleRatio`,
   `pat_apply.*ToMenu`, `seq_recordAutomation`, `automationNode`,
   `FS_FILE_PATTERN`, `FS_FILE_ALL`, `FS_FILE_PERFORMANCE`, and
   `PAT_DEFAULT_NOTE`. Each remaining hit must be either an intentional
   migration test/comment or removed before acceptance.

## Verification and acceptance

### Build and structural checks

1. Build with `make`, then inspect the fresh ELF with `arm-none-eabi-size -A`
   and `arm-none-eabi-nm -S --size-sort`.
2. Confirm `PatternSet == 112`, `scene_t == 1312`, `scenes == 20,992`, and
   `slider_lut == 8,192` bytes. Search the linked symbols and source for
   removed `Step` arrays, `pat_mainSteps`, `LengthRotate`, and binary Step
   stream state.
3. Confirm all SRAM reductions are in SRAM1 and DTCM/DMA section sizes remain
   unchanged unless the linker proves otherwise.

### Deterministic functional checks

1. Unit-test bitmap set/get/toggle/clear/copy at step boundaries 0, 7, 8, 15,
   16, and 127, for every track and for two different resident Scenes.
2. Round-trip a v3 `pattern.pat`; import a v1 placeholder and v2 fixture and
   confirm they produce the expected bitmaps and v3 saves. Confirm binary
   bridge input fails cleanly.
3. Exercise step toggle, bar/track/pattern copy, clear, Euclidean transfer,
   Scene Save/Load, and multi-Scene Bank Save/Load. Confirm only on/off state
   survives and default triggering is deterministic.
4. Sweep each slider slowly over all raw values, especially the two deadzone
   boundaries and every even/odd raw-code pair, while monitoring for
   discontinuities/clicks. Compare each selected float LUT gain against the old
   transfer function at all 4,096 raw inputs; record maximum absolute and dB
   error. Verify full-scale, true silence, and a slow fade of a sustained voice
   on hardware.

### Documentation

Update `MEMORY.md`, `SRAM_DTCM_MANIFEST.md`,
`MODULE_INTERCHANGE_SPEC.md`, and `FILESYSTEM_SPEC.md` only after the measured
implementation is accepted. Record the retired bridge file support and the
v3 bitmap bit ordering so later dynamic-pattern work does not mistake this
evaluation build for the final Pattern architecture.

## Review decisions before implementation

1. Approve the intentional measurement-build behavior: fixed 16-step timing
   and default velocity/note/probability, with step automation and variable
   track timing removed.
2. Approve v3 hex bitmap persistence with v1/v2 text import and explicit
   rejection of legacy binary pattern files.
3. Approve retirement of the hidden legacy Pattern/Performance/All bridge
   streams rather than carrying deleted `Step` serialization types solely for
   compatibility.
4. Use a non-interpolated 2,048-entry native-`float` LUT. Every pair of
   adjacent 12-bit raw ADC codes shares its even-code LUT node.
