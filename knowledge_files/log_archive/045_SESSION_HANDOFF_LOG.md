# Session 045 Handoff — Autosave Baseline, Settings Persistence, And Failed Phase 2 Quarantine

DATE: 2026-07-29 through 2026-08-02; closed and reconciled on 2026-08-06

SESSION GOAL: Establish the smallest observable autosave implementation in
hardware-testable increments: create two hidden records, add a durable
asynchronous ping-pong writer, expand the records into a complete non-Pattern
Bank parameter layout, drain mutation bits through bounded live parameter
gets, move dirty-mask ownership into one canonical SRAM record, hook ordinary
single-parameter changes, and persist AutoSave policy plus Bank/Scene source
metadata. The accepted session boundary is the boot-display correction
reported immediately before the user's August 2 response.

COMPLETED: The retained production source now matches commit `326a8a1`
(`settings format update; initial single-parameters 'get' for autosave`) for
the relevant source and planning files. It contains the 34,768-byte A/B record
format, streamed CRC/generation/commit validation, bounded asynchronous
parameter draining, one 3,856-byte canonical dirty mask, Phase 1 change-aware
scalar hooks, version-1 settings persistence with sixteen Scene-source values,
an AutoSave Global setting, and the boot splash ordering correction. Later
Phase 2 and follow-up attempts were unsuccessful, were rolled out of
production, and survive only in files whose names contain `failed`.

VERIFIED ON HARDWARE:

- Yes: deleting both hidden records allowed the retained creator to recreate
  the pair after the AsyncFATFS free-cluster wrap correction. The earlier
  16,384-byte stop was localized to second-cluster allocation, not an actually
  full card.
- Yes: the dummy/probe writer advanced generations and the one-byte witness in
  alternating files; independent host checks validated record size and CRC.
- Yes: case-folded duplicate hidden targets and the leading-dot short-alias
  collision were identified and addressed in the retained filesystem path.
- Yes: fully dirty test records drained asynchronously over multiple valid
  generations. A roughly 15-second test produced generation 5 with 6,620 bits
  still dirty after draining Bank and Scenes 0-11 plus part of Scene 12; every
  observed payload change corresponded to a cleared source bit.
- Yes: Bank fields and drained Scene parameter blocks were compared with
  `SD_CARD/Bank/000 Full/`; the inspected data matched the source.
- Yes: Phase 1 ordinary Scene-0 parameter edits produced autosave payload
  differences and were validated against `000 Full`. This confirms at least
  the exercised scalar setter/get/dirty path, not the complete Phase 1 matrix.
- Yes: the one-second settings writer produced a complete 33-line
  `settings.cfg`. The retained post-test fixture records these changes:
  `bpm 120 -> 133`, `ext_sync 0 -> 4`, `quantisation 0 -> 2`,
  `bar_reset_mode 0 -> 1`, and `osc_wave_interp 0 -> 1`. `autosave` remained
  enabled, `active_bank` remained zero, and all sixteen Scene sources remained
  encoded as Bank slot 000 (`1000`).
- Partly: healthy boot with diagnostic file logging enabled did not produce an
  observed hang or timeout log. The forced ten-second timeout, abandon,
  remount, and exact eight-byte `bootlog.bin` path was not hardware-forced.
- Not yet: the final splash-hold correction was built but the session was
  closed at the message announcing that change, before the user's response.
  Do not turn the build result into a hardware-verification claim.
- Not yet: the complete `AUTOSAVE_SETTINGS.md` matrix, the full Phase 1 scalar
  matrix, power-cut boundaries, corruption recovery, AutoSave OFF/ON, settings
  write concurrency/error retry, and a long clean-mask no-I/O observation all
  remain open.

CHANGES THIS SESSION:

- `Core/Bank/Scene/Autosave.c/.h`: owns the final 34,768-byte wire geometry,
  pure initial formatter, streaming validation and CRC32C, generation/probe
  transforms, live Bank/Scene/Kit/Instrument byte projection, the sole
  canonical mutation mask, atomic mask operations, typed scalar dirty markers,
  whole-region marker stubs, and explicit future Effect/copy-paste scopes.
- `Core/Hardware/SD/filesystem.c/.h`: creates missing A/B records, schedules
  the asynchronous writer, validates/selects a winner, regenerates both when
  neither validates, bounds mutation classification and live gets, performs
  the one-stream copy/CRC/CRC-write/commit-last publication, persists
  `settings.cfg`, and owns boot timeout logging.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h`: retained free-cluster scan
  wrap behavior and case-insensitive hidden-target cleanup support used by the
  writer.
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h`: diagnostic-only transfer
  abandon support used by bounded boot-timeout recovery.
- `Core/Bank/BankData.c/.h`: change-aware Bank-field owners mark restore slot,
  display name, Scene-present mask, active Scene, and VOICE edit mask.
- `Core/Bank/Scene/SceneData.c/.h`: owns exactly sixteen `uint16_t` Scene-source
  values (32 bytes), change-aware Scene and Kit scalar stores, and typed source
  encoding helpers.
- `Core/Bank/Scene/Preset/presetManager.c/.h`: routes ordinary Instrument
  normal/Morph endpoint changes through generic changed-value stores; records
  successful root Scene/Bank provenance at public completion boundaries; and
  suppresses the premature pre-audio repaint while retaining runtime Settings
  Load repaint behavior.
- `Core/Bank/Scene/Preset/ParameterArray.h`: appends the AutoSave setting inside
  the existing fixed parameter array without renumbering earlier parameters.
- `Core/Menu/menu.c`, `menu.h`, `menuPages.h`, and `MenuText.h`: expose
  `ats` / Global / `AutoSave`, default it ON, apply policy on changed user
  commits, and dirty settings from changed Global cells.
- `config.h`: retains separate diagnostic-screen and diagnostic-file flags,
  the ten-second boot logging deadline, five-second autosave debounce, 250 ms
  continuation, 1,536-get cap, 256-position foreground scan cap, and one-second
  settings debounce.
- `main.c`: loads settings before selecting the boot Bank and before autosave
  setup, enables runtime settings/autosave scheduling only after the pre-audio
  filesystem ladder, owns the bounded boot-timeout exit, and keeps the splash
  visible through pre-audio settings apply.
- `Makefile`: compiles the Autosave module.
- `SD_CARD/.hcprms1`, `SD_CARD/.hcprms2`, and
  `SD_CARD/hcprms_post/`: retained binary fixtures and hardware-returned
  evidence for format/drain inspection.
- `SD_CARD/settings.cfg`: contains the implemented 33-line version-1 schema.
- `AUTOSAVE_FILES.md`, `AUTOSAVE_WRITER.md`, `AUTOSAVE_PARAMETERS.md`,
  `AUTOSAVE_SINGLE_RECORD.md`, `AUTOSAVE_PARAM_HOOK.md`,
  `AUTOSAVE_SETTINGS.md`, and `BOOT_LOGGING.md`: retain design, implementation,
  and verification notes. Their historical status lines must be read together
  with this handoff; some say hardware remained pending even though a later
  chat test covered part of the matrix.
- `build/LXRV2_lxr02.img`: checked-in rollback artifact is 368,012 bytes.
  `AUTOSAVE_SETTINGS.md` reports a 368,132-byte packaged image, while the
  current ELF was produced by the later failed branch and is not a valid size
  witness for this baseline. Rebuild-derived size verification remains open.

KNOWN ISSUES INTRODUCED:

- The accepted baseline does not hook whole-object Kit, Scene, Instrument,
  Morph-projection, or Bank Load/Save commits. Phase 1 covers ordinary scalar
  owner changes; Phase 2 remains unimplemented.
- Initial creation is name/identity plus zero parameter data. A complete
  working register currently depends on recovery from an existing file mask or
  a deliberately dirty test fixture; creation alone must not be mistaken for a
  complete saved Bank image.
- The retained validator still compares record Bank slot/name with live Bank
  identity even though those values also exist as mutable payload fields. That
  contract tension was exposed later and needs explicit review/testing before
  any ownership expansion.
- The accepted scheduler prevents a new writer start while Load or Save is the
  active page, but it does not provide the later attempted transaction-wide
  exclusion/entry deferral. Do not claim that an operation already in progress
  cannot overlap a Load/Save session.
- `settings.cfg` still truncates and rewrites the live file through the
  existing writer; no atomic temporary-file replacement was added.
- Current documentation and packaged-image sizes disagree by 120 bytes. The
  production source boundary is clear, but the artifact-size record needs a
  clean baseline rebuild before it is treated as authoritative.

KNOWN ISSUES RESOLVED:

- Hidden autosave names are exactly `/.hcprms1` and `/.hcprms2`, including the
  leading dot.
- The two files have explicit generation, CRC, final commit, and deterministic
  tie/wrap selection instead of relying on Bank slot as recency.
- Duplicate case-folded hidden targets are retired before inactive-target
  creation.
- A leading dot no longer causes `.hcprms1` and `.hcprms2` to derive the same
  `FILE.HCP` short alias.
- AsyncFATFS free-cluster allocation wraps from the end of its hint range back
  to cluster 2 before declaring a filesystem full; the observed one-cluster
  creator freeze no longer silently retries forever.
- A fully empty canonical mask falls through before a periodic file write;
  remaining backlog uses a bounded continuation rather than another full
  five-second delay.
- The mutation mask has one retained owner in `Autosave.c`; the filesystem
  cache retains only patch offsets and values.
- CRC is calculated from the same transformed copy stream that creates the
  target. The target is durable with CRC/commit clear, then CRC is written and
  synced, and the valid commit marker is written last.
- Diagnostic screen output and diagnostic file logging are separate modes.
  Screen diagnostics are disabled; file logging is enabled and must not print
  or wait on the display.
- The pre-audio settings apply no longer exposes the first VOICE page before
  Bank/fallback parameters have been applied.

NEXT SESSION RECOMMENDED GOAL: Re-establish an evidence-backed baseline before
Phase 2. Review and execute the complete `AUTOSAVE_SETTINGS.md` hardware matrix,
define non-perturbing observability for every autosave admission/transaction
boundary, rerun the retained Phase 1 scalar tests from known clean records,
then review `AUTOSAVE_PARAM_HOOK.md` Phase 2 against the exact current source.
These are topics for investigation and testing, not authorization for a code
change.

BLOCKERS:

- No runtime trace currently distinguishes a dirty producer, scheduler
  observation, operation admission, winner validation, mask merge, parameter
  capture, or final commit. Final file state alone cannot identify which gate
  rejected work.
- Scene-source persistence was only confirmed for the initial Bank-derived
  values. Root Scene/partial Bank load and save provenance still need direct
  tests.
- The full AutoSave OFF/ON lifecycle has not been proven on hardware.
- The current `.hcprms` fixtures have been reused across several tests. Every
  future result needs its exact starting generation, CRC, mask population, and
  expected payload recorded before power-on.
- All post-cutoff Phase 2 code is rejected. Its presence in `failed` files is
  diagnostic evidence, not a shortcut around re-analysis.

CRITICAL REMINDERS FOR NEXT SESSION:

- Production source is the `326a8a1` state. Current branch
  `dev-ph3-autosave-retry` deliberately carries later code only under names
  containing `failed`; those files must not be compiled or copied back by
  mechanical comparison.
- There must remain exactly one 3,856-byte canonical mutation mask. A file
  mask is a completeness/recovery copy, not an independent live owner.
- Mutation producers set bits only in SRAM. They do not open files, poll the
  card, print diagnostics, or schedule a transaction directly.
- A clean mask must produce no hidden-file I/O after the one-time recovery
  obligation is complete.
- Parameter changes may occur while a drain is active. Atomic take/re-dirty
  behavior is part of the retained Phase 1 contract and must be explicitly
  exercised.
- Bank name, Bank slot, active Scene, names, and other metadata are payload
  data unless a separately reviewed format rule says otherwise. Do not invent
  identity-discard semantics from display-name mismatches.
- Bank Load/Save masks are selective. A partial Bank operation does not imply
  replacement of all sixteen resident Scenes.
- Bank Save reads resident data; it must not be treated automatically as a
  whole-workspace parameter mutation.
- `DEV_MODE_DIAGNOSTIC` may display runtime information but must never add file
  interaction. `DEV_MODE_LOGGING` may write operation codes but must never
  print or add screen-delay timing perturbations.
- Build, symbol, CRC, and file-size checks are necessary but do not prove that
  the runtime scheduler is reachable.

---

## 1. Accepted boundary and repository authority

The user explicitly closed Session 045 at the August 2 message reporting the
boot splash correction. The next user response and every later Phase 2 change
belong to the failed post-boundary branch documented in section 12.

The repository was later restored so the production files are identical to
commit:

```text
326a8a171a68b90b7861f78f21107e1b2212967a
settings format update; initial single-parameters 'get' for autosave
2026-08-02 10:33:06 +0200
```

Current HEAD is `81b6e6f` on `dev-ph3-autosave-retry`; it adds the rejected
sources and notes under `failed` filenames. A direct source comparison between
HEAD and `326a8a1` for the production Autosave, BankData, SceneData, Preset,
filesystem, Menu, `main.c`, `config.h`, and accepted planning files is clean.

At closeout, `AUTOSAVE_PARAM_HOOK_failed.md` is untracked. This handoff neither
adds it nor changes any source/fixture/build file.

## 2. Chronological accepted work

### 2.1 Two-file creation baseline

The work began by replacing a failed prior `Autosave.c/.h` attempt with the
smallest boot-visible operation: after normal mount/scans and successful
Bank/fallback selection, create two hidden root records if they do not exist.
No autosave payload write was initially allowed.

Names were populated from the existing HCNAMES authority and zero-padded into
their fixed regions. A fallback without a resident Bank leaves autosave
inactive. Existing files are preserved by the creation-only pass.

The user fixed the filenames as `/.hcprms1` and `/.hcprms2`; both leading dots
are part of the contract.

### 2.2 Header, generation, corruption detection, and durability

A 64-byte control header was placed before the mask/payload. It contains:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | `HCPR` magic |
| 4 | 1 | format version 1 |
| 5 | 1 | final valid marker `0xA5` |
| 8 | 4 | little-endian generation |
| 12 | 4 | little-endian CRC32C |
| 16 | 1 | wrapping probe/dummy counter |
| 17..63 | 47 | reserved control bytes |

CRC uses reflected Castagnoli CRC32C over the complete record, treating stored
CRC bytes 12..15 as zero. Among valid peers, wrap-safe generation comparison
selects the newest; A wins a tie. The probe byte was the initial hardware
witness but never selects recency.

The inactive target is published in this order:

1. copy the transformed record with stored CRC and commit clear;
2. close and sync the copy;
3. write the calculated CRC, close, and sync;
4. write `0xA5` as the final valid marker;
5. close and pass the normal final sync gate.

A reset before step 4 leaves the prior valid peer available.

### 2.3 Dummy writer and AsyncFATFS defects exposed by hardware

The autonomous writer runs through `filesystem_tick()` and never blocks the
audio runtime. It streams in 512-byte chunks and uses AsyncFATFS's one operation
owner.

Hardware testing exposed three separate filename/allocation defects:

1. case-sensitive LFN target creation could leave two same-folded
   `.hcprms2` directory entries;
2. treating the leading dot as an extension separator derived the same initial
   SFN alias for both hidden names;
3. a new file stopped at exactly 16,384 bytes, one cluster on the test card.

The third failure was not a full card. Free-cluster scanning began at
`lastClusterAllocated`, searched only to volume end, and set sticky
`filesystemFull` without wrapping to cluster 2. Allocation now has two bounded
passes. The creator and runtime writers also distinguish temporary zero-byte
back-pressure from terminal full/error state instead of retrying forever.

### 2.4 Boot timeout logging

An overly broad proposed directory-scan repair was rejected. The accepted
diagnostic instead stores the exact current boot filesystem operation as eight
bytes, arms a ten-second deadline, abandons/remounts once after a timeout, and
makes one bounded root `bootlog.bin` attempt before continuing to audio.

The active flags were subsequently corrected into exactly two modes:

- `DEV_MODE_DIAGNOSTIC = 0`: screen-only boot operation/phase display;
- `DEV_MODE_LOGGING = 1`: file-only eight-byte operation logging.

The intended separation comments are present beside the config hooks and
individual operations. The healthy path does not create a new log simply
because logging is enabled. No forced timeout was committed or hardware-tested.

### 2.5 Final record geometry

The retained fixed record is exactly:

```text
64-byte control header
3,856-byte bit-per-payload-byte mutation mask
30,848-byte payload
----------------------------------------------
34,768 bytes total
```

The payload is:

```text
128-byte Bank
16 * 1,920-byte Scene
```

Each Scene is:

```text
128-byte Scene section
512-byte reserved Effects section
1,280-byte Kit
```

Each Kit is:

```text
128-byte Kit header/parameter section
6 * 192-byte Instrument
```

Each Instrument is:

```text
3-byte type token
8-byte zero-padded name
72-byte descriptor-indexed Normal endpoint
72-byte descriptor-indexed Morph endpoint
37-byte padding
```

The mask covers payload-relative offsets only. Bit N describes payload byte N,
LSB first within its mask byte. It does not cover the 64-byte header.

Current live projection includes fifteen Bank bytes, forty Scene parameter
bytes per present Scene, two Kit parameter bytes per present Scene, Instrument
type tokens, active descriptor-indexed Normal values, and Morph values only for
Morphable descriptors. Pattern is absent. Effect has a 512-byte reserved region
and a zero live count.

Names are initially formatted from HCNAMES. Not every name has a runtime live
getter, so name creation and later dirty projection must not be conflated.

### 2.6 Bounded parameter drain

The initial test hand-marked file mutation bits because runtime dirty producers
did not yet exist. The writer selects a valid winner, OR-merges its file-carried
mask into SRAM, classifies set bits, gets currently existing live bytes, clears
nonexistent/reserved cells without a live request, patches captured values into
the inactive target, and carries the remaining mask forward.

Current limits are:

| Setting | Value | Meaning |
| --- | ---: | --- |
| `AUTOSAVE_WRITER_INTERVAL_MS` | 5,000 ms | debounce/retry interval |
| `AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` | 250 ms | durable backlog continuation |
| `AUTOSAVE_PARAMETER_GETS_PER_WRITE` | 1,536 | maximum successful live-byte captures per generation |
| `AUTOSAVE_MASK_BITS_PER_TICK` | 256 | maximum payload positions examined per foreground call |

Padding, absent objects, unknown types, out-of-range descriptors, and
non-Morphable Morph cells consume classification work but not the 1,536-get
budget.

The 8,464-byte retained autosave working allocation is:

```text
Autosave canonical mask                    3,856 bytes
filesystem patch offsets: 1,536 * 2        3,072 bytes
filesystem patch values:  1,536 * 1        1,536 bytes
                                            -----------
total                                      8,464 bytes
```

The separate 9,000-byte library-name cache is not dual-used by the accepted
implementation.

### 2.7 Single canonical dirty record

Mask ownership was corrected before parameter producers were added. The one
3,856-byte array now has static lifetime in `Autosave.c`. A selected file mask
is recovery/completeness input and is ORed into that array; it never replaces
or becomes a second live owner.

Operation initialization clears only transaction-local patch state. Failed
publication restores every captured offset to the canonical mask. A successful
clean result disarms periodic file work; remaining canonical bits choose the
250 ms continuation.

The writer takes one mask bit atomically before sampling it. A concurrent
producer that re-dirties the same byte after that take leaves the bit set for a
later generation. This prevents a foreground clear from erasing a newer
interrupt-side change.

### 2.8 Phase 1 ordinary parameter hooks

Phase 1 keeps all wire-offset arithmetic in `Autosave.c`; BankData, SceneData,
and Preset pass typed coordinates rather than raw offsets.

Retained hooks include:

- Bank restore slot, display name, Scene-present mask, active Scene, and VOICE
  edit mask;
- all forty currently live Scene parameter bytes;
- both generated Kit decay endpoint bytes;
- each type's descriptor-indexed Instrument Normal endpoint;
- Morph endpoint values only for descriptors marked Morphable;
- supplemental serialized descriptor values through the same generic Preset
  store where applicable.

Each owner compares the normalized final value, stores it first, then sets the
matching canonical bit only if the value changed. Initialization and staging
are not dirty mutations.

The source also retains explicit, currently uncalled region scopes for:

- Whole Instrument, including type;
- same-type Instrument Normal;
- same-type Instrument Morph;
- Kit;
- Scene without Pattern;
- future Scene with Pattern;
- future Effect.

Effect single/region hooks are deliberate no-ops with zero live parameters.
They reserve an obvious extension boundary without allocating fake state.

Phase 1 does not publish complete object loads. It must not be described as
covering a raw whole-Scene/Kit/Instrument assignment merely because scalar
setters are covered.

## 3. Settings and source provenance retained at the cutoff

### 3.1 Version-1 schema

`settings.cfg` remains keyed text format version 1. The writer emits 33 lines:

1. format;
2. version;
3. active Bank;
4. thirteen existing Global values;
5. `autosave`;
6. sixteen `scene_source_00..15` values.

AutoSave defaults ON. Each Scene source is a `uint16_t`:

| Value | Meaning |
| ---: | --- |
| 0..999 | root Scene library slot |
| 1000..1999 | root Bank slot |
| 65535 | unknown/unrecorded |
| 2000..65534 | invalid/reserved |

For a Bank source, the resident Scene index implies the Bank child index.
Exactly sixteen values consume 32 bytes of SRAM. Provenance itself does not set
an autosave payload mutation bit.

### 3.2 Persistence boundaries

The retained design updates provenance only after a successful public Preset
completion:

- root Scene Load/Save updates only the accepted destination/source Scene;
- Bank Load uses the final actually loaded-child mask;
- Bank Save uses the retained selected mask;
- failures update neither provenance nor settings dirty state.

Global menu commits dirty settings only when the normalized byte changes. A
one-second trailing debounce coalesces changes. A change revision prevents a
write that began earlier from acknowledging a later edit. Errors retain dirty
work for retry.

The settings writer remains active even while AutoSave is OFF.

### 3.3 AutoSave policy

`ats` is displayed under Global with long label `AutoSave` and on/off dtype.
OFF stops mutation production and suppresses new hidden-file setup,
validation, recovery, and drain starts; it does not delete or rewrite either
record. An already active transaction is permitted to reach its close/flush
boundary before its retained work is discarded. ON queues setup only for a
resident Bank after runtime authorization.

The policy is loaded before initial Bank selection and before optional
autosave ensure. Runtime settings and autosave schedulers are opened only after
the pre-audio filesystem ladder releases ownership.

### 3.4 Boot splash correction

Moving settings earlier exposed an ordinary `menu_repaintAll()` before the
Bank/fallback payload was ready. The pre-audio `menu_startGlobalApply()` path
now applies settings without repainting. The first parameter repaint belongs
to the Bank/Scene/Kit sound apply after
`preset_sendDrumsetParameters()` completes. If no sound source loads,
`menu_start()` releases the splash at the final boot boundary. Runtime Settings
Load retains normal repaint behavior.

## 4. Accepted hardware evidence in detail

### 4.1 Partial full-mask drain

The most useful bounded-drain run began from both records fully dirty. After
roughly fifteen seconds:

| File | Generation/probe | Dirty bits | Progress |
| --- | --- | ---: | --- |
| `.hcprms2` | generation 4 / probe 3 | 12,461 | Bank and Scenes 0-8 complete; Scene 9 partial |
| `.hcprms1` | generation 5 / probe 4 | 6,620 | Bank and Scenes 0-11 complete; Scene 12 partial |

Both files were exactly 34,768 bytes, carried commit `0xA5`, and passed an
independent CRC32C calculation. Between the initial fixture and generation 4,
18,387 bits cleared and 2,592 payload bytes changed. Between generations 4 and
5, another 5,841 bits cleared and 931 payload bytes changed.

No observed clear bit re-dirtied in that test. No payload byte changed when its
source-generation bit was already clear, and no changed payload byte remained
dirty in the output generation.

Bank output was slot 0/name `Full`, present mask `0xffff`, active Scene 6, and
VOICE edit mask `0x0040`. Inspected Scene blocks matched `000 Full`, including
decimation 127, MIDI channels 1-7, and MIDI notes 63.

### 4.2 Phase 1 ordinary edit evidence

The user later changed parameters in Scene 0 and supplied another A/B pair.
The changed payload was compared against `SD_CARD/Bank/000 Full/00 Slak/` and
the exercised values were consistent with the edited resident state. This was
accepted as evidence that ordinary menu edits can set canonical bits and be
sampled by the writer.

The chat did not close a complete category-by-category matrix. Do not infer
untested Bank scalar, every Instrument type, Morph, supplemental selector,
generated Kit byte, MIDI input, concurrent re-dirty, or identical-value no-op
behavior from the Scene-0 test alone.

### 4.3 Settings evidence

The hardware-returned settings file was structurally complete and recorded the
five Global changes listed in the template block above. The source fields
remained Bank slot 000 for every Scene. This verifies an ordinary burst of
Global menu changes and one complete background write; it does not verify
provenance transitions or policy OFF/ON.

## 5. General topics recommended before further implementation

These topics are deliberately phrased as review/test areas. They do not
authorize or prescribe a source change.

### Topic A — Reconcile `AUTOSAVE_SETTINGS.md` with current source and fixtures

- Verify all 33 keys, defaults, strict parse behavior, and writer ordering.
- Exercise active Bank persistence and every Scene-source transition for root
  Scene and partial Bank Load/Save.
- Verify failed requests leave sources and settings dirty state unchanged.
- Test trailing debounce, a second edit during an active stream/flush, and
  error retry.
- Test AutoSave OFF at boot and runtime, then ON again, while confirming both
  hidden files remain untouched during OFF.
- Reconcile the 368,132-byte documented image with the 368,012-byte checked-in
  artifact using a clean baseline build.

### Topic B — Establish non-perturbing autosave observability

- Distinguish dirty production, scheduler observation, deadline state,
  operation admission, validation result, winner selection, mask import,
  parameter capture, target publication, and terminal status.
- Keep runtime diagnostics separate from the existing pre-audio boot logger.
- Ensure observation cannot print to the display, add Load/Save filesystem
  traffic, or materially alter the cadence being diagnosed.
- Define the exact evidence collected before any diagnostic implementation is
  accepted.

### Topic C — Re-run the accepted Phase 1 matrix

- Use known valid, fully drained starting records.
- Test one coordinate at a time across Bank, Scene, Kit, Instrument Normal,
  Instrument Morph, supplemental descriptor, MIDI channel/note, and generated
  Kit endpoints.
- Include identical-value writes, repeated coalesced writes, a re-dirty during
  drain, and clean-mask idle observation.
- Record exact starting/ending generation, CRC, mask bits, and payload offsets
  for every run.

### Topic D — Re-audit Phase 2 against current commit only

- Re-read actual whole-object commit boundaries for Instrument, Kit, Scene,
  Morph projections, and partial Bank operations.
- Separate staged parse, retained assignment, public success, DSP apply, and
  Save-only serialization.
- Treat one object type and one independently observable boundary per test
  pass.
- Preserve the current region-marker stubs as interface candidates, not proof
  that their planned call sites are correct.

### Topic E — Review identity, names, and completeness contracts

- Examine the retained live-Bank slot/name validation against the rule that
  slot and name are mutable payload bytes.
- Distinguish HCNAMES-owned names from parameter-owner bytes that have live
  getters.
- Define what makes a newly created record complete versus merely valid.
- Keep active Scene, Bank masks, and selective Load/Save semantics grounded in
  their actual owners rather than display identity.

## 6. Later failed Phase 2 branch — explicitly outside the accepted session

Everything in this section occurred after the August 2 cutoff and is retained
only to prevent repetition. It is not an implementation plan and none of it is
accepted production behavior.

### 6.1 Initial Phase 2 expansion

The first Phase 2 implementation attempted to publish whole-object load scopes
and treat successful Bank Load/Save as a canonical Bank-session replacement.
It cleared and fully re-dirtied the canonical mask, introduced a staged Bank
metadata commit, and marked Kit/Scene/Instrument/Morph aggregate regions at
several completion and assignment boundaries.

This interpretation was wrong in several ways:

- Bank name and slot were treated partly as record identity rather than
  ordinary payload;
- Bank Save was treated as a resident-session replacement even though Save
  reads the workspace and may include only selected Scenes;
- partial Bank behavior was made broader than its actual selected/loaded mask;
- active Scene ownership was changed while the Bank save/load semantics were
  still unsettled.

### 6.2 Narrow ownership follow-up

A follow-up removed live Bank-name/slot equality from record validity, tried to
make BankData the sole active-Scene owner, separated file-only active fallback
from live state, and narrowed whole-region publication to Load.

Although several individual observations were reasonable, the combined change
was still too invasive for the requested scope. It touched validator policy,
BankData ownership, SceneData compatibility access, filesystem Bank phases,
Preset completion, Menu active/pattern alignment, and Autosave publication in
one pass.

### 6.3 Scene Load mutation failures and repeated ownership moves

Hardware Scene Load tests did not show the expected autosave differences, and
Scene-source persistence also failed to update `settings.cfg`.

The attempted publication point moved repeatedly:

1. public load completion;
2. aggregate resident assignment;
3. parameter-boundary transfers after assignment;
4. a two-byte loaded-Scene register consumed by the drain;
5. Menu Scene selection while LEDs were toggled;
6. a boot diagnostic that armed all sixteen Scene bits.

The boot diagnostic proved the drain could consume the two-byte Scene register
and set Scene regions. A later code review found Menu reset logic overwrote the
accepted selection with the active-Scene default before autosave began. That
specific overwrite was corrected and a Scene test then appeared to work.

This did not validate the overall architecture. The signal had become tied to
Menu selection state rather than one unambiguous successful retained commit.

### 6.4 Kit, Instrument, and Bank aggregate records

The Scene-register idea was expanded using the approved small SRAM amounts:

- one byte for BankData;
- two bytes for Kit destination Scenes;
- six bytes encoding one Scene coordinate per Instrument slot.

The drain was expected to snapshot these records, expand them into the same
canonical mask, publish the expanded mask to both files, acknowledge the
compact records, and then perform ordinary parameter gets.

An Instrument hardware test showed dirty bits being set/drained while old
Instrument values were stored. The investigation then focused on overlap
between Load/Save and an already active autosave transaction.

### 6.5 Load/Save exclusion and final regression

The failed branch added an explicit suspension flag, deferred physical
Load/Save entry while an autosave transaction was active, retained the prior
page until final flush, and blocked new autosave starts while Load/Save owned
the filesystem.

The returned files then stopped advancing altogether. A later adjustment moved
deadline bookkeeping outside the suspension check so time could accrue while
Load/Save was open, but the final hardware test still produced unchanged files
and did not drain mutation bits already present in the input records.

That last fact is decisive: the failure was no longer only a whole-object load
notification problem. The common writer admission/lifecycle path was no longer
reaching the drain transaction.

No intermediate durable trace existed to distinguish disabled policy,
resident-Bank state, boot-ready/setup failure, recovery state, writer arming,
suspension release, facade readiness, pending records, or
`filesystem_start()` rejection. The timing diagnosis was therefore not proven.

### 6.6 Scene-source persistence remained unresolved

The post-load `settings.cfg` retained its old timestamp/content. Rewriting
settings was expected after successful Scene Load and was not intended to
depend on leaving Load/Save. This problem was observed but not isolated before
the rollback.

Do not describe the later branch as having implemented Scene-source persistence
for runtime loads merely because the accepted baseline contains the intended
callback hooks.

## 7. Failed approaches to avoid or use only with sufficient isolation

The following cautions are based on the failed branch. They are not proposed
fixes.

### Do not repeat

- Do not clear the canonical mask merely because Bank name or slot changes.
- Do not discard a structurally and CRC-valid record solely because its
  payload name differs from live display identity without an explicitly
  accepted format rule.
- Do not treat Bank Save as a whole resident-workspace replacement.
- Do not mark all sixteen Scenes for a partial Bank Load/Save.
- Do not create parallel active-Scene owners in BankData and SceneData.
- Do not treat Menu selection, preview, or LED state as proof that a retained
  load committed successfully.
- Do not place aggregate notifications behind a drain operation unless the
  operation's admission is independently proven and observable.
- Do not change producer ownership, scheduler admission, Menu page transitions,
  filesystem exclusion, and CRC publication in one hardware test pass.
- Do not infer a specific scheduler failure solely from unchanged output files.
- Do not use OLED diagnostics or additional filesystem writes in a way that
  perturbs the timing or storage operation being investigated.
- Do not use build success, symbol sizes, or valid input CRCs as substitutes
  for runtime reachability evidence.

### Use only with careful, staged testing

- Whole-object region markers: test one retained assignment boundary and one
  object type at a time.
- Load/Save exclusion: distinguish prevention of new starts from handling an
  operation already active before page entry.
- Compact SRAM notification records: prove producer, retention, consumer, and
  acknowledgement separately before composing them with file publication.
- Active-Scene ownership consolidation: audit every runtime reader/writer and
  Bank file fallback before changing retained ownership.
- Record validation policy changes: test corruption, stale generation,
  Bank-name edits, slot edits, and mixed-source Scenes independently.
- Runtime logging: establish its byte budget, persistence point, and timing
  effect before enabling it during autosave tests.

## 8. Failed-reference file map

These files preserve the rejected branch and must remain reference-only:

- `AUTOSAVE_PARAM_HOOK_failed.md`;
- `AUTOSAVE_PARAM_HOOK_FOLLOWUP_failed.md`;
- `Core/Bank/BankData.c.failed` and `.h.failed`;
- `Core/Bank/Scene/Autosave_failed.c` and `.h`;
- `Core/Bank/Scene/Preset/presetManager.c.failed` and `.h.failed`;
- `Core/Bank/Scene/SceneData.c.failed` and `.h.failed`;
- `Core/Hardware/SD/filesystem.c.failed` and `.h.failed`;
- `Core/Menu/menu.c.failed` and `.h.failed`.

Their comments and postmortem are useful for locating attempted ownership
boundaries and recognizing repeated failure modes. They are not candidates for
wholesale restoration.

## 9. End-of-session evidence checklist

Before beginning a future change, capture:

1. exact source commit and rebuilt image hash/size;
2. exact A/B size, generation, probe, commit, CRC, Bank identity, and dirty-bit
   count;
3. expected parameter/source offsets for the one test action;
4. whether the action is a scalar edit, staged load, retained assignment,
   public completion, save-only serialization, or UI preview;
5. whether Load/Save is open and whether a writer was already active;
6. expected settings revision/source changes;
7. minimum runtime long enough to distinguish debounce, continuation, and
   final flush;
8. returned A/B/settings files copied without interpreting host timestamps as
   firmware behavior.

That evidence is required to keep the next pass smaller than the failed branch
and to distinguish “producer did not dirty,” “scheduler did not admit,” “writer
failed,” and “commit completed with the wrong live value.”
