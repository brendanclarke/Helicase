# Session 061 Handoff — Typed HCNAMES and AutoSave Boot Restore

Date: 2026-09-05 through 2026-09-08  
Branch at closeout: `dev-ph3-autosave-ph6`  
Firmware HEAD reviewed: `6642f4c` (`new fix in, test pending`)  
Final hardware-tested image SHA-256:
`5732e821d256f521e48814d2cf255c895b1fbb7fdfa9f006b43f5ae293fb8c62`

This log preserves the durable result of the five Session 061 working
documents. Those documents can be deleted after this closeout without losing
the implementation contract, investigation evidence, rejected approaches, or
remaining test obligations.

## 1. Executive summary

Session 061 completed the first usable AutoSave boot restore and the typed
HCNAMES schema required to reload individual Instrument rows safely. It then
used repeated real-card power-cycle captures to repair four defects exposed by
the new reader:

1. HCPR intentionally contains no Pattern data, so restoring only its scalar
   Scene payloads produced silent Scenes. The reader now best-effort loads each
   accepted Scene's `pattern.pat` from the resolved Scene source.
2. An Instrument-row validity guard rejected a numeric source inherited from a
   Bank/Scene/Kit ancestor. It now rejects numeric sources only when the
   Instrument row itself supplied the numeric token.
3. Successful Scene and Bank operations could publish incomplete HCNAMES child
   identity, leaving stale Kit/Instrument names beside refreshed sources. Load
   and Save publication now preserves or stages the full committed hierarchy.
4. the reader parsed 96 Instrument types into a destructive stage union. An
   intermediate six-byte per-Scene snapshot protected Scene 0 only. The final
   fix retains all 96 types for the entire traversal in a mutually exclusive
   view of the existing 144-byte Bank-child cache, with no BSS growth.

The final HCNAMES-authoritative failure was hardware-accepted with
`SD_CARD_READER_9`: Bank 001 and all sixteen Scenes were restored, including
root Scene 008 `Rollin` overlays in resident slots 0, 1, 14, and 15. The trace
contains exactly 128 successful Case-2 row records plus its summary, no Case 3,
no operation error, and no phase stall. Both resulting HCPR records are valid,
and every HCNAMES refreshed witness agrees with the corresponding dirty object
mask.

The reader is usable and the original failure is closed. One deliberately
bounded reader regression remains: reboot the Reader 9 state once to exercise
the matching-winner mixed Case-1/Case-2 path. It does not block the next goal,
which is Pattern data storage. The wider Load/Save cleanup and edge-case matrix
now lives in `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` and is intentionally
deferred until after Pattern work unless it exposes a blocking fault.

## 2. Chronological work and decisions

### 2.1 Typed HCNAMES Instrument rows

The boot reader cannot select a typed root Instrument file from name and source
alone. HCNAMES therefore gained an explicit schema/version witness and an
Instrument type column.

The physical file is now exactly one header plus 129 data rows:

```text
#types\tdrm\tsnr\tcym\that
```

- Rows 0..32 (Bank, sixteen Scenes, sixteen Kits):
  `name<TAB>source[<TAB>R]`.
- Rows 33..128 (six Instruments for each Scene):
  `name<TAB>source<TAB>type[<TAB>R]`.
- `type` is mandatory and must be exactly `drm`, `snr`, `cym`, or `hat`.
- `R` is the optional refreshed witness introduced in Session 060.
- The header token count, spelling, and order are mandatory. A missing or
  mismatched header, invalid Instrument type, wrong row count, or malformed row
  invalidates the register. A genuine read/I/O failure remains an error and is
  not silently treated as invalid content.

All HCNAMES emitters write the header. The `.hcnamtmp` boot prelude validates
the same schema before promotion. Runtime formatting obtains the type from the
resident SceneData slot; boot parsing must retain the types independently of
payload staging until all sixteen Scenes are evaluated.

`tools/verify_bank_autosave.py` was updated to parse the header/type fields and
cross-check Instrument types against the applicable kitset. The
`SD_CARD_HCNAMES_INST` pass confirmed a valid header, 129 rows, correct type
columns, no leftover temp, and expected removal of `R` after the drain. A
verifier failure against a static Bank tree after root Scene overlays is
expected: that tool's Bank comparison is not an oracle for a deliberately
mixed-source resident state.

### 2.2 `Err BKKit14` was fixture data, not a loader defect

`BKKit14` decodes as Bank loader case `0x14` (decimal 20): failure while loading
an embedded Kit child. Inspection disproved the initial “sparse Banks fail”
correlation. The affected kitsets contained `file=` stems longer than the
canonical eight-character display-name contract. Root Kit workflows happened
to admit data that the stricter Bank-embedded path rejected; that did not make
the embedded files valid.

The fix was to repair the SD corpus, not weaken or add silent truncation to the
firmware. Ten composite Kits were normalized across every occurrence in root
Kit, root Scene, and Bank-embedded trees. In total, 88 kitset files had
references updated and 449 Instrument files were renamed with the deterministic
form `<base truncated to seven><slot digit>.<typed extension>`. Post-fix checks
found zero over-eight-character `file=` stems and zero missing references, and
the transformation was idempotent.

Carry this rule forward: every Bank-embedded kit member stem is at most eight
characters and must match the referenced physical file exactly. A future
canonicalization UX may reject or guide invalid root-pool names, but must not
silently truncate two names to the same target.

### 2.3 AutoSave matching-winner boot reader

The record format and writer were deliberately left unchanged. Boot now uses
the following order after settings and root indexes are available:

1. With AutoSave ON, stage 10b validates both `/.hcprms1` and `/.hcprms2` by
   exact size, header/version/commit, CRC32C, generation, and Bank identity.
2. If the selected valid winner matches `settings.cfg`'s active Bank,
   `filesystem_autosaveBootReaderBlocking()` attempts the restore.
3. If that path is unavailable or declines, the narrowly gated
   HCNAMES-authoritative path described below is tried.
4. Otherwise boot uses the existing canonical Bank or Scene/Kit fallback
   ladder. When AutoSave is ON, canonical Bank fallback is recorded for
   deferred whole-Bank dirty replay after tracking is enabled.

The matching-winner reader opens `.hcnamtmp` first and adopts it only when the
entire typed schema validates; otherwise it reads `.hcnames`. If neither is
usable but a valid HCPR winner exists, it can regenerate HCNAMES atomically
from the winner's identities, Phase-C sources, and type text, marking every row
refreshed. It then applies the Bank payload and evaluates the eight rows of
each present Scene in fixed order: Scene, Kit, and six Instruments.

Per row:

- Case 1, no `R`: the HCPR object is proven caught up. Apply its scalar payload
  through the `autosave_apply*Payload()` API. Cross-check the embedded source;
  a mismatch is traced and the payload source wins as defense-in-depth.
- Case 2, `R` plus a resolvable source: load exactly that level from its
  explicit/inherited library source. Scene loads only Scene settings; Kit loads
  the kitset plus all six members; Instrument loads only that typed member.
  These narrow loaders do not cascade unrelated child source changes.
- Case 3, `R` plus an unknown/unresolvable source, narrow-load failure, or
  unrecognized HCPR Instrument type: empty the entire Scene, stop evaluating
  its later rows, set all eight sources to `?|R`, and queue the Scene for a
  post-boot notice. This all-or-nothing P1 rule prevents a partially rebuilt
  Scene from becoming the input to a later Save.

Source resolution follows Instrument -> Kit -> Scene -> Bank. A numeric direct
source written on an Instrument row is invalid because no writer creates that
form. A numeric source inherited from the Kit, Scene, or Bank row is valid. The
guard must test `resolved_row == row`, not merely that the resolved value is
numeric.

The boot-only inverse projection in `Autosave.c/.h` is:

- `autosave_applyBankPayload()`;
- `autosave_applyScenePayload()`;
- `autosave_applyKitPayload()`;
- `autosave_applyInstrumentPayload()` (returns false on unknown type text);
- `autosave_extractPayloadSource()`.

Those functions run while mutation tracking is off. They project validated
wire bytes into retained BankData/SceneData and do not perform filesystem I/O.

### 2.4 Pattern and Effect behavior at boot

HCPR v1 still contains no Pattern bitmap data. Pattern state is therefore
cleared by normal retained initialization and then populated, best effort, by
`filesystem_bootReaderLoadPattern(scene)` after each present Scene has passed
row evaluation. It resolves the Scene source and parses that source's
`pattern.pat`. Missing or malformed Pattern data leaves that Scene's PatternSet
zeroed but does not invoke Case 3 or empty otherwise valid scalar state.

Effects are not restored from HCPR because `AUTOSAVE_EFFECT_PARAM_COUNT` is
still zero. The existing effect region is reserved, not live. Adding Pattern
or Effect persistence is a future format/ownership project and must not be
represented as already covered by the scalar reader.

### 2.5 Deferred dirty replay and post-boot notices

Boot loads occur before AutoSave mutation tracking is enabled, so normal
markers would be no-ops. Filesystem retains a logical five-byte boot latch:

- one canonical-Bank-fallback flag;
- a sixteen-bit Case-2 Scene mask;
- a sixteen-bit Case-3 Scene mask.

Immediately after `filesystem_ensureAutosaveFilesBlocking()` enables tracking,
the latch replays `autosave_markResidentBankDirty()` for canonical/authoritative
Bank fallback and `autosave_markSceneWithoutPatternDirty()` for the union of
Case-2 and Case-3 Scenes. Case-2 bits are then cleared. Bank and Case-3 bits
remain until Menu consumes them for one-shot notices.

Menu owns six bytes of logical notice state and displays sequential,
non-blocking approximately two-second overlays after audio starts: first
`AutoSave bank load`, then one empty-Scene notice per Case-3 bit. Accessors are
read-and-clear. Notice work must never add boot filesystem steps or delay audio
startup.

### 2.6 HCNAMES-authoritative special path

The menu page guard can keep the background HCPR writer suppressed while a
user loads a Bank, then loads or saves additional children, then removes power
without leaving Load/Save. In that state the HCPR winner may still describe an
older Bank, while HCNAMES contains the latest committed source for every row.

`filesystem_bootHcnamesAuthoritativeLoad()` is allowed only when both of these
checks hold:

1. HCNAMES row 0 is a direct numeric Bank source exactly equal to the active
   Bank selected by `settings.cfg`.
2. All 129 HCNAMES rows carry `R`.

This path never consults or regenerates from HCPR. It narrow-loads the Bank
container from the Bank tree, then resolves and loads every row for each
present Scene under the same all-or-nothing Scene rule, then loads Patterns
best effort. It marks the entire constructed Bank for post-enable replay. If
either authority check fails, the Bank is empty, a hard reader condition
occurs, or the boot deadline expires, it declines to the unchanged canonical
fallback rather than inventing authority.

### 2.7 Complete committed-hierarchy invariant

The first loaded-Scene failure was partly a writer-side truth problem: a root
Scene Load could update the Scene name while leaving stale resident Kit and
Instrument names beside new sources and `R` flags. The binding invariant is
now:

> Every successful Load or Save that commits an object hierarchy must stage
> and publish the HCNAMES name, source, and refreshed witness for every
> Scene/Kit/Instrument object it actually committed.

The implementation added `filesystem_cacheCurrentResidentSceneChildNames()`
and uses it in Scene HCNAMES update paths so the Scene row plus Kit and six
Instrument identities reflect resident state. Scene Save seeds its identity
store from the source Scene register rows before writing. Bank Save child
preparation also stages the Kit identity. The pre-existing terminal Scene Load
completion already sets `R` after Pattern/Effect success, so no duplicate early
mark was added. Effect and Pattern have no HCNAMES rows; if they gain rows in a
future schema they must enter this invariant.

The rejected alternative was to make the reader scan around stale HCNAMES
names. That would hide an invalid durable register and reproduce ambiguity.
Writers must publish truth; readers may recover only under explicit rules.
Already-persisted sticky `? R` rows are not guessed or self-healed: reload the
affected Scene explicitly once.

### 2.8 Root-CWD readiness rule

`afatfs_chdir(NULL)` previously reinitialized `afatfs.currentDirectory`, queued
its root seek, and immediately reported success. The blocking reader treated
success as “root is idle” and could race the next relative open. The root case
now polls until that seek completes before returning true. This is a low-level
contract change: successful root reset means currentDirectory is at root and
not busy. Non-root `chdir(handle)` retains its immediate busy checks.

### 2.9 Final 96-type lifetime fix

The first implementation stored all parsed types in
`fs_stage_workspace.boot_reader_type[96]`. That 2,048-byte union is also the
Scene/Kit/Instrument payload stage. The first Case-2 load therefore destroyed
types needed later. The attempted six-type local snapshot ran at the start of
each Scene, so Scene 0 succeeded but Scene 1 read bytes already overwritten by
Scene 0.

The final implementation defines one 144-byte operation union:

```c
typedef union {
    char bank_child_display[16][9];
    uint8_t boot_reader_type[16 * 6];
} filesystem_bank_child_scratch_t;
```

Both readers parse or seed all 96 types before any narrow load and use that
view through the last Scene. Canonical asynchronous Bank Load uses the 16x9
display-name view only after a reader has returned and `filesystem_start()`
has cleared the union. These lifetimes cannot overlap. The 9,000-byte shared
list cache was considered and rejected because stage-11 canonical fallback
still needs the Bank index before a new filesystem request disposes it. Adding
a standalone 96-byte static was also unnecessary.

Static assertions bind the 96-entry count, 144-byte union size, and enclosing
scratch budget. Linked BSS did not grow. Removing the two local six-byte arrays
also reduced their aligned stack frames by eight bytes each in the reviewed
LTO output.

## 3. Hardware evidence

### 3.1 Typed HCNAMES

The typed-HCNAMES card pass confirmed the required header and 129 data rows,
correct Instrument type columns, no leftover `.hcnamtmp`, and normal refreshed
witness drainage. The sequence included loading Bank 002 `LoadTst`, resaving
it as `LoadTst2` in slot 14, and loading root Scene 009 `Forest` into multiple
resident slots. Differences from the unchanged Bank library tree were expected
resident overlays, not register corruption.

### 3.2 Reader fixture lineage

The decisive input `SD_CARD_READER_4` was created by:

1. loading Bank 001 `Full`;
2. without leaving Load/Save, loading root Scene 008 `Rollin` into resident
   Scenes 0, 1, 14, and 15;
3. powering off while the page guard still prevented HCPR publication.

It contains `active_bank=1`, AutoSave ON, valid fully-refreshed typed HCNAMES,
and a newer HCPR winner still describing the older Bank 002 session. The Bank
001 and Scene 008 library objects are valid. This is the exact gate for the
HCNAMES-authoritative path.

Intermediate captures proved the defects rather than bad media:

- omitting Pattern loads gave correct parameters but silent Scenes;
- the incorrect numeric Instrument guard emptied Bank-inherited Scenes;
- incomplete Scene-child identity publication produced stale names and sticky
  invalidation;
- after the six-type attempted fix, Scene 0 loaded all six Instruments but
  Scene 1 failed its first Instrument from the same physical Rollin tree,
  proving cross-Scene type-storage corruption.

### 3.3 Reader 9 acceptance

`SD_CARD_READER_9` is the post-boot capture from a fresh Reader 4 copy running
the final reviewed image. Library trees and `settings.cfg` are byte-identical
to the input, and no `.hcnamtmp` remains.

The Q trace is decisive:

- record `#015346` validates the old generation-8 winner;
- records `#015347..#015474` are exactly 128 Case-2 successes (`flags=0x04`),
  eight rows for each of sixteen Scenes;
- Rollin Scenes 0, 1, 14, and 15 resolve through source 8; all other Scenes
  resolve through Bank source 1;
- summary `#015475` is raw `0x0000ffff`: Case-2 mask `0xffff`, Case-3 mask
  `0x0000`;
- no `E` operation-error or `X` phase-stall appears.

HCNAMES has the correct header/row count, no `?` sources, and preserves every
identity/source/type. Only expected `R` removals occurred as objects became
fully captured. The register had 46 clean rows and 83 still carrying `R` at
capture time. Generation-10 HCPR's dirty mask and all 129 HCNAMES witnesses
have zero disagreements.

The resulting records are:

| Record | Generation | Probe | CRC32C | Dirty bits | Bank | Present | Active / VOICE |
|---|---:|---:|---:|---:|---|---:|---|
| `.hcprms1` | 9 | 8 | `0x2449f3fb` | 6,671 | `001 Full` | `0xffff` | Scene 6 / `0x0040` |
| `.hcprms2` | 10 | 9 | `0x12caa3b9` | 5,138 | `001 Full` | `0xffff` | Scene 0 / `0x0001` |

Generation 10 is the unambiguous winner. The trace file ends at generation 9
only because low-priority trace append had not caught the later transaction;
generation 10's commit byte and CRC prove its publication completed. Stale
embedded name bytes are expected by format: HCNAMES, not HCPR names, owns live
identity.

Fixture hashes:

```text
.hcnames   7d54c7f4a4a1cdf6d362ad4cfb019fc298007186a18f3ec7dea38c1ecd23ed02
.hcprms1   5d641b94252acb184d4965d5b087336c69dd21e3ac492ff799ecda432c8b4ea2
.hcprms2   cc83be68bf8024d64833ef0b917ac92db2a713b3645cf934ebf46b0a6ea14639
asavetrc   3d45f11ac6ad9ae7be2784ae8c4a1cb4880bebb64dec989ba458c2b939fb0696
```

Do not use `tools/verify_bank_autosave.py SD_CARD_READER_9 1` as the sole
pass/fail oracle. Its intended Bank-tree comparison reports the four deliberate
Rollin overlays as mismatches. For this fixture, validate the typed HCNAMES,
effective source hierarchy, HCPR CRC/generation, and per-object `R`/dirty-mask
agreement.

## 4. Diagnostics and interpretation

The new `AUTOSAVE_TRACE_STAGE_BOOT_READER` stage is ASCII `Q`:

- flags `0x01`: Case-1 embedded-source mismatch;
- flags `0x02`: Case-3 Scene emptied;
- flags `0x04`: Case-2 single-level load completed;
- flags `0x80`: end-of-reader summary.

For row records, `value32` packs Scene in bits 0..3, HCNAMES row in bits 8..15,
and the embedded/resolved source in bits 16..31 where applicable. Summary
records pack Case-2 Scene mask in bits 0..15 and Case-3 mask in bits 16..31.
Therefore an all-Case-2/no-Case-3 pass is raw `0x0000ffff`, not
`0xffff0000`.

Both readers flush their boot Q batch before runtime dirty-mark traffic can
wrap the retained trace ring. Absence of a later writer trace record does not
invalidate a separately committed, CRC-valid HCPR transaction.

## 5. API and module ownership changes

### `Core/Bank/Scene/Autosave.c/.h`

- Added the payload-to-resident functions listed in section 2.3.
- Preserved record v1 geometry, CRC, dirty mask, and writer behavior.
- Patterns and Effects remain excluded from HCPR live data.

### `Core/Hardware/SD/filesystem.c/.h`

- Added blocking boot winner validation and query.
- Added HCNAMES regeneration from a valid winner.
- Added the matching-winner and HCNAMES-authoritative readers.
- Added narrow boot loaders and best-effort Pattern loading.
- Added the deferred dirty/notice latch and public read-and-clear notice API.
- Extended every HCNAMES parser/writer to the typed schema.
- Added complete Scene-child identity staging and the 144-byte scratch union.
- Preserved the single filesystem facade and boot-only blocking execution.

### `main.c`

- Inserted stage-10b AutoSave candidate validation and the stage-11 restore
  decision before canonical Bank Load.
- Canonical Bank fallback now latches its post-enable dirty replay when
  AutoSave is ON.

### `Core/Menu/menu.c`

- Added the post-audio AutoSave boot-notice sequencer and consumes the
  filesystem's one-shot notice masks.

### `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

- Strengthened successful `afatfs_chdir(NULL)` to return only after the root
  seek leaves `currentDirectory` idle.

### Diagnostics and tools

- `AutosaveTrace.h` and `tools/decode_devlogs.py` gained Q-stage semantics.
- `tools/verify_bank_autosave.py` gained typed-HCNAMES parsing/type checks.

### Splash/UI work also present in the session range

The session commits also changed the static boot branding and introduced a
compiled `SplashAnimation.c/.h` CGRAM animation. The call to
`splashAnimation_play()` remains commented out, so the animation is not live;
the static splash draws six custom glyphs plus `voskomm` / `helicase 0.00`.
LCD CGRAM slots 2..7 now contain those splash glyphs at initialization rather
than the prior check/heart/bell set, and the init delay is 20 ms. Treat the
animation's header comment claiming it restores the old glyphs cautiously:
the restore calls in its implementation are currently commented. This is
separate from AutoSave and should be reviewed when the animation is enabled.

## 6. RAM, flash, and build closeout

Clean incremental verification at final HEAD:

```text
make -j2                         PASS
make img                         PASS
image SHA-256                    5732e821d256f521e48814d2cf255c895b1fbb7fdfa9f006b43f5ae293fb8c62
arm-none-eabi-size:
  text                           407,060
  data                           404
  bss                            96,212
```

Section split:

```text
.isr_vector       456
.text          394,128
.itcm            3,768
.dma_nocache     3,100
.data              404
.bss            89,540
.dtcm            8,708
.dtcmz           3,572
```

Relevant linked symbols under LTO:

```text
fs_boot_latch                 6 B linked (5 B logical fields + padding)
fs_boot_winner               12 B linked (7 B semantic fields + padding)
menu boot-notice state        6 B total
op_bank_child_scratch       144 B (unchanged allocation; includes 96-type view)
autosave_dirty_mask        3,856 B
fs_autosave_parameter_cache 4,608 B
autosave_trace_records    16,384 B with DEV_MODE_LOGGING
```

The latch/winner/notice fields total 18 semantic bytes and link as 24 bytes of
named objects; the measured pre-reader-to-reader BSS shift was 28 bytes after
whole-layout alignment. The original plan's “8-byte winner / 19-byte total”
was therefore an estimate, not the linked allocation. The final type-lifetime
change itself produced `text -40`, `data 0`, `bss 0`
against its clean pre-change baseline. It did not borrow the 9,000-byte list
cache and did not add the initially considered 96-byte static.

## 7. Load/Save revision backlog created at closeout

`AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` is the sole forward list for the
cross-system edge cases removed from `SCOPING_TARGETS.md`. Its highest-value
items are:

- checkpoint pending Kit/Instrument HCNAMES when switching browser item/type,
  because that transition already disposes or reassigns the `.hcindex` cache;
- eliminate the visible Kit/Instrument Load-menu exit hang by decoupling page
  paint/ownership from background HCNAMES persistence;
- show a blank name while a list is not ready, reserving `Empty` for an index
  that has actually proved the slot absent;
- standardize Bank/Scene/Kit/Instrument scrolling as latest-selection-wins:
  publish slot immediately, then name, then optional data/LED preview, then
  post-cache work; stale asynchronous callbacks dispose their results;
- exercise AutoSave/HCPR, HCNAMES/temp promotion, and settings/temp promotion
  combinations, failed operations, page suppression, active Bank changes,
  direct and inherited sources, copy/paste, Morph-only paths, and power cuts.

Initial inspection suggests occupied Bank scrolling is slower mainly because
it starts a child-Scene preview scan and gates on `menu_storageBusy`; SEQ LED
painting alone is not established as the cause. Kit/Instrument durable name
publication is deferred through the resident-name scratch session and the page
switch waits on storage ownership. These are scoping observations, not final
root causes.

## 8. Known issues, risks, and non-goals

- Reboot Reader 9 once and capture the result to cover the matching-winner
  mixed Case-1/Case-2 path. Require no Case 3, correct Rollin overlays, correct
  Patterns, and continued HCNAMES-R/dirty-mask agreement.
- Pattern is currently loaded from explicit Scene files during boot but is not
  stored in HCPR. Pattern persistence is the next feature and requires an
  explicit wire/version and bounded snapshot plan.
- Live Effect persistence remains absent (`AUTOSAVE_EFFECT_PARAM_COUNT == 0`).
- Best-effort Pattern failure does not invalidate a Scene; document and test
  this distinction when Pattern persistence changes.
- HCNAMES-authoritative restore is intentionally narrow. Do not relax either
  the row-0 Bank match or all-129-rows-refreshed gate without a new proof.
- Do not change the all-or-nothing Scene rule. A failed child must never leave a
  partially constructed resident Scene available to Save.
- Do not “repair” stale HCPR embedded names by adding name getters/dirty bits.
  HCNAMES owns live identity; HCPR name fields are baseline/debug identity.
- Do not borrow the disposable `.hcindex` list cache for reader state needed by
  canonical fallback. Preserve the 144-byte alias lifetime or make a separately
  reviewed allocation.
- A root Kit workflow can encounter a filename that an embedded Bank Kit later
  rejects. The later Load/Save pass should validate/guidance-test this boundary
  rather than adding silent truncation.
- Previously persisted `? R` rows from an old failed reader need explicit user
  reload; current boot does not guess their original sources.
- The live splash animation is disabled, and its CGRAM restoration comments do
  not match the currently commented restore calls.

## 9. Repository state at closeout

Session firmware changes are committed through `6642f4c`. At documentation
closeout the working tree intentionally also contains:

- modified `S061_HCNAMES_INVALID_FIX_2.md` with the Reader 9 assessment;
- modified `SCOPING_TARGETS.md` after moving open Load/Save work;
- new `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md`;
- new `SD_CARD_READER_9/` hardware capture;
- this session index/log and specification/MEMORY updates.

Do not discard the Reader 9 capture or assume the working tree should be
cleaned with a destructive Git command. The original Session 061 planning and
analysis documents are now superseded by this handoff and the updated specs.

## End of session block

```text
DATE: 2026-09-08
SESSION GOAL: Add typed HCNAMES support, implement the AutoSave boot reader, and make the Bank-plus-four-Scene power-off workflow restore correctly.
COMPLETED: Added the typed 130-line HCNAMES schema; repaired invalid overlength kit fixture names; implemented matching-winner and HCNAMES-authoritative boot restore, per-row Case 1/2/3 evaluation, narrow loaders, payload apply APIs, Pattern fallback loading, deferred dirty replay, notices, Q diagnostics, complete committed-hierarchy HCNAMES publication, root-CWD readiness, and the zero-growth 96-type lifetime fix; consolidated the later Load/Save test/refactor backlog.
VERIFIED ON HARDWARE: Yes. SD_CARD_READER_9 from the Reader-4 Bank 001 plus four Rollin overlays produced 128/128 Case-2 successes, case2=0xffff, case3=0, no E/X, correct typed HCNAMES, valid HCPR generations 9/10, and zero refreshed-witness/object-mask disagreements. The separate mixed matching-winner reboot remains outstanding.

CHANGES THIS SESSION:
- Core/Hardware/SD/filesystem.c/.h: typed HCNAMES; both boot readers; narrow loaders; Pattern load; HCNAMES regeneration; latch/notices; hierarchy publication; shared 144-byte scratch.
- Core/Bank/Scene/Autosave.c/.h: wire-payload-to-resident apply and source extraction APIs; record geometry unchanged.
- Core/Bank/Scene/AutosaveTrace.h and tools/decode_devlogs.py: Q boot-reader diagnostics.
- Core/Hardware/SD/asyncfatfs/asyncfatfs.c: root chdir now returns only when root currentDirectory is idle.
- main.c: stage-10b winner validation and stage-11 three-way restore decision.
- Core/Menu/menu.c: non-blocking boot notices.
- tools/verify_bank_autosave.py: typed HCNAMES verification.
- SD_CARD/: canonicalized overlength kit Instrument stems and references.
- Core/Menu/SplashAnimation.c/.h, Core/Hardware/frontPanel/lcd.c, main.c, Makefile: static splash/CGRAM work; animation call remains disabled.
- AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md and SCOPING_TARGETS.md: deferred Load/Save/AutoSave edge-case backlog consolidation.
- knowledge_files/log_archive/000_SESSION_INDEX.md, knowledge_files/log_archive/061_SESSION_HANDOFF_LOG.md, specification sheets, MEMORY.md: durable closeout.

KNOWN ISSUES INTRODUCED: No known AutoSave regression. The splash animation is compiled but disabled; its CGRAM restoration code/comments need review before enabling. Menu-exit and browser-refresh issues are documented for the later Load/Save pass.
KNOWN ISSUES RESOLVED: Missing Pattern restore/silent boot; inherited numeric Instrument-source rejection; stale Scene child HCNAMES publication; Bank-plus-four-Scene authoritative restore; destructive cross-Scene Instrument-type lifetime; Err BKKit14 fixture incompatibility.

NEXT SESSION RECOMMENDED GOAL: Design and implement Pattern data storage in AutoSave with explicit format/version, ownership, bounded snapshot, dirty-marker, and recovery rules before undertaking the general Load/Save refactor.
BLOCKERS: None for Pattern work. One useful but non-blocking acceptance test remains: reboot SD_CARD_READER_9 and capture the mixed matching-winner Case-1/Case-2 result.

CRITICAL REMINDERS FOR NEXT SESSION:
- HCNAMES is typed identity/provenance authority; HCPR embedded names are not live identity.
- Keep the row-0/all-R HCNAMES-authoritative gates and the all-or-nothing Scene rule.
- Direct numeric Instrument source is invalid only when the Instrument row supplied it; inherited numeric ancestry is valid.
- Preserve all 96 Instrument types outside destructive payload staging for the complete traversal.
- Pattern is boot-loaded best effort but is not yet in HCPR; Effects are still absent.
- The canonical dirty mask is singular; enable tracking only after setup, then replay the boot latch.
- Use AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md as the deferred cross-system test/refactor list.
```
