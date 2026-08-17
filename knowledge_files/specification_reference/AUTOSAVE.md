# AutoSave Specification

## Authority and scope

This is the authoritative reference for the implemented Helicase AutoSave
format, ownership, mutation tracking, and background writer. Historical root
plans and Session 045/046 logs explain how the implementation was reached, but
they do not override this document.

Related authority is deliberately separate:

- `FILESYSTEM_SPEC.md` owns the non-AutoSave product filesystem layout and
  `settings.cfg` schema. Its AutoSave boundary deliberately points here; the
  rejected pre-Session-045 per-file dot-backer design is not a current spec;
- `DEV_MODES.md` owns development-mode selection and diagnostic file output;
- `ASYNCFATFS_REFERENCE.md` owns low-level AsyncFATFS contracts;
- `SRAM_MANIFEST.md` owns the binding memory-reservation policy and the current
  Session 051 linked allocation/capture snapshot.

AutoSave currently persists the active resident Bank's implemented scalar
state into two hidden root records. It does not modify root `Bank/`, `Scene/`,
`Kit/`, or `Instrument/` library objects and does not replace explicit Load or
Save operations.

Implemented through the Session 048 AutoSave baseline:

- persistent `settings.cfg` AutoSave on/off preference;
- boot/runtime creation and validation of `/.hcprms1` and `/.hcprms2`;
- scalar dirty hooks for Scene, Kit, Instrument normal, and morphable
  Instrument Morph values, plus the format's implemented Bank fields;
- successful normal Kit Load, root Scene Load without Pattern, and selective
  Bank Load whole-object markers; a root Scene marker runs only after its
  complete Scene/HCNAMES filesystem transaction reports success;
- one canonical mutation mask, bounded dirty scanning and value capture, A/B
  transformed copy, CRC32C, commit-last runtime publication, retry, and
  continuation scheduling;
- an AutoSave lifecycle trace when `DEV_MODE_LOGGING` is enabled.

Not implemented and not to be inferred from the A/B writer:

- applying a winning hidden record back into resident state during boot;
- Pattern persistence in the hidden records;
- live Effect persistence (`AUTOSAVE_EFFECT_PARAM_COUNT` is zero);
- crash-recoverable promotion into explicit Bank library files;
- a second resident Bank, background staging Bank, or general object journal.

## Ownership

- `Core/Bank/Scene/Autosave.c/.h` owns the binary format, live-byte projection,
  CRC32C helpers, one canonical dirty mask, and typed dirty-marker API. It owns
  no file handle or scheduler.
- Retained owners mark their own changes: `BankData`, `SceneData`, and Preset's
  descriptor-aware Instrument path call typed marker functions only after the
  retained value changes. `on_scene_load_complete()` owns the root Scene
  whole-object marker after the terminal Scene/Pattern/Effect/HCNAMES result,
  so no partial Scene commit can be published as a successful load.
- `Core/Hardware/SD/filesystem.c` is the sole AsyncFATFS owner. It owns pair
  setup, validation, winner selection, bounded capture, transformed copying,
  publication, scheduling, and error rollback.
- `settings.cfg` and `filesystem_setAutosaveEnabled()` own policy. A trace or
  diagnostic must never change that policy or dirty state.

## On-card v1 record contract

The two singleton names are:

- `/.hcprms1`
- `/.hcprms2`

Each file is exactly 34,768 bytes:

| Region | Offset | Bytes | Meaning |
| --- | ---: | ---: | --- |
| Header | 0 | 64 | Magic, version, commit, generation, CRC32C, probe |
| Mutation mask | 64 | 3,856 | One bit for every payload byte |
| Bank payload | 3,920 | 128 | Restore slot, name, masks, active Scene |
| Scene payloads | 4,048 | 30,720 | Sixteen fixed 1,920-byte Scene regions |

The payload is 30,848 bytes, so the 3,856-byte mask covers it exactly. Mask bit
N describes payload-relative byte N. It never describes a header byte.

Each Scene region reserves:

- eight name bytes;
- 120 Scene-parameter bytes, currently 40 live;
- 512 Effect bytes, currently no live parameters;
- 1,280 Kit bytes containing two live Kit values and six fixed 192-byte
  Instrument records.

Instrument records retain a three-byte type token, eight identity bytes,
72 descriptor-indexed normal cells, 72 descriptor-indexed Morph cells, and
reserved padding. A Morph cell is live only when its descriptor is Morphable.
C structs are never copied as the wire format.

The Bank `scene_present_mask` occupies payload bytes 10..11 (absolute record
offsets 3930..3931) and is the effective resident Scene availability union.
Bank Load preserves existing resident bits and ORs in its effective
selected-child mask; an equal-value completion explicitly re-marks those two
bytes so a successful load refreshes the hidden record even when the
change-aware BankData setter is a no-op. The logging-only `B` trace stage
witnesses the resident mask at Bank Load commit and at the drain's first-byte
capture; it uses the existing eight-byte trace ring and adds no production RAM.

Header requirements:

- magic `HCPR`;
- format version 1;
- valid commit byte `0xa5`;
- wrapping 32-bit generation;
- Castagnoli CRC32C over the complete record while treating stored CRC bytes
  12..15 as zero;
- one-byte probe counter as a writer witness. Generation, not probe, selects
  the newer record. Equal valid generations deterministically select A.

Changing offsets, widths, ordering, or interpretation requires a format-version
change and an explicit migration/rejection policy. New fields must not silently
reuse reserved cells whose meaning has already shipped.

## Boot and policy lifecycle

`settings.cfg` is loaded before initial Bank selection and before hidden-file
setup. Its normalized `autosave=0|1` value is supplied to
`filesystem_setAutosaveEnabled()`.

With AutoSave off:

- no hidden-file ensure, validation, recovery, or drain may start;
- mutation tracking is disabled;
- pending canonical dirty state is discarded immediately when safe, or at the
  first safe completion boundary if a transform was already running;
- disabling must never abort an owned AsyncFATFS operation or alter CRC-covered
  bytes midway through a copy.

With AutoSave on and a resident Bank:

- boot or runtime setup ensures both hidden records exist;
- mutation tracking starts only after setup and its flush succeed;
- runtime re-enable marks the complete currently gettable resident Bank dirty
  so changes made while tracking was off are not missed;
- setup failure leaves tracking and the writer disabled until an explicit
  lifecycle transition permits another setup attempt.

No resident Bank means no AutoSave file activity.

## Dirty marking rules

There is exactly one persistent 3,856-byte canonical dirty mask. Producers OR
bits into it atomically; they do not enqueue events and do not own files.

Use only the typed API:

- `autosave_markBankFieldDirty()`;
- `autosave_markSceneParameterDirty()`;
- `autosave_markKitParameterDirty()`;
- `autosave_markInstrumentNormalParameterDirty()`;
- `autosave_markInstrumentMorphParameterDirty()`;
- future Effect marker functions only after Effect ownership exists.

Whole-object helpers mark currently gettable cells but do not copy data.
Successful root Instrument Load is the first admitted whole-object load hook:
immediately after every retained destination-slot commit it marks that slot's
three type bytes, all owned Normal endpoints, and all owned Morphable Morph
endpoints. Successful InstrumentMrp Load marks only the committed destination's
Morphable Morph endpoints. Hidden temporary `kit` restore, failed loads,
identity/HCNAMES source, and names remain excluded. The reversible InstrumentMrp
`kit` restore uses a Morph-only hidden snapshot but follows the same
non-normalizing rule: only the restored Morphable Morph endpoint cells are
marked. These are writer-side
marks only; they do not implement an AutoSave boot reader.
`autosave_markSceneWithPatternDirty()` is presently the non-Pattern alias and
must not be described as Pattern persistence. The complete-Bank helper is used
by runtime AutoSave re-enable; broad Load/copy/paste integration must be added
and tested explicitly before claiming general whole-object coverage.

The ordering rule is binding: update the retained owner first, then mark the
matching typed coordinate. Never calculate wire offsets in Menu, DSP, MIDI, or
another producer. Never mark runtime-only DSP overlays as retained state.

Successful Bank Load and Bank Save also mark the existing settings writer
dirty immediately after committing the restore slot. The debounced writer then
serializes `active_bank` from `bank_restoreBankSlot()`; the mark performs no
filesystem I/O and does not create a second settings writer.

## Background writer

New dirty work receives a five-second debounce. Repeated changes coalesce into
the same bits; they do not start one file operation per edit. Load and Save
pages suppress new background starts, and the single filesystem facade gives
foreground work priority. An already active transaction runs to its safe
close/flush boundary.

The page rule is a deferment, not a discard. After a direct foreground
filesystem callback consumes a terminal result, it must acknowledge that
`DONE`/`ERROR` result before it releases its UI owner; otherwise the facade is
not `IDLE` and neither the trace append nor this writer can acquire it. The
final read-only root Scene/Bank index callback follows this rule after it has
captured its success byte. It does not alter the page guard, writer debounce,
or mutation mask.

One transaction:

1. validates both candidates and chooses the newest valid record matching the
   current Bank identity;
2. imports the winner's on-card mutation mask once for interrupted-work
   recovery;
3. examines at most `AUTOSAVE_MASK_BITS_PER_TICK` mask positions per service
   pass;
4. atomically takes and snapshots at most
   `AUTOSAVE_PARAMETER_GETS_PER_WRITE` live values into the dedicated patch
   cache;
5. opens the winner read-only;
6. removes every case-folded physical variant of only the inactive target;
7. creates one inactive target and streams the winner through a transformed
   copy, substituting captured values and the updated mask;
8. closes and syncs the invalid copy;
9. writes and syncs the CRC;
10. writes the valid commit byte last, closes, and syncs;
11. acknowledges captured bits only after durable completion.

If work remains after success, the next complete transaction is eligible after
250 ms. Clean completion disarms the writer until a later mutation. Errors
restore every captured offset to the canonical mask and retry after the normal
five-second interval. An empty merged mask completes read-only and must not
advance generation, probe, or target contents.

The live-Bank match in step 1 is current inherited behavior, implemented by
`autosave_streamValidationMatchesBank()`, but it is not the settled future
semantic contract. Bank slot and name are also mutable payload fields; treating
a mismatch as foreign-record identity can force regeneration instead of
carrying a legitimate Bank-session transition. Resolve that validator/session
boundary explicitly before whole-object Bank Load/Save publication. Do not
silently describe the current comparison as proof that Bank metadata is
immutable identity.

## CRC scheduling: implemented bounded contract

Every CRC traversal is now governed by
`AUTOSAVE_CRC_BYTES_PER_TICK == 128`: candidate validation, transformed-copy
CRC, initial-record generation, and neither-valid recovery each retain their
cursor/accumulator between filesystem passes and consume no more than the
shared byte budget in one tick. The creation selector is retained separately
before its former scratch field is reused as the CRC accumulator, so a missing
A cannot be opened accidentally as B. This needs no record-sized buffer, blind
delay, or new permanent CRC store.

Do not add a blind one-millisecond interval or sleep between unbounded work.
That failed experiment slowed Bank operations dramatically and merely delayed
the audio glitches; it was rolled back. The controlled reimplementation order
is recorded in `SETTINGS_BANK_LOAD_REIMPLEMENT.md`.

## Power-loss behavior

During a normal A/B update, the inactive target remains invalid until payload
and CRC are durable. The valid commit byte is published last. Power loss can
therefore leave:

- the previous winner valid and the target invalid;
- both records valid, with generation selecting the newer;
- an incomplete target that fails size/header/CRC/commit validation.

Power loss while writing the runtime target's CRC does not invalidate the
previous winner. Initial creation is different: an interrupted newly-created
file can be short and invalid, and if both files were absent there may not yet
be a valid peer. AutoSave does not promise recovery if both records are
externally deleted, corrupted, or made ambiguous by duplicate directory
entries.

## Singleton and duplicate-file rules

FAT display names are case-insensitive. A failed open callback is not proof of
absence, and AsyncFATFS append/write modes include CREATE. These rules caused
repeated same-display-name failures and are mandatory for future work:

- First creation must follow a complete, successfully closed root scan proving
  zero case-folded matches.
- Existing boot setup records are never opened for write.
- Runtime replacement keeps the selected winner, removes all case-folded file
  variants of only the inactive target, waits for removal completion, and then
  creates one canonical target.
- Scan, finder, close, open, or type errors remain errors. They must not fall
  through to creation.
- Never choose one of multiple matches silently. Preserve evidence unless a
  separately specified recovery transaction authorizes removal.
- Never add another hidden record name as a workaround for a failed lookup.

The hidden A/B paths implement their own scan/create discipline. Development
log files do not yet have equivalent duplicate-safe singleton handling; that
limitation belongs to `DEV_MODES.md` and must not be mistaken for an AutoSave
format rule.

## Known `.hcprms` boot-lock evidence

A failed hardware capture produced a valid 34,768-byte `/.hcprms1` and an
exactly 32,768-byte `/.hcprms2` whose prefix looked like a valid initial
record. No diagnostic log survived. The strongest current theory is a stall or
failed callback while extending the second file beyond its first 32 KiB,
possibly in FAT cluster allocation/cache handling or the SD transport. That is
evidence, not proof, and does not justify changing the format or hiding an
AsyncFATFS failure.

The Session 047 logging build freezes a fixed 64-byte application/AsyncFATFS/
SD diagnostic capsule only on an `ASENSURE` boot deadline, before normal
recovery destroys live state. It does not change AutoSave validity, retry, or
allocation behavior; `DEV_MODES.md` owns its exact 72-byte bootlog envelope.
Any older diagnosis plan that assumes `/devlog.bin` is a current sink is stale.

## Extending AutoSave

For each new retained scalar:

1. identify the owning Bank/Scene/Kit/Instrument/Effect domain;
2. append or explicitly version its format identifier and live-count contract;
3. add the live-byte getter mapping;
4. call the typed marker from every retained setter after mutation;
5. include it in the appropriate whole-object marker;
6. keep descriptor Morph eligibility consistent between getter and marker;
7. add compile-time geometry assertions;
8. test mutation, writer error rollback, restart, AutoSave off, and power
   interruption at payload/CRC/commit boundaries appropriate to the change.

Pattern or Effect support is a feature extension, not a scalar addition. It
requires confirmed retained ownership, an explicit wire schema/version plan,
bounded snapshot/read behavior, dirty hooks, recovery semantics, and SRAM
approval for any new allocation.

Do not add a second writer, scheduler, mask, record-sized SRAM image, or
filesystem handle. Do not borrow the 9,000-byte name cache for the dedicated
4,608-byte patch cache without a separately reviewed ownership transition.

## Validation status and diagnostics

Hardware validation already accepted for the available scalar controls:

- Scene parameters;
- Kit and Instrument parameters;
- MIDI channel/note values, which are Scene parameters;
- no user-changeable Bank scalar control exists for an additional UI test.

The complete root Scene Load publication boundary is hardware-confirmed on
2026-08-16. Loading root Scene slot 024 (`SeaWaked`) into resident Scene 15
emitted `R flags=0x01 value=0x00008000`, tracking-enabled Kit and Scene `L`
witnesses (`0x3c` and `0x3d`), then the expected trace-flush/page-suppression
observations and one successful `A/V/M/C/P/T` writer transaction. The publish
record selected generation 6 in `/.hcprms2`; the HCNAMES Scene row became
`SeaWaked<TAB>024`. This confirms the existing terminal Scene marker and
writer path. Pattern and live Effect remain excluded exactly as above.

Do not reopen that completed work as a vague “coverage matrix,” “idle,” or
“repeated edit” requirement. A future code change should be tested against the
specific owner and failure boundary it changes.

With file logging enabled, AutoSave emits bounded lifecycle transitions through
the RAM-only `AutosaveTrace` producer. The trace must not alter AutoSave
policy, dirty state, scheduling, or writer results. AutoSave does not own the
development flag, destination filename, record envelope, or persistence
policy; those details are authoritative only in `DEV_MODES.md`.

Whole-Instrument marking additionally emits one bounded diagnostic outcome
record after each request. It records map eligibility, the mutation-tracking
gate, and expected versus accepted dirty-byte counts; it does not change the
mask, scheduling, retained record, or loader result. This terminal summary is
necessary because the individual dirty-byte records for one Instrument can
wrap the fixed trace ring. Its exact flags and packing are owned by
`AutosaveTrace.h` and `DEV_MODES.md`.

An `ASENSURE` boot timeout additionally freezes a logging-only diagnostic
capsule before boot recovery destroys the active filesystem state. It observes
creation only and neither changes record validity nor retries, truncates,
repairs, or accepts either hidden record. Its exact `/bootlog.bin` envelope is
owned by `DEV_MODES.md`; this specification deliberately does not duplicate
the diagnostic wire layout.

`tools/decode_devlogs.py` decodes the eight-byte boot token and conditional
72-byte `ASENSURE` capsule; it also decodes `/asavetrc.bin`. It is not an
AutoSave record inspector and it must not modify fixtures. Validate AutoSave
captures without
editing the source files: check exact record sizes, header/commit fields,
CRC32C, generation selection, dirty masks, and trace records independently. A
broader human-readable development-log converter is deferred until AutoSave
behavior and every logging format are complete.
