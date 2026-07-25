# Session 040 Handoff Log

DATE: 2026-07-18

SESSION GOAL: Establish the verified Session 040 implementation record,
repair the Bank Load failure reported as ERR BnkL14, consolidate the root
working notes, and reconcile the filesystem/API specifications with the code
that is actually present.

WORKING REPOSITORY: /Users/bc/Helicase Project/Helicase-check-fs/Helicase
on branch dev-ph3-fsfix.

WORKTREE CAUTION: The checkout contains extensive pre-existing SD_CARD fixture
deletions, an untracked freshly-saved SD_CARD/Bank/000 Full tree, and an
existing build image modification. They are user work and were not restored,
deleted, or otherwise changed by this session. Session documentation and the
Bank Load source change are separate from that fixture churn.

## End of session

COMPLETED:

- Verified the completed eight-bit resident Instrument parameter and target
  token refactor.
- Verified the sixteen-Scene Bank workspace, version-2 Bank manifest,
  selected-Scene load/save, linked PERF Scene/Pattern controls, staged apply
  behavior, and strict settings.cfg migration.
- Verified native AsyncFATFS physical-object deletion and the exact-name
  cleanup repair used by Bank/Scene replacement.
- Diagnosed and fixed ERR BnkL14 when loading a freshly saved full Bank.
- Replaced the Session 040 TBD record with AsyncFATFS-only recommended
  follow-up work in SESSION_040_AFATFS_FOLLOWUP.md.
- Appended the terse archive-index entry and reconciled current filesystem
  specification material with the checked source.

VERIFIED ON HARDWARE:

- Yes. The user confirmed that the corrected Bank Load successfully loads the
  freshly saved full Bank that previously reported ERR BnkL14.
- The user also confirmed that the former sixteen-Scene follow-up test list has
  been performed and will be repeated under future relevant code changes.
- No local firmware compilation was possible here: arm-none-eabi-gcc is not
  available on PATH. Static git diff --check passed.

CHANGES THIS SESSION:

- Core/Hardware/SD/filesystem.c: Added
  filesystem_resetSceneLoadChildDiscovery and invokes it for root Scene phase
  0, the first Bank-local Scene handoff, and every subsequent Bank-local Scene
  handoff.
- Core/Hardware/SD/filesystem.h: Documented the public Bank Load guarantee
  that every local Scene discovers its own embedded Kit, pattern, and effects
  files.
- MEMORY.md: Corrected stale one-Scene/legacy-globals notes and retained the
  Bank Load isolation invariant.
- SESSION_040_AFATFS_FOLLOWUP.md: Reshaped to AsyncFATFS-only future work.
- knowledge_files/log_archive/000_SESSION_INDEX.md: Added Session 040 entry.
- knowledge_files/log_archive/040_SESSION_HANDOFF_LOG.md: This archival
  detailed record.
- knowledge_files/specification_reference/: Updated current filesystem/API
  documentation to distinguish implemented identity/delete behavior from
  unimplemented parent-relative, move/copy, and transactional APIs.

KNOWN ISSUES RESOLVED:

- ERR BnkL14 when loading SD_CARD/Bank/000 Full was a Bank Load state-isolation
  fault, not a Bank Save serialization fault.
- Existing Bank-local Scene cleanup no longer needs to rediscover a display name
  after selection; it uses the captured physical FAT object identity.
- The old TOut06 cleanup-class failure is addressed by full LFN finding,
  recycled-handle initialization, cache release, real-error propagation, and
  one callback on native delete completion.

KNOWN ISSUES / TECHNICAL DEBT:

- Public AsyncFATFS declarations for parent-relative child APIs, move, copy,
  and tree replace are not completed by the current implementation. Do not
  build product behavior on them.
- afatfs_moveObject is a nonfunctional stub with unsafe lifecycle details. It
  must not be treated as a supported primitive.
- A physical object ID currently identifies the selected entry but is not yet a
  complete parent-bound mutation capability.
- There is no crash-recoverable journaled tree replacement. Current save
  preflight and staging measures are not a power-loss transaction.
- A local ARM build cannot be run until the cross compiler is installed or an
  approved toolchain path is supplied.

NEXT SESSION RECOMMENDED GOAL:

Keep the current Bank/Scene save/load path stable. If AsyncFATFS expansion is
resumed, first make the public API truthful and implement parent-relative
capabilities plus structured operation results; do not start Bank autosave,
cross-directory move, or transactional replace on the current stubs.

CRITICAL REMINDERS FOR NEXT SESSION:

- A false return from an AsyncFATFS start function means no operation was
  accepted and no callback will occur.
- A zero return from afatfs_fread is not EOF unless afatfs_feof is also true.
- LFN display components and short aliases are different identities. Use the
  identity appropriate to the next API and never substitute an LFN display name
  into a short-name call or an SFN alias into an LFN display lookup.
- Any caller that delegates multiple sibling directories through the shared
  Scene loader must reset the child-discovery scratch for each directory.
- Preserve the user-owned SD_CARD fixture changes; do not use broad restore,
  reset, or deletion commands to clean this worktree.

## Verified implementation detail

### 1. Compact eight-bit Instrument parameter and target representation

The resident Instrument, Scene, and Morph parameter domain is byte based.
InstrumentManager defines instrument_param_value_t and
instrument_target_token_t as uint8_t. The explicit off token is 0xff.
Target-selection state is no longer stored as a packed 16-bit parameter ID.

The target namespace is intentionally compact and role specific:

- Local Instrument parameters are indices from zero through the local target
  count minus one.
- Voice values are user-facing one through INSTRUMENT_SLOT_COUNT.
- The Scene-wide voice namespace is INSTRUMENT_SLOT_COUNT plus one.
- LFO voice selection accepts self, the voice values, and the Scene namespace;
  the latter is rendered as scn, not as another voice.
- Velocity selection is self-scoped. It offers targets belonging to the source
  Instrument plus the source-voice Morph token 0x40 where applicable. It does
  not browse arbitrary Scene or other-voice parameters.

SceneData uses byte arrays for velocity amount/target, LFO amount/target, and
the related resident values. presetMorphEngine interpolates
instrument_param_value_t values. InstrumentManager, presetManager, menu, and
SceneModTargets keep wide parameter IDs only at the naming/resolution boundary;
the stored editable value remains a byte token. Menu display expands a token
only while rendering.

Storage reads/writes compact values. Target off is written as 255. LFO target
voice parsing accepts self or the supported one-through-Scene range and clamps
to that range; other compact rows parse as unsigned bytes. The checked SD-card
fixtures were migrated. Pattern automation IDs deliberately remain dynamic
Pattern references and were not swept into this resident refactor.

Legacy packed-16-bit-file conversion is intentionally not a product goal. The
current compact format is authoritative; do not silently interpret an old
packed value as a new compact token because that could select the wrong target.

### 2. Sixteen-Scene Bank workspace and persistence

SCENE_COUNT is 16. BankData owns the active Scene, a sixteen-bit
scene_mask_voice_edit/present selection, and the restore Bank slot. The active
Scene remains constrained to a present Scene. The standard restore convention
can use the complete 0xffff mask.

Bank folders use the root shape:

    Bank/NNN Name/
      bankset.bcg
      00 Scene Name/
      ...
      15 Scene Name/

The local child folder identity is a two-digit 00 through 15 slot. It is not
the root numbered Bank/Scene parser. bankset.bcg version 2 contains:

    format=helicase.bankset
    version=2
    active_scene=<0..15>
    scene_mask_voice_edit=0xNNNN

Bank Save writes the manifest plus one local Scene payload for each selected
bit. Bank Load validates the manifest, applies the active Scene and mask, and
iterates selected local children. Empty valid Banks remain successful Bank
loads; the caller can inspect whether a child Scene was loaded before using
the normal fallback chain.

Boot restores the persisted Bank slot with the full selected Scene mask. This
is no longer a one-resident-Scene bridge.

### 3. Scene controls, runtime apply, and settings ownership

PERF SEQ selection changes the linked Scene and Pattern together. The selection
uses seq_selectActivePattern, realigns the master Pattern state, releases notes
as required, and starts the staged preset/drumset application. The historical
seq_setNextPattern entry is a compatibility no-op so it cannot restore
Pattern-only switching.

Holding VOICE while using a Scene selector toggles that Scene in the edit mask
and consumes the button event. LED and button-overlay state use the active and
present/edit masks consistently.

Scene application is audio safe by design. Scene-wide mirrors and required
decimation state update promptly; individual Instrument slots commit when their
amplifier is quiet or at a safe trigger boundary. InstrumentManager retains
the runtime type shadow used by that decision, and Morph work is prioritized so
that it does not conflict with a staged Scene commit.

Root settings.cfg is keyed text with strict format/version validation and an
allowlist of global settings: bpm, external synchronization, quantisation,
global MIDI channel, MIDI TX/RX filters, MIDI routing, screensaver, bar reset
mode, input/output prescalers, follow mode, oscillator interpolation, and
active_bank. Scene-owned Morph, per-voice Morph, and decimation values do not
belong to global restore. The current code does not fall back to legacy glo.cfg.

### 4. AsyncFATFS current production primitives

AsyncFATFS is a single-context foreground-pumped FAT32/VFAT component layer.
Product code does not write FAT records directly. filesystem.c owns product
directory topology and storageTypes owns schemas.

Object scans return afatfsObjectInfo containing afatfsObjectId. The ID holds
object kind, display name, printable short alias, LFN fragment-run identity,
SFN entry identity, first cluster, logical size, and attributes. A selected
object can therefore be passed directly to destructive code without a second
display-name search.

afatfs_deleteTree is a native non-blocking operation for one selected directory
identity. It validates the input, allocates and initializes a private file
handle, copies the root identity, scans/deletes child LFN/SFN runs and cluster
chains, frees the root, releases retained cache state, initializes the handle
back to idle, then invokes the result callback once. Returning false means no
private handle accepted the operation and no callback is expected.

afatfs_getDeleteTreePhase reports native delete progress for timeout/error
diagnostics. afatfs_removeObject supports exact short-alias removal; the
legacy empty-directory cleanup path uses that exact alias where available
instead of resolving an ambiguous display component.

The repair addresses the historical TOut06 path: never cast a small raw finder
as a full LFN object finder; initialize every recycled handle; release cache
ownership before reusing/finishing it; and do not convert a genuine finder or
I/O error into normal end-of-directory completion.

### 5. Bank Save exact-object repair

The duplicate-folder/freeze trace was a cleanup defect. A pre-existing
Bank-local Scene could be missed because a root parser was used for the
two-digit local folder. The old recursive display-name route could then choose
a wrong LFN sibling, leave a stale folder, collide during save, or wait for a
callback that was never promised. Corrupt _bad folders were evidence of prior
interrupted/deletion damage, not a reason to bypass exact selection.

The implemented path is:

1. Enter the correct parent and scan immediate children.
2. Parse a Bank-local folder with storage_parseBankSceneFolder.
3. Capture its afatfsObjectId while it is selected.
4. Give that identity to afatfs_deleteTree for recursive cleanup.
5. Use afatfs_removeObject with the exact short alias for the legacy
   empty-directory fallback where needed.
6. Reopen the Bank target directory with afatfs_opendir and its stored
   short/open name, rather than opendir_lfn with a display label.

This prevents duplicate or stale display names from redirecting replacement
cleanup. An actual filesystem failure remains a bounded error rather than
normal completion or a frozen UI.

The user has completed the Bank/Scene regression cases previously listed for
the sixteen-Scene expansion and will continue to rerun them after relevant
future changes. Retain that practice for full/sparse masks, active-Scene
containment, linked Scene/Pattern selection, settings validation, promotion
collision states, duplicate LFNs, rejected async starts, and SD I/O failure.

### 6. ERR BnkL14 root cause and repair

The freshly saved SD_CARD/Bank/000 Full tree was structurally valid:

    format=helicase.bankset
    version=2
    active_scene=6
    scene_mask_voice_edit=0x0040

It contained local Scene payloads 00 through 15, each with sceneset.scg, an
embedded Kit directory, pattern.pat, and effects.fx. The error did not indicate
a malformed manifest or a Bank Save data fault.

Bank Load delegates each local child to filesystem_loadSceneDirectory_tick.
That shared Scene loader records the discovered embedded Kit directory,
pattern, and effects names in these operation-scratch buffers:

    op_scene_child_open_name
    op_scene_child_display_name
    op_scene_pattern_open_name
    op_scene_effect_open_name

Before the repair, root Scene loading cleared them at phase 0, but Bank Load
did not clear them between delegated children. For example, child 00 Slak
recorded Kit Brezel. When Bank Load entered child 01 Slak, which contained Kit
Forest, non-empty scratch made Scene discovery look complete. The loader tried
to open Kit Brezel in the child 01 directory and failed.

filesystem_resetSceneLoadChildDiscovery now clears all four scratch buffers.
Root Scene phase 0 uses this common helper. Bank Load calls it before the first
selected child and every later selected child. It resets only transient
per-directory discovery names; it does not discard the Bank manifest, child
iterator, active Scene, mask, or loaded Scene data.

BnkL14 is the Bank wrapper's decimal phase 20 displayed in hexadecimal. It is
not a direct Scene-loader phase 14 diagnosis. Detailed comments in both
filesystem.c and filesystem.h record this state-lifetime rule. The user
confirmed the repaired full-Bank load on hardware.

## AsyncFATFS follow-up boundary

The full recommended list is preserved in SESSION_040_AFATFS_FOLLOWUP.md.
Its implementation order is intentional:

1. Make the public API truthful: do not expose a replace/create or
   move/copy/replace declaration as usable before it is a complete operation.
2. Finish a parent-bound object capability and parent-relative lookup/create
   APIs with explicit collision policy and copied input lifetime.
3. Harden native delete with parent relation validation, structural dot/dot-dot
   checks, corrupt-chain/cycle guard, and partial-progress reporting.
4. Implement same-parent rename and cross-parent move as distinct operations.
5. Implement bounded non-blocking tree copy.
6. Implement durable staging/backup replacement and boot recovery with explicit
   flush/sync boundaries.
7. Only then migrate broader Bank/Scene/Kit/settings paths to the new
   transaction primitives.

Do not regard the current move/copy/tree-replace declarations as completed
features. Current safe production behavior is narrower: component LFN access,
object iteration, exact selected-object deletion, namespace-aware product
matching, and preflight around the existing save flow.

## Source records preserved by this handoff

The following root documents informed this record and may be removed after this
archive is accepted:

- 8_BIT_PARAM_REFACTOR.md: byte parameter/token contract, selector namespaces,
  parser/writer migration, fixture migration, and Pattern automation non-goal.
- AFATFS_EXPANSION_PLAN.md: foreground lifecycle invariants, parent-relative
  API design, delete hardening, move/copy/replace architecture, and test plan.
- AFATFS_ADDITIONS_SUMMARY.md: native delete, dispatcher context, phase
  diagnostic, exact removal, and TOut06 repair history.
- ASYNCFATFS_MISSING_OPS.md: original duplicate-LFN, global-current-directory,
  create-or-open, diagnostics, and transaction gap postmortem.
- BANK_16_SCENE_EXPANSION.md: 16-Scene persistence/control/settings/apply
  implementation and retest repair rationale.
- BANK_SAVE_FAIL_TRACE.md: duplicate local folder, LFN recursion ambiguity,
  callback-freeze, and _bad corruption trace.
- BANK_LOAD_FIX.md: BnkL14 symptom, source state leak, exact helper calls,
  header contract, hardware verification, and regression invariant.
- SESSION_040_COMPLETE.md: source-audited completed-work record.

The root follow-up was intentionally renamed to
SESSION_040_AFATFS_FOLLOWUP.md and narrowed to AsyncFATFS. Bank-specific
hardware regressions were completed by the user and are retained above as a
future-change testing discipline, not an open follow-up point.
