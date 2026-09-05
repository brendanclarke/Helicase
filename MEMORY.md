# LXR-02 Firmware Port — Project Context

This file is the working memory for Codex/Claude Code/LLM Agent sessions on this project.
Read it fully at the start of every session before touching any code.
Update it whenever something is confirmed, fixed, or decided.

---

## Quick Start

```
# Repository root is the working tree root

# Build
make && make img   →   build/LXRV2_lxr02.img

# Flash: copy LXRV2_lxr02.img to SD card root, hold main encoder, power on
```

**Current working source**: Session 061 `.hcnames` instrument-type field
implementation on branch `dev-ph3-autosave-ph6` (planning commits `919cfcc`
and `ce796ae`); the code and documentation edits for
`S061_HCNAMES_INST_TYPE.md` are uncommitted and await hardware card
verification. Clean logging-on build reports `text=392,148`, `data=404`,
`bss=96,184` (no new RAM; the header/type work reuses existing scratch).
`.hcnames` now opens with a `#types` header line and Instrument rows
(33..128) carry a mandatory third column with the typed-directory token.
Hardware tests 1-4 in `S060PHASE_D_RE_DIRTY.md` Section 5 remain the
outstanding Session 060 Phase D work; Session 061 verification steps are
listed at the end of `S061_HCNAMES_INST_TYPE.md`.

## RAM Allocation Approval Policy

All presently uncommitted on-chip RAM capacity is reserved, not general feature
headroom: free DTCM (including capacity released by moving `transientData` to
FLASH) is reserved exclusively for future delay-line buffers, while free normal
SRAM1 is reserved exclusively for future Pattern data. Before implementing any
new or enlarged RAM allocation in this project, explicitly identify its exact
byte count, memory region, lifetime, and owner, then obtain the user's
acknowledgement. This applies to globals, static storage, linker sections,
pools/unions, DMA buffers, and any material stack-budget increase, in this and
future sessions. A change that releases RAM does not authorise reuse of that
capacity for another subsystem.

Logging/trace allocations are approved only while `DEV_MODE_LOGGING` is
enabled and the corresponding logging path is compiled. A logging-off build
must not allocate those rings, cursors, or timing records.

## Volatile Notes

This section is for short carryover points only. Flush or rewrite it at session
end; durable facts belong in `knowledge_files/log_archive/` or
`knowledge_files/specification_reference/`.

- Read `knowledge_files/log_archive/040_SESSION_HANDOFF_LOG.md` before
  continuing Scene/Bank or filesystem work. It preserves the verified
  Session 040 implementation, the Bank Load fix, and archived root notes.
- Session 042 completed the HCNAMES/name-SRAM and filesystem-cache refactor.
  Read `042_SESSION_HANDOFF_LOG.md` before changing identity ownership,
  `.hcindex`, `.hcnames`, typed staging, canonical-name repair, Bank masks, or
  Instrument Load's reversible `kit` row. Archive that handoff before deleting
  the Session 042 working plans/logs.
- Current filesystem authority:
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` and
  `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`. API
  boundaries and live memory ownership are in `MODULE_INTERCHANGE_SPEC.md` and
  `SRAM_MANIFEST.md`; the latter records the Session 059 logging-on linked
  allocation and totals. AutoSave format/writer authority is `AUTOSAVE.md`;
  development-mode and logging authority is `DEV_MODES.md`. Read
  `SESSION_040_AFATFS_FOLLOWUP.md` before extending AsyncFATFS. The complete
  reference set is indexed below, including the historical DSP audit, live
  memory manifest, module map, and oscillator-interpolation document.
- The hardware has no real-time clock. FAT timestamps may retain fixed/default
  values and must not be used to order or validate device operations. Use file
  contents, CRC/generation fields, structural comparison, and trace order.
- Source layout is now `Core/Bank/Scene/` and `Core/Bank/BankData.*`, not
  `Core/Scene/`.
- Scene/Bank saves persist compact v3 `pattern.pat`: seven 32-hex-character
  rows representing the exact 112-byte 128-step x 7-track on/off bitmap. v1
  placeholders remain accepted and v2 imports its final bit field only;
  length/scale and all other former Pattern fields are disposed. This is not
  the final dynamic Pattern format.
- Session 043 completed the bitmap Pattern/LUT/tagged-runtime/transient-ROM
  storage pass. Read `043_SESSION_HANDOFF_LOG.md` before changing PatternData,
  slider conversion, InstrumentManager runtime ownership, DTCM placement, or
  the RAM-allocation approval policy. `transientData` is FLASH-resident;
  target-audio stress validation of that placement remains pending.
- Session 044 completed the cold-boot tagged-runtime/LFO activation and runtime
  Scene/Bank Load terminal ordering. Read
  `044_SESSION_HANDOFF_LOG.md` before changing Scene activation,
  `preset_startDrumsetApply()`, Load:Bank preview masks, HCNAMES/index ordering,
  or the accepted OK/OW command UI.
- Resident Instrument parameter values and target selectors are compact bytes:
  instrument_param_value_t and instrument_target_token_t. Target off is 0xff;
  wide descriptor/Scene IDs exist only for lookup/runtime resolution. Velocity
  targeting is self-scoped plus its Morph token, while LFO voice selection
  supports self, voices 1..6, and scn.
- Bank has a 16-Scene resident workspace. Its v2 manifest carries
  active_scene and a 16-bit scene_mask_voice_edit; Bank-local Scene folders
  are 00..15. Bank Load delegates each selected local payload through the
  shared Scene loader, and Bank Save serializes the selected children through
  direct exact-root delete/recreate. This is not a crash-recoverable
  transaction; temporary/old promotion names are not used.
- Instrument membership is fully dynamic at boot and runtime. SceneData must
  initialize before InstrumentManager constructs tagged members. Scene
  activation clears outgoing targets, image-applies all six incoming types,
  then performs one all-source two-LFO-pair/velocity rebind; cold boot starts
  that exact ordinary Scene worker after audio startup. Never assume a fixed
  Drum/Snare/Cymbal/HiHat slot arrangement.
- Bank Load must reset shared Scene child-discovery scratch before every
  Bank-local Scene payload. Otherwise a full Bank reuses child 00's embedded
  Kit/pattern/effect names for child 01, which surfaces as BnkL14 (the Bank
  wrapper's decimal phase 20 rendered in hexadecimal). The saved Bank tree is
  valid; this is a loader-state isolation requirement. The reset helper is
  implemented in filesystem.c, documented in filesystem.h, and was confirmed
  by the user on hardware in Session 040.
- Instrument, Kit, root Scene, and root Bank Load/Save now share exactly one
  `fs_list_cache_name[1000][9]` display-name cache (9,000 bytes). Instrument
  rows are sorted; numbered-library rows are direct `000..999` slot rows with
  blank rows preserved. Root `/.hcnames` temporarily borrows its first 129
  rows. Menu entry/type changes and exit dispose or reload the same cache; no
  per-instrument or per-library name cache is allowed.
- Root `/.hcnames` is the authoritative active identity **and provenance**
  register: row 0 Bank; rows 1..16 Scene; rows 17..32 Kit; rows 33..128 six
  Instruments per Scene. Every row is `name<TAB>source`; `-`, `?`, `000..999`,
  and `@` are its only source tokens. The 258-byte filesystem-owned source
  register survives name-cache reuse and replaces the retired 32-byte
  SceneData source array. Runtime holds exactly 81 bytes of musical identity:
  one Bank, one Scene, one Kit, and six Instrument names. `scene_t` and `kit_t`
  contain no display names or retained filename stems. Because text rows have
  variable length, a targeted update reads all 129 paired rows, overlays only
  its owned rows, and rewrites the file.
- HCNAMES paired source correction: a successful non-empty root Bank Load must
  stage row 0 to its direct `op_slot` before the Bank-owned HCNAMES close gate,
  just as the empty-Bank branch does. `settings.cfg` is the 17-line
  source-free system file: it supplies `active_bank` for boot selection and
  never stores Scene sources. Legacy `scene_source_NN` keys are accepted only
  to be ignored during migration.
- Instrument Load AutoSave root-Instrument and InstrumentMrp fixtures are
  hardware-confirmed:
  root-pool Instrument commit marks type plus owned Normal/Morphable Morph
  endpoints for every committed destination; InstrumentMrp marks only committed
  Morphable Morph endpoints; hidden `kit` restore marks nothing. Authority:
  `048_SESSION_HANDOFF_LOG.md` and `AUTOSAVE.md`. Hardware inspection must leave both nested
  Instrument Load and the parent Load/Save page, then wait through both the
  normal five-second debounce and the bounded record transaction. That page
  intentionally holds the canonical mask and trace in RAM, while HCNAMES
  defers its `@` publication until the nested session closes. A copied card
  while browsing—or after a lone trace `S` but before `A...T`—cannot disprove
  an immediate marker. Field evidence on 2026-08-11 showed HCNAMES `@` for a
  normal pool file while tracking was live but its Instrument offsets were
  absent; Menu's temporary-operation latch was therefore rejected as request
  provenance. The corrective path reads the existing filesystem request flag,
  without allocating new state. The same field pass exposed the map's
  Bank-present precondition, so the final commit also publishes each actual
  destination before marking it. A subsequent current-image capture still had
  no Instrument `D` offsets, so no further persistence-path guess is allowed:
  `autosave_markWholeInstrumentDirty()` now emits a fixed-size `I` trace
  summary with map-valid/tracking/all-published flags and expected/accepted
  counts. The acknowledgement fix was then hardware-confirmed: a normal root
  load at Scene 5/slot 0 produced `J flags=0x03`, `I flags=0x07` with 76
  expected/accepted bytes, and a successful `A/V/M/C/P/T` sequence; the newly
  committed `.hcprms2` generation 2 persisted `drm` Normal/Morph bytes matching
  `brezeld1.drm`, while HCNAMES row 63 was `brezeld1 @`. The preserved AutoSave
  name `rollind1` is correct because names/sources remain HCNAMES authority.
  A later combined hardware fixture wrote generation 3 in `.hcprms1`: Scene
  5/slot 0 Normal and Morph values matched `brezeld3.drm`, while Scene 5/slot
  1 changed only its Morph allocation and matched `casiopd3.drm`; its type,
  AutoSave name allocation, and Normal allocation were byte-for-byte
  unchanged from generation 2. The retained 64-entry trace ring wrapped the
  earlier root `I`/`J` records behind two Drum-Morphable dirty sweeps, but its
  `A/V/M/C/P/T` sequence reached successful generation-3 publication. Only
  the reversible-`kit` fixture remains pending; the observer has no new RAM
  allocation or record-size change.
- The first nested Instrument-entry `kit` label delay is now observed with
  trace stage `N`: paired entry, HCNAMES read/flush, `.hctmp` snapshot, and
  typed-index request/completion ticks, each with Scene/slot/type. It is a
  diagnostic-only producer in the existing logging ring and must identify the
  slow step before entry behavior changes.
- Root Instrument commits additionally emit `J` immediately after SceneData
  assignment: requested/called flags prove whether the whole-marker gate was
  reached before its `I` map/tracking/publication outcome. This is diagnostic
  only and exists because clean-card captures currently retain only `S`.
- User-approved Menu exit queue: one normal-SRAM1 byte,
  `menu_pendingPageSwitch` (zero none; page-plus-one encoding), retains the latest requested
  non-Load page only while a Load/Save owner holds `menu_storageBusy`. The
  Menu poll consumes it via the ordinary page-switch exit path before any
  browser retry. This prevents a physical mode-button exit from being silently
  dropped during Instrument I/O/apply; it owns no payload or name data.
- Every create-capable HCNAMES path first completes and closes a
  case-insensitive root absence proof. A NULL read open is not absence; one
  folded match permits one read retry, while duplicate matches and every
  scan/open/close/FAT failure remain errors and authorize no creation.
- Typed load staging is a separate aligned 2,048-byte union, never the
  9,000-byte name cache. It holds one Kit, one Instrument candidate, or Scene
  settings plus one Kit. Scene Pattern data is excluded: after settings and
  the embedded Kit validate and commit, Pattern loads directly into the final
  resident Scene slot and is intentionally non-atomic pending Pattern redesign.
- Boot writes `/Kit/.hcindex`, `/Scene/.hcindex`, `/Bank/.hcindex`, and the
  four registry-owned Instrument indexes one at a time, then reloads
  `/Bank/.hcindex` before initial Bank selection because Instrument generation
  disposes the shared cache. Kit/Scene/Bank Save performs a physical parent
  rescan and complete `.hcindex` rewrite before releasing its callback, then
  refreshes the current Save slot display. Runtime Instrument entry opens the
  selected type's index directly and repairs only missing/empty/corrupt
  metadata; it does not regenerate every index.
- A pure root Scene/Bank Load does not use the Save rebuild. It commits
  payload/HCNAMES, publishes the completed result, applies the active Scene
  through the shared runtime worker, then reloads the unchanged selected
  `.hcindex` read-only as the accepted command's final step. Load:Bank entry
  must also preview the highlighted Bank's children and hold input until its
  destination mask is valid; a resident Bank index alone is not selection
  readiness.
- Accepted OK/OW requests alone own `menu_loadSaveCommandActive`: render `...`,
  suppress every cursor, and retain input locking until true terminal work
  finishes. Preparatory index/preview work may use `menu_storageBusy` without
  showing `...`. Every completion resets to the bracketed type row.
- Sessions 045–048's committed AutoSave implementation is the
  accepted baseline, not rejected work: exact 34,768-byte A/B records, one 3,856-byte
  canonical mutation mask, bounded mask/value capture with atomic
  take/re-dirty behavior, typed scalar markers, source-free v1 settings, and
  the AutoSave lifecycle trace. Available scalar controls are accepted as
  hardware tested: Scene; Kit/Instrument; and MIDI channel/note, which are
  Scene values. There is no user-changeable Bank scalar control for another UI
  test. Do not reopen this as a vague coverage matrix. Authority:
  `knowledge_files/specification_reference/AUTOSAVE.md`.
- Production currently includes four pre-audio SD timing holds (250 ms before
  SD init, paced ACMD41 with one-second timeout, 50 ms post-mount, and 50 ms
  pre-Bank). The intermittent boot hang that motivated them is not reproducible
  or localized. If it recurs, capture a non-perturbing stage/operation deadline
  before adding more delays; Dev Mode's `lcd_waitForIdle()` changes timing at
  many internal transitions.
- The former Kit/Scene/Bank per-slot presence/display/alias arrays are retired
  (69,000 bytes combined). Session 042 also removed the dead `kitBrowser`
  compatibility bridge and its 1,000-entry `kb_map`, releasing the linked
  2,004-byte SRAM allocation. Kit browsing now uses only the filesystem-owned
  slot cache/index accessors.
- The former Bank child alias/presence arrays, 64-entry File/Dir list caches,
  and firmware recursive-delete stacks are gone. Session 058 added one bounded
  16-by-9 display-name capture so Bank Load scans the selected Bank once.
  Two unreachable 49-byte File/Dir Menu strings plus nine result bytes still
  link (107 bytes total).
- Never add object self-name fields to `sceneset.scg`, `bankset.bcg`, or
  instrument files. Object identity comes from directory/file names.
- For overwrite code, enter the correct parent root first, parse visible child
  names with the right product parser, prove zero/one candidate, capture the
  complete selected afatfsObjectInfo_t, and delete only that exact same-slot
  object. Native afatfs_deleteTree copies its identity and completes
  asynchronously; its false return means no callback. This is documented in
  `FILESYSTEM_SPEC.md` and `ASYNCFATFS_REFERENCE.md`.
- File/Dir/sDir are no longer in the normal type cycle; their compatibility
  filesystem/Preset calls do no work, although residual Menu display code and
  the two 49-byte strings remain linked. There are exactly two development
  flags: `DEV_MODE_DIAGNOSTIC` is screen-only and `DEV_MODE_LOGGING` is
  file-only; trace is logging. The current configuration is
  diagnostic 0 and logging 1, with the temporarily approved 2,048-record
  trace ring (normal default: 64). Current file outputs are `/bootlog.bin` and
  `/asavetrc.bin`; there is no implemented `/devlog.bin`. A timed-out
  `ASENSURE` may append a 64-byte raw diagnostic capsule to its normal
  eight-byte boot token (72 bytes total). The completed read-only decoder is
  `tools/decode_devlogs.py`; it decodes `/bootlog.bin` and `/asavetrc.bin` and
  is the tool to use for card analysis. Authority:
  `knowledge_files/specification_reference/DEV_MODES.md`.
- Session 050 is closed in
  `knowledge_files/log_archive/050_SESSION_HANDOFF_LOG.md`. Its verification
  supersedes the disposable root planning/evidence files for Scene Load
  publication. Those files may be deleted after retaining the handoff and
  specification references; they are not future authority.
- Session 051 implemented the Scene HCNAMES follow-up: Menu now admits a
  nonzero Scene dirty mask at the physical Load/Save exit boundary, and flushes
  that mask before a later Kit-family type can overwrite the operation-scoped
  identity block. The existing filesystem writer and identity publication are
  unchanged. A single-destination Scene 015 Machine -> Scene 15 fixture is
  hardware-confirmed: Scene/Kit/Instrument HCNAMES rows and generation-2
  publication are correct. Multi-destination, deferred-exit, toggle, hazard,
  and failure fixtures still need hardware evidence.
- Session 051 also implemented the InstrumentMrp reversible `kit` row. It
  displays the selected slot's HCNAMES Instrument name, writes a Morph-only
  projection to the existing `Instrument/<type>/.hctmp.<ext>` transport, and
  restores only Morphable Morph endpoint cells. The first hardware pass exposed
  that the restore copied the staged normal image into the Morph endpoints;
  presetManager.c now dispatches a Morph-to-Morph staged commit when
  filesystem_loadedInstrumentWasMorphTemporary() is set. The repaired image is
  hardware-confirmed. Type, Normal image, HCNAMES identity/source, and routing
  remain untouched.
- Session 052 closed Bank Load persistence. `settings.cfg` now re-serializes
  `active_bank` after a successful Bank Load/Save, and the AutoSave Bank
  scene-present mask equals the effective selected-child union.
  `bank_setScenePresentMask()` now returns whether it changed, and Bank Load
  re-marks the two present-mask bytes on a no-op union. Hardware-verified with
  Bank 008: active_bank=8, scene_present_mask=0xffff, B trace commit/drain
  0xffff, and `tools/verify_bank_autosave.py` PASS. Deferred to
  `SCOPING_TARGETS.md`: the boot Kit-quarantine refactor (KQ019KST), the Bank
  Save present-mask union (P1), and the boot settings-mark redundancy (P2).
The disposable `SESSION_052_PRE_PLAN.md` and `SESSION_052_POST_ANALYSIS.md`
are superseded by `knowledge_files/log_archive/052_SESSION_HANDOFF_LOG.md`.
- Session 053 reimplemented AsyncFATFS recursive delete and the Bank/Scene/Kit
  overwrite path (exact-object delete/recreate, direct Bank Save, structured
  remove/rename results, no tmp/old promotion) and relaxed the LFN shape
  validator to accept 0xffff padding. Source builds (text 379,660, data 396,
  bss 94,612) but the overwrite matrix is UNPROVEN on hardware: Scene overwrite
  returned ScnS05 though the slot was replaced, Kit Save did not materialize,
  Kit Save menu was empty, and boot Bank Load still times out (B012S09I).
  HCNAMES source provenance is not updated on Save (reports loaded slot, not
  saved). The AutoSave boot Bank section is empty by design (tracking enabled
  after the boot Bank Load) and is deferred to the AutoSave reader milestone.
  Deferred targets are in SCOPING_TARGETS.md; durable closeout is
  knowledge_files/log_archive/053_SESSION_HANDOFF_LOG.md. The four working docs
  (SESSION_053_PRE_PLANNING.md, RECURSIVE_TREE_DELETE_REIMPLEMENT.md,
  KIT_PARSE_BOOTLOCK_RESOLVE.md, LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md) may be
  deleted; their durable facts are preserved in the handoff and SCOPING_TARGETS.
- 2026-08-21: investigated a boot hang/timeout with no `/bootlog.bin` on the
  card. `tools/devlog_unpack.py` (new, low-token sibling of
  `decode_devlogs.py`, same lookup tables) showed the captured
  `/asavetrc.bin` was an ordinary clean prior session with no `X`/`E`
  records — the actual finding was that a raw blocking call inside the
  pre-audio window that never returns (`SD_init()`, bit-bang SPI, or a fixed
  `timebase_holdPreAudioMs()` hold) skips every cooperative check, so nothing
  ever runs to log it. Added `DEV_LOGGING_IWDG` (config.h, default 1) as the
  fix: starts the STM32F765 IWDG once at boot, fed from both foreground pumps
  (`filesystem_tick()` and `filesystem_blockPoll()`, never an ISR) so a genuine
  hang stops feeding and the IWDG resets the MCU on its own ~32.8s native
  period; a software `DEV_LOGGING_IWDG_EXPIRE` (2 minutes, config.h) ceiling
  separately catches a foreground loop that keeps ticking forever without
  finishing boot. **The flag now defaults to 0 and is UNVERIFIED on hardware.**
  Its first version caused an indefinite boot-splash hang (IWDG init spun on
  `IWDG_SR` before writing the `0xCCCC` key that starts the LSI; nothing else
  in this firmware enables the LSI). The init is now correctly ordered, bounds
  every handshake against TIM6 ms, and starts nothing if `LSIRDY` never
  appears. The `filesystem_blockPoll()` feed is mandatory: modal sample install
  bypasses `filesystem_tick()` and would otherwise be reset mid `sampleFlash`
  erase/program. Enable deliberately, not as a default. Since the IWDG has no early-warning interrupt on this part, a 12-byte
  capsule in a new `.devwdg_noinit` SRAM2 section (approved ceiling 32 bytes;
  see SRAM_MANIFEST.md) survives the reset and is replayed to `/bootlog.bin`
  on the next boot via the existing `filesystem_writeBootFailureLogBlocking()`
  path — no new on-card format. No NVIC/interrupt configuration is touched;
  the IWDG has no interrupt line on this part. Full contract in DEV_MODES.md
  and config.h. UNPROVEN on hardware — this has not yet been exercised
  through an actual reproduced hang.
- **Build-system footgun (found 2026-08-21):** the Makefile has NO header
  dependency tracking (no `-MMD`/`-MP`/`-include *.d`). Editing `config.h`
  alone does not rebuild anything, so a flag flip followed by a bare `make`
  silently produces a binary with the OLD flag value and identical reported
  sizes. Always `make clean` after editing a header, or add `-MMD -MP` plus
  `-include $(OBJS:.o=.d)`. This affects every `config.h` experiment
  (`DEV_MODE_*`, trace sizes, timing constants), not just one feature.
- Session 2026-08-21 Scene-Pattern fix: Pattern is the only Scene payload
  playback/UI address through `seq_activePattern`/`menu_shownPattern` instead
  of `scene_getActiveIndex()`. Bank Load committed a new active Scene without
  realigning them, so Scene Load wrote the committed Scene while the sequencer
  read Scene 0 — presenting as "Scene Load never loads the pattern". Fixed by
  new `seq_alignActivePatternToScene()` (state realign only: no LED notify, no
  MIDI program change, no note-off) plus `menu_setShownPattern()`, called at
  the Bank Load phase-20 commit. Scene Load itself was correct and unchanged.
  Confirmed by trace (`R DONE=1 mask=0x0020 {5}`) and by Session 055's
  hardware round-trip testing (loading a Scene after a PERF Scene switch now
  plays/displays immediately). Authority: `SCENE_LOAD_PAT_RESTORE.md`,
  `knowledge_files/log_archive/054_SESSION_HANDOFF_LOG.md`.
- Session 054 closed the pinned recursive-delete/overwrite target
  (`SCOPING_TARGETS.md`'s "duplicate-slot overwrite"), through five
  successive bugs found by an iterative card-driven diagnostic process, not
  by inspection alone:
  - **Bug #1** — `filesystem_deleteSlotDirectory_tick()` had two completion
    gates that treated its own diagnostic-only 50,000-poll stall latch
    (`op_delete_slot_timeout_observed`) as a hard failure by itself, so a
    nested Scene delete slow enough to trip the counter reported
    `FS_STATUS_ERROR` even after finishing correctly (`ScnS05`, seen even
    though the slot was actually replaced). Fixed: only the scan's own
    error/duplicate latches or the native result now decide pass/fail; the
    stall is purely observational and durably traced (see the `'X'`
    `PHASE_STALL` stage below).
  - **Bug #2** — none of the four Save paths (Kit/Scene/Bank/root
    Instrument) staged `filesystem_setResidentSource()`, so a saved row kept
    reporting its previously *loaded* slot as HCNAMES source. Root Instrument
    Save's gap was deeper: it never reached any HCNAMES publish path at all
    (`filesystem_requestUpdateResidentInstrumentNames()` had zero callers
    repo-wide) — fixed by adding a hand-off into
    `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` plus a new type-filtered
    index-rebuild path. See `FILESYSTEM_SPEC.md`'s Save/Overwrite Safety
    section.
  - **Bugs #3/#4/#5 — the actual `ScnS05` root cause**, found only after
    building exhaustive failure-site instrumentation into the native
    `afatfs_deleteTree()` traversal (17-value
    `afatfsDeleteTreeFailureSite_e`) and iterating repeated card retests: the
    descend/ascend path depends on one invariant —
    `file->directoryEntryPos` must always identify whichever directory the
    handle currently has open, since that is the only thing distinguishing
    "the delete root just emptied, finish" from "a nested child just
    emptied, ascend". Two *sequential* bug fixes (Bug #3: stop
    reconstructing a resume target from an unset `parentCluster`; Bug #4:
    remove a redundant, apparently-unreliable parent re-scan) each correctly
    fixed their own symptom but broke one half of that invariant in the
    process — Bug #3's fix cleared `directoryEntryPos` as a side effect of
    reusing `OPEN_DIR`'s reset code, and Bug #4's fix assumed
    `op->currentTarget` survived a descent untouched, when the descended-into
    child's own internal deletes overwrite that register with every object
    they process. **Bug #5** is the real fix: snapshot the child's identity
    and directory-entry pointer at the moment of descent
    (`descendTarget`/`parentEntry` on the persistent `afatfs.deleteTreeState`
    singleton) and restore both on ascend, one level deep only (matching the
    existing `parentCluster` depth bound; `afatfs_deleteTree()` has exactly
    one caller, never more than one nested directory below a delete root).
    **Hardware-confirmed Session 055**: a full Kit-modify-save,
    Instrument-modify, Scene-modify-save, and Bank-save-then-load round trip
    reported no errors. The dedicated low-level acceptance matrix (malformed
    LFN, cyclic/broken-parent, injected FAT/cache error) is still unexercised
    as its own fixture set — see `ASYNCFATFS_REFERENCE.md`.
  - Extensive new, permanent diagnostics were added while chasing this:
    delete-slot failure-reason classification (`fs_delete_slot_reason_t`),
    the native 17-site failure-site enum above, a shared
    `filesystem_pollPhaseStall()` edge-triggered stall detector (also used at
    Bank Save entry and the runtime AutoSave drain — the drain site is the
    one place a stall now forces a real error completion instead of only
    observing, since it previously had no bounded escape at all), an ordered
    per-Save-type lifecycle trace (`'O'`), and a universal `'E'` backstop on
    both of filesystem.c's shared terminal-completion functions so a future
    failure path nobody thought to instrument still gets caught by
    construction. All documented in `DEV_MODES.md`'s stage-letter list.
  - Card damage found and repaired along the way: six root Scene folders were
    missing `effects.fx` and/or their embedded Kit contents, all consistent
    with an *earlier* interrupted, non-atomic Scene Save (the old tree is
    deleted first, and `effects.fx` is written last of ~12 sequential file
    operations, so an interruption anywhere in between leaves a stub that
    Load's current all-or-nothing child check permanently rejects). Repaired
    on-card; **no save-path hardening was implemented** — four ranked options
    (tolerate a missing `.fx`; write it earlier; clean up a failed save;
    real atomic commit) are recorded but deferred. See
    `knowledge_files/log_archive/054_SESSION_HANDOFF_LOG.md` for the full
    round-by-round trail and every file/line changed.
  - `AUTOSAVE_TRACE_RECORD_COUNT` is still the temporary, approved 2,048-record
    expansion (`config.h:255`, normal default 64) adopted for this
    investigation. **Needs a decision**, not yet reverted: keep it while any
    further recursive-delete/Save-path work is plausible, or shrink back to
    64 now that the pinned target is hardware-confirmed closed.
- Session 055 hardware round-trip testing (Kit/Instrument/Scene/Bank
  load-modify-save) confirmed Session 054's fixes above and surfaced a new
  Load-menu freeze, root-caused across two rounds — **do not re-litigate as
  one bug, they are different defects**:
  - **Round 1 (real, but not the freeze)**: an unbounded, no-backoff retry
    livelock — Menu re-posted a doomed Kit/Scene entry request once per
    foreground pass for as long as AutoSave held the filesystem facade,
    destructively clearing the shared name cache each time before
    discovering the refusal. Self-recovering in under a second once AutoSave
    released, but it visibly rendered the page against a cleared cache (a
    Scene stem appearing on the Kit row) and burned the foreground. Fixed by
    gating the deferred-selection dispatch on `filesystem_status() !=
    FS_STATUS_BUSY` and adding a non-destructive early busy-check to
    `menu_requestResidentNameScratch()`.
  - **Round 2 (the actual freeze)**: `menu_showFilesystemErrorOverlay()` —
    the shared terminal path for nearly every failed Menu filesystem
    operation — never called `filesystem_ack()`, so one failed read parked
    the facade at `FS_STATUS_ERROR` **permanently**, silently killing both
    the AutoSave writer and the trace flush from that moment on (both are
    admitted only while the facade is `IDLE`). This is why *both* freeze
    captures ended cleanly with a normal AutoSave completion and contained
    zero evidence of the freeze itself. Separately, two request helpers
    (`menu_requestInstrumentIndexLoad()`, `menu_requestLibraryIndexLoad()`)
    raised `menu_storageBusy` expecting acceptance and left it stuck on
    refusal, self-deadlocking their own only retry path. Fixed (6 sites
    total, all in `menu.c`, no RAM cost). **Closed per user hardware
    confirmation** — freeze no longer reproduces. General rule this
    reinforces, now stated once in `AUTOSAVE.md`: every Menu-side filesystem
    terminal path, success or failure, must `filesystem_ack()`.
  - **Deferred, not fixed**: the AutoSave writer reads the shared 9,000-byte
    name cache live while serializing its record, and Menu clears that same
    cache directly (bypassing facade arbitration) from 18 call sites. Only
    the two hottest callers were closed. The general hazard (a torn AutoSave
    record, not a hang) needs a proper ownership interlock — deliberately
    left as a `SCOPING_TARGETS.md` item rather than an architecture change
    riding along with a freeze fix.
  - Also fixed this session: a permanent (not merely delayed) failure to
    restore the cached `kit` row in Load: Instrument/InstrumentMrp when
    scrolling back to it while the previewed pool file's apply was still
    draining — the cursor latched to `kit` before the restore was even
    attempted, and a declined attempt was never retried. Fixed by tracking
    restore-owed-ness as slot state (data), not as one call's outcome.
    Preliminary hardware check looked correct; full test matrix (endless pot,
    InstrumentMrp, stopping dead on `kit`) not yet exhaustively run.
  - Also fixed (provable race, root cause of the exact symptom shape but not
    confirmed as the *only* contributor — no trace record survived from the
    actual incident): a stale Bank/Scene/Kit name briefly shown with the
    correct slot number on fresh Load/Save entry, because the page's shared
    tail repaint ran unconditionally before the async `.hcindex` reload it
    had just posted could complete. Fixed by gating that one repaint behind
    `!menu_storageBusy`, matching the convention already used elsewhere in
    `menu.c`.
  - Full investigation, evidence, and rejected-first-attempt record:
    `knowledge_files/log_archive/055_SESSION_HANDOFF_LOG.md`.
- Root-directory working docs for Sessions 054-055 (`SESSION_054_PREPLAN_ASYNC_RECURSIVE_CLEANUP.md`,
  `SESSION_054_PLAN_DEFECT_EVIDENCE_FIX.md`, `AFAT_RECURSIVE_WHITEPAPER.md`,
  `SCENE_LOAD_PAT_RESTORE.md`, `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`,
  `SESSION_054-055_TESTING.md`, `S055_KIT_LOAD_FREEZE_FIX.md`,
  `S055_INST_RESTORE_FIX.md`, `S055_BANK_NAME_ENTRY_FIX.md`) are superseded by
  `054_SESSION_HANDOFF_LOG.md`/`055_SESSION_HANDOFF_LOG.md` and the
  specification-reference updates above; their durable facts are preserved
  there and they may be deleted.
- Session 056 fixed two independent AsyncFATFS bugs and added the autosave
  page-exit expedite:
  - **LFN duplicate-creation fix**: `afatfs_createFileContinue()` exited the
    directory scan early when a viable free-run of deleted entries was found,
    before checking whether the target file already existed later in the
    directory. Fixed with a latch-and-continue approach (4 sites, 2 new struct
    fields). Hardware-verified: no duplicate files across two-boot test.
  - **Cluster-boundary file-size fix**: `afatfs_fseekAtomic()` did not call
    `afatfs_fileUpdateFilesize()`, leaving `logicalSize` at 0 for new files so
    only `physicalSize` (cluster-rounded) was persisted. This was the root cause
    of the previously unexplained 32,768-byte `.hcprms` files. Fixed with one
    added call. Hardware-verified: all `.hcprms` files now 34,768 bytes.
  - **Page-exit expedite**: `fs_autosave_page_suppressed` flag in
    `filesystem_autosaveWriterSchedule_tick()` resets the writer deadline to
    250 ms after leaving the Load/Save page, instead of waiting for the original
    5-second debounce. Pending hardware verification. Does not reduce the
    inherent two-cycle recovery cost (~12 s) but eliminates wasted gap time.
  - Root-directory working docs `S056_AFATFS_DUPLICATE_CORRECTION.md` and
    `S056_AFATFS_FILE_CLUSTER_CORRECTION.md` are superseded by
    `056_SESSION_HANDOFF_LOG.md` and may be deleted.
- Session 057 closed the wrap-session punch list (P1 Bank Save present-mask
  union; P2 redundant settings write accepted as-is; the AutoSave writer's
  Bank-identity-mismatch case now copy-forwards instead of forcing full
  regeneration), then implemented and mostly-tested three larger items and
  root-caused a live Bank Save screen freeze:
  - **`settings.cfg` safe write** (temp file `settings.tmp` + `afatfs_sync()`
    + remove-old/rename-promote, plus a boot recovery prelude and a
    self-checking `lines=17` terminator) — hardware-tested with a deliberate
    mid-promotion power-cut simulation, PASS. The only fully closed-loop
    hardware-verified item from this session.
  - **Empty-Scene/Bank overwrite guard** — `filesystem_requestSaveBank()`
    filters the save mask against `bank_scenePresentMask()`
    (`bank_hasResidentBank()`-gated) before the state machine starts; root
    Scene Save's case 0 refuses outright if the source isn't present. Code
    landed, build-verified, **not hardware-tested**.
  - **Boot Kit-directory sanitizer replaced with lazy quarantine-on-failed-
    load.** Boot no longer opens/parses any Kit payload (closes a real
    false-boot-failure risk that scaled with library size — 308 blocking ops
    on the 44-Kit test card, one shared 10 s deadline); a folder is renamed
    `err...` only when an actual Load attempt proves it invalid. Root Scene
    Load cascades a Kit-layer failure to the owning Scene; Bank Load never
    renames a Bank-local Scene folder (positional identity) and never fails
    the whole Bank for one bad child — `filesystem_lastBankLoadFailedSceneMask()`/
    `preset_bankLoadFailedSceneMask()` surface it through the existing error
    overlay instead. Build-verified; hardware-tested only for
    boot-timing/regression (checklist items 1/2/4/5/6). **The behavioral
    tests — including the single most important one, the boot-safety
    regression test — were not run.** Treat the false-boot-failure fix as
    verified by code review only, not by hardware evidence, until that test
    runs.
  - **Bank Save rebuilt as per-Scene delete-then-write**, replacing
    total-tree-delete-then-recreate. Fixes `ErrS05` (a quarantined
    `errKit` directory's rewritten LFN entries broke `afatfs_deleteTree()`'s
    scan on the next Bank Save) and an independent, previously-undocumented
    bug: a partial `Save:[Bank]` (subset mask) used to silently delete every
    **non-selected** resident child too. Hardware-tested via a full 16-Scene
    Bank Save producing a byte-identical, uncorrupted 161-file tree.
  - **Bank Save/Load screen freeze — root-caused and closed.** A user-visible
    freeze (static `...`, no recovery short of hard reboot) was first
    misdiagnosed as AsyncFATFS handle exhaustion (the operation reliably
    stalled after exactly 5 children, matching `AFATFS_MAX_OPEN_FILES`);
    built out into a full diagnostic build (pool bumped 5->8, a handle
    census accessor, a phase-20 hard check) that then **disproved its own
    hypothesis** — handle count stayed at exactly 1 per child, never
    accumulating. Actual cause: a total-duration watchdog
    (`op_bank_total_ticks`/`FS_BANK_TOTAL_TICK_LIMIT`) counted
    `filesystem_tick()` foreground polls and treated the count as an
    elapsed-millisecond budget; at the measured ~7,600 polls/second its
    300,000-poll limit fired at a fixed ~39.5s regardless of Bank size,
    aborting otherwise-healthy operations mid-write (the 32,768-byte all-
    `0xFF` "corrupt" instrument files from earlier hardware tests were this
    watchdog's provisional-write artifact, not an oversized serializer).
    Fix: removed the watchdog outright (no replacement counter/timer of any
    kind — see the CRITICAL REMINDERS in `057_SESSION_HANDOFF_LOG.md`), made
    the two stall detectors whose abort could fire mid-callback during a Bank
    operation (`BkSt` Bank Save entry, `ScSv` Scene Save)
    **trace-only**, and reverted the handle-pool bump. **Verified directly
    against current source, not merely restated from the closeout document**:
    six sibling stall detectors added the same session (`KtSv`, `KtLd`,
    `ScLd`, `BkLd`, `StWr`, `Flsh`) were *not* reverted and still abort on a
    stall; a handle-census hard-abort (`BkHd`) at Bank Save's per-child entry
    also remains permanently live, ungated by the new `DEV_STALL_DETECTION`
    toggle. Hardware-accepted: full 16-Scene Bank Save now completes in
    ~2.5 minutes with no corruption (latency itself is deferred to Session
    058, `S058_BANK_LOAD_SPEEDUP_PROPOSAL.md`).
  - `sizeof(afatfsFile_t)` is corrected to **188 bytes** (was recorded as 328
    in `ASYNCFATFS_REFERENCE.md`; the old figure predated moving expanded
    delete state out of every handle). Final build, independently re-verified
    this logging pass by running `arm-none-eabi-size` directly:
    `text=380,436 data=408 bss=94,848`, a net -832 text/+8 data/+48 bss versus
    the Session 056 close-out build.
  - **The Session 056 page-exit expedite was never re-verified on hardware
    this session**, despite being first on this session's own priority list
    at the outset. Still exactly where Session 056 left it: code-complete,
    not hardware-tested.
  - Still open, not touched this session: the sequencer chaselight
    disappearing bug (traced but not root-caused — top hypothesis connects to
    the still-open "single-source-of-truth Pattern/Scene index" item below);
    an HCNAMES-row-0-vs-`settings.cfg` Bank-identity discrepancy on the
    current test card (no trace evidence exists to root-cause it); the
    `bootlog.bin`/`asavetrc.bin` duplicate-name re-verification (plausibly
    narrowed by the Session 056 LFN fix, but not actually re-checked); the
    name-cache ownership interlock; the top-level Load/Save entry trace gap;
    `AUTOSAVE_TRACE_RECORD_COUNT` (still 2048, deferred again).
  - Full detail, including every point above independently re-verified
    against current source rather than only summarized from the session's own
    planning documents:
    `knowledge_files/log_archive/057_SESSION_HANDOFF_LOG.md`. The six
    `S057_*.md` root planning documents this session produced are superseded
    by that log and by the `specification_reference/` updates it made
    (`FILESYSTEM_SPEC.md`, `ASYNCFATFS_REFERENCE.md`, `DEV_MODES.md`,
    `MODULE_INTERCHANGE_SPEC.md`, `SRAM_MANIFEST.md`) and may be deleted.

- Session 061 (`.hcnames` instrument-type field) is implemented but
  unverified on hardware: `.hcnames` now has a `#types` header line and
  Instrument rows 33..128 serialize/validate `name<TAB>source<TAB>type`
  (`drm|snr|cym|hat`) before any optional `R`. A header or type mismatch
  invalidates the register: readers close/remove it and the next
  write-capable pass (Bank Load at boot, update rewrites) regenerates it.
  Authority: `S061_HCNAMES_INST_TYPE.md`; verify its steps 1-7 on the card,
  and expect the old dev-card `/.hcnames` to be deleted and rebuilt at the
  next boot. `tools/verify_bank_autosave.py` now skips the header and
  cross-checks Instrument row types against kitset members.

---

## Repository Layout

```
./
├── MEMORY.md                        ← this file
├── README.md                        ← confirmed hardware, known issues, critical reminders
├── main.c
├── config.h
├── Makefile
├── STM32F765VIHx_FLASH.ld
├── requirements.txt
├── tools/
│   └── build_lxrv2_img.py          ← packages ELF → LXRV2_lxr02.img
├── build/                           ← generated, not in VCS
├── knowledge_files/
│   ├── SESSION_HANDOFF_TEMPLATE.md ← template for writing new session handoff logs
│   ├── ENHANCED_FEATURES.md        ← future enhancement notes
│   ├── OSC_INTERP_AUDIT.md         ← oscillator interpolation audit
│   ├── specification_reference/
│   │   ├── ASYNCFATFS_REFERENCE.md    ← low-level async FAT/VFAT API contracts, pumping, LFN/object identity, deletion, and caller rules
│   │   ├── AUTOSAVE.md                 ← authoritative hidden A/B format, dirty ownership, writer lifecycle, limitations, and validation status
│   │   ├── CPU_USE_DSP_AUDIT.md       ← historical DSP timing/performance audit, cache/MPU/IRQ findings, and ordered optimization record
│   │   ├── DEV_MODES.md                ← authoritative screen-diagnostic versus file-logging policy and current log formats
│   │   ├── FILESYSTEM_SPEC.md         ← authoritative product filesystem, kit/instrument files, Scene/Bank storage, and save/load target spec
│   │   ├── MODULE_INTERCHANGE_SPEC.md ← current direct-call API ownership/boundary map through Session 059
│   │   ├── OSC_INTERP_AUDIT.md        ← oscillator waveform interpolation implementation, persistence, runtime behavior, risks, and validation
│   │   └── SRAM_MANIFEST.md           ← current Session 059 linked snapshot and binding reservation policy
│   ├── hardware_archive/
│   │   ├── HARDWARE_MAP.md         ← full confirmed pin table, IRQ numbers
│   │   ├── AVR_TO_F765_MIGRATION.md ← architectural notes, sequencer ISR design baseline
│   │   ├── FRONTPANEL_AUDIT.md     ← legacy front-panel bridge elimination audit
│   │   ├── SD_CARD_INVESTIGATION.md ← SD false-positive analysis (PA8, 74HC165)
│   │   └── XP_CONNECTOR_MAPS.md   ← ribbon cable pin mappings
│   └── log_archive/
│       ├── 000_SESSION_INDEX.md    ← index of all sessions with keyword lookup
│       ├── 001_SESSION_HANDOFF_LOG.md
│       ├── 002_SESSION_HANDOFF_LOG.md
│       ├── 003_SESSION_HANDOFF_LOG.md
│       └── ...
└── Core/
    ├── globals.h
    ├── datatypes.h
    ├── Src/
    │   └── startup_stm32f765xx.s
    ├── Hardware/
    │   ├── clocks.c/h               ← sysclk_init(), FPU enable via CPACR
    │   ├── timebase.c/h             ← SysTick 4kHz mainboard tick, TIM6 1kHz counters + 500Hz foreground service, TIM7 5kHz LCD drain
    │   ├── AudioCodecManager.c/h    ← consolidated audio: DMA ISRs, I2S/GPIO/DMA init, SPSC queue
    │   ├── triggerJacks.c/h         ← CLK OUT/IN, RST IN; OUT jack detect is foreground-polled
    │   ├── memtest.c/h              ← flash sector probe (boot-time, MEMTEST_ENABLED gate)
    │   ├── frontPanel/
    │   │   ├── buttonHandler.c/h    ← ISR-safe event ring, main-loop processEvents()
    │   │   ├── lcd.c/h              ← TIM7-driven async queue, 128-entry SPSC ring
    │   │   ├── ledHandler.c/h
    │   │   └── IO/
    │   │       ├── adcPots.c/h      ← sliders RV5-10, ADC1 DMA
    │   │       ├── din.c/h          ← 74HC165×5 buttons, SPI1
    │   │       ├── dout.c/h         ← 74HC595×5 LEDs, SPI1
    │   │       ├── encoder.c/h      ← SW42, TIM1 IC, Dannegger, accel + rebound suppression
    │   │       └── endlessPots.c/h  ← RV1-4, atan2 delta tracking
    │   ├── SD/
    │   │   ├── filesystem.c/h       ← public facade: typed async load/save/name/scan operations; Kit load/save uses root Kit/ directories; root Instrument load/save exists
    │   │   ├── storageTypes.c/h     ← Kit/instrument text parser+writer, numbered-folder parser, descriptor file schema helpers
    │   │   ├── SPI/
    │   │   │   ├── spi_sd.c/h       ← bit-bang SPI: PC12/PD2/PC8/PD0
    │   │   │   └── sd_routines.c/h  ← SD_init() only; blocking read/write superseded
    │   │   └── asyncfatfs/
    │   │       ├── asyncfatfs.c/h   ← Betaflight asyncfatfs modified for LXR-02, with LFN/case object APIs
    │   │       ├── fat_standard.c/h
    │   │       ├── sdcard.h
    │   │       └── sdcard_lxr02.c/h ← SD block-device shim over bit-bang SPI
    │   └── USB/
    │       ├── OTG_Driver/
    │       ├── Device_Library/
    │       └── App/                 ← usb_manager, usb_midi_core, etc.
    ├── Menu/
    │   ├── menu.c/h                 ← full port, all pages, load/save UI
    │   ├── menuPages.h              ← 16-page × 8-subpage table
    │   ├── MenuText.h               ← all label strings
    │   ├── Cc2Text.c                ← modTargets[] 205 entries
    │   ├── CcNr2Text.h
    │   ├── copyClearTools.c/h       ← copy/clear UI; pattern mutation through PatternData
    │   └── screensaver.c/h          ← screensaver with explicit LCD off/on phases
    ├── MIDI/
    │   ├── Uart.c/h                 ← USART3, 31250 baud, interrupt-driven dual FIFO (realtime + normal)
    │   ├── MidiRealtime.c/h         ← 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP
    │   ├── FIFO.c/h
    │   ├── MidiMessages.h           ← full mainboard version (MIDI_NRPN_* prefix)
    │   ├── MidiNoteNumbers.h
    │   ├── MidiParser.c/h
    │   ├── MidiVoiceControl.c/h
    │   ├── SeqStep.h
    │   └── valueShaper.h
    ├── Bank/
    │   ├── BankData.c/h             ← resident Bank display name, active Bank-local Scene slot, loaded-bank flag
    │   └── Scene/
    │       ├── SceneData.c/h        ← Scene-owned settings, kit slots, descriptor images, MIDI routing
    │       ├── SceneModTargets.c/h  ← Scene-level modulation target namespace: 1vm..6vm plus Scene srt
    │       ├── Pattern/
    │       │   ├── PatternData.c/h      ← pattern/track/step storage and edit API
    │       │   ├── EuklidGenerator.c/h  ← pattern generator
    │       │   ├── SomData.c/h          ← SOM data tables
    │       │   └── SomGenerator.c/h     ← SOM pattern/performance generator
    │       └── Preset/
    │           ├── ParameterArray.h/c   ← supersedes Parameters.h; NUM_PARAMS=275
    │           └── presetManager.c/h    ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── DSP/
    │   └── Instruments/
    │       ├── InstrumentManager.c/h ← descriptor registry, VOICE menu lookup, Scene-to-DSP apply bridge
    │       ├── Drum/                 ← Drum descriptor keys, flags, menu layout, runtime metadata
    │       ├── Snare/                ← Snare descriptor keys, flags, menu layout, runtime metadata
    │       ├── Cymbal/               ← Cymbal descriptor keys, flags, menu layout, runtime metadata
    │       └── HiHat/                ← HiHat/open-hat descriptor keys, flags, menu layout, runtime metadata
    ├── SampleRom/
    │   ├── SampleMemory.c/h         ← sample flash metadata/runtime cache, 120 entries, loop flags
    │   └── sampleFlash.c/h          ← guarded F765 sector 6-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h       ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2) — Session 019
    │   ├── sequencer.c/h            ← original LXR sequencer source (driven by TIM3_IRQHandler)
    │   ├── clockSync.c/h
    ├── DSPAudio/
    │   ├── random.c/h               ← F765 RNG port (PLL48CLK, bare register)
    │   └── [all DSP voice files]    ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h              ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```

### Where to look for things

| Question | File |
|----------|------|
| Which session introduced a fix? | `knowledge_files/log_archive/000_SESSION_INDEX.md` |
| Full details of a fix or decision? | `knowledge_files/log_archive/00x_SESSION_HANDOFF_LOG.md` |
| Current filesystem, instrument/kit file, Scene storage, menu, and DSP propagation spec? | `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` |
| Current AutoSave format, ownership, writer, and limitations? | `knowledge_files/specification_reference/AUTOSAVE.md` |
| Development screen diagnostics and file logging? | `knowledge_files/specification_reference/DEV_MODES.md` |
| Current module/API ownership boundaries? | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` |
| Confirmed pin assignments / IRQs? | `knowledge_files/hardware_archive/HARDWARE_MAP.md` |
| Sequencer / DSP architecture plans? | `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md` |
| Current known issues and reminders? | `MEMORY.md` |

### Specification-reference index

These are the eight authoritative/reference documents under
`knowledge_files/specification_reference/`. `FILESYSTEM_SPEC.md` is the
product-level source of truth; `ASYNCFATFS_REFERENCE.md` is its low-level
filesystem implementation companion. For AutoSave specifically,
`AUTOSAVE.md` is authoritative and `FILESYSTEM_SPEC.md` contains only the
product-filesystem boundary. `DEV_MODES.md` exclusively owns development-mode and
logging behavior. The remaining documents are audits or API/feature references
and may contain historical snapshots as noted below.

| File | What it contains | Use it when |
|------|------------------|------------|
| `ASYNCFATFS_REFERENCE.md` | Foreground-pumped async FAT32/VFAT contracts: component paths, LFN/SFN identity, object iteration, removal, terminator-aware directory-entry publication, lazy directory-cluster initialization, and flush boundaries. | Changing `Core/Hardware/SD/asyncfatfs/` or adding filesystem operations. |
| `AUTOSAVE.md` | Implemented hidden A/B wire format, ownership, canonical dirty mask, writer lifecycle, power-loss behavior, CRC limitation, and accepted scalar validation status. | Changing AutoSave format, dirty hooks, capture, scheduling, or recovery. |
| `CPU_USE_DSP_AUDIT.md` | Historical DSP performance audit covering render scheduling, IRQ priorities, caches/MPU, ITCM/DTCM, SIMD/FPU, DMA, hot-loop costs, and an ordered optimization record. | Investigating audio underruns or changing render placement/optimization. It describes an audited snapshot, not necessarily current ownership. |
| `DEV_MODES.md` | Screen-only diagnostic versus file-only logging contract, current `bootlog.bin`/`asavetrc.bin` formats, duplicate limitation, and failed unified-log warning. | Adding or interpreting diagnostics, trace, or logging output. |
| `FILESYSTEM_SPEC.md` | Current product storage specification through Session 059: root layout, name indexes and typed-index recovery, Kit/Instrument schemas, Scene/Bank storage, load/save reachability, overwrite safety, and verification anchors. | Changing product storage, serialization, load/save, or instrument propagation. |
| `MODULE_INTERCHANGE_SPEC.md` | Live direct-call ownership map through Session 059 for Pattern, UI, sequencer, Preset, instruments, modulation, MIDI, filesystem, AsyncFATFS, storageTypes, and boot. | Connecting modules or deciding which layer owns a new API/state transition. |
| `OSC_INTERP_AUDIT.md` | Implemented oscillator waveform interpolation feature: global parameter/UI/runtime state, render behavior, settings persistence, file-level changes, risks, and hardware validation checklist. | Changing oscillator interpolation or its global save/load behavior. |
| `SRAM_MANIFEST.md` | Current Session 059 logging-on linked snapshot, AutoSave/trace owners, and binding Pattern/delay reservation policy. | Changing retained state, adding caches/names, or evaluating RAM cost. Regenerate after allocation changes. |

---

## Project Goal

Port LXR 0.37 to the LXR-02 hardware (STM32F765VIH6). Original LXR: STM32F4 audio + ATmega644 AVR front panel. LXR-02: single STM32F765.

- This folder is the repository/codebase.
- `knowledge_files/LXR-master/` is read-only reference material only. Do not modify it.
- Knowledge docs should be updated when architecture changes; session logs live under `knowledge_files/log_archive/`, current direct-call API boundaries live in `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`, and filesystem plus descriptor instrument/kit storage rules live in `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
- **Original source reference**: `knowledge_files/LXR-master/` — AVR in `front/LxrAvr/`, STM32F4 in `mainboard/LxrStm32/src/`

## General Process Reminders

- Always verify the local working repository directory before writing code.
- **Always `make clean` after editing `config.h` (or any header).** The Makefile
  has no header dependency tracking — no `-MMD`, no `-MP`, no `-include *.d` —
  so editing a header rebuilds *nothing*. A flag flip followed by a bare `make`
  silently produces a binary containing the OLD value, with byte-identical
  reported sizes, which makes it look like the change had no effect. This
  affects every `config.h` experiment (`DEV_MODE_DIAGNOSTIC`,
  `DEV_MODE_LOGGING`, `DEV_LOGGING_IWDG`, `AUTOSAVE_TRACE_RECORD_COUNT`, the
  timing constants). Never trust an incremental build across a header edit, and
  never report build sizes from one. The durable fix is adding `-MMD -MP` to
  `CFLAGS` plus `-include $(OBJS:.o=.d)` to the Makefile. Confirmed 2026-08-21.
- Blocking for 1ms anywhere in the main loop or any ISR at priority <= 4 is unacceptable.
- Runtime SD/file work must remain asynchronous; boot-only synchronous polling is allowed before audio starts.
- New code should be commented at detailed contract level: why the function,
  variable, or storage type exists; what it does; inputs/outputs; and
  clients/accessors/affiliates. Do this proactively, not as a cleanup after the
  user asks again.

---

## Hardware

### MCU: STM32F765VIH6, TFBGA100

- SYSCLK = 216MHz (HSE 16MHz, PLLM=16, PLLN=432, PLLP=2)
- PCLK1 = 54MHz (APB1/4) — TIM6, USART3, I2S2, I2S3
- PCLK2 = 108MHz (APB2/2) — SPI1, ADC
- PLL48CLK = 48MHz (PLLQ=9) — USB, RNG
- PLLI2S: N=271, R=2 → 135.5MHz → Fs=44108Hz
- Application origin: **0x08008000** — VTOR must be set at startup
- Stack top (SP): **0x20080000**
- DTCM 128KB (not DMA-accessible) + SRAM1 368KB + SRAM2 16KB
- I-Cache enabled (16KB, ICIALLU invalidate) — Session 13.
- D-Cache enabled (16KB) with MPU (WT for SRAM, SO for DMA buffers) — Session 13.
- DMA buffers live in the `.dma_nocache` linker section, marked Strongly-Ordered via MPU.
- `audioOutBuffer` lives in DTCM (`INDTCMZ`) for single-cycle access.

### Flash Sector Layout (single-bank, confirmed via memtest)

| Sector | Range | Size | Use |
|--------|-------|------|-----|
| 0 | 0x08000000–0x08007FFF | 32KB | LXRV2 Bootloader |
| 1–5 | 0x08008000–0x0807FFFF | — | Application |
| 6–11 | 0x08080000–0x081FFFFF | 6×256KB | Sample storage, implemented Session 18 |

**Erase floor: sector 6.** `sampleFlash.c` must hard-reject any erase below sector 6.

### Confirmed GPIO

| Peripheral | Pins | Notes |
|-----------|------|-------|
| LCD (4-bit parallel) | PE7-PE10 (DB4-7), PE11 (E), PE12 (RS) | TIM7 async driver |
| LEDs — 74HC595×5 | PA7 MOSI, PB3 SCK, PB2 LATCH | SPI1 |
| Buttons — 74HC165×5 | PA6 MISO, PB3 SCK, PB2 LATCH | SPI1, shared with LEDs |
| SW43 SHIFT/BAR1 | PB7 switch (active HIGH), PB8 LED | |
| Main encoder SW42 A/B | PE13/PE14 | AF1 TIM1_CH3/CH4, external 10kΩ pull-ups (R75/R76), NO internal pull-up |
| Main encoder SW42 SW | PE15 | Internal pull-up |
| Endless pots RV1-4 | PB0/1, PC4/5, PC2/3, PC0/1 | ADC1, atan2 delta tracking |
| Sliders RV5-10 | PA0-PA5 | ADC1 DMA (RV5↔RV6 label swap corrected in config.h) |
| DAC2 (CS4344, U24) | PB12 WS, PB13 CK, PB15 SD, PC6 MCK | I2S2, AF5 |
| DAC1 (CS4344, U23) | PA15 WS, PC10 CK, **PB5** SD, PC7 MCK | I2S3, AF6. **SD=PB5 not PC12** |
| SD card (bit-bang) | PC12 SCLK, PD2 MOSI, PC8 MISO, PD0 CS, PD1 DETECT | NOT SPI1. Hardware SPI remapping impossible on this board. |
| USB MIDI | PA11 D-, PA12 D+ | OTG_FS, ADUM3160 isolator, VBUS sensing disabled |
| MIDI DIN | PB10 TX, PB11 RX | USART3, 31250 baud |
| CLK OUT | PC13 | |
| CLK IN | PD4 | GPIO input pull-up, EXTI4 rising edge on low-to-high transition |
| RST IN | PD5 | GPIO input pull-up, EXTI5 rising edge on low-to-high transition |
| OUT1 L detect | PD6 | GPIO input pull-up; no plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |
| OUT1 R detect | PD7 | GPIO input pull-up; no plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |
| OUT2 L detect | PB4 | No plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |
| OUT2 R detect | PB6 | No plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |

### IRQ Assignments (current — Session 025)

| IRQ | Priority | Handler | Function |
|-----|----------|---------|----------|
| IRQ10 | 3 | EXTI4_IRQHandler | CLK IN — PD4 rising edge, push to trigger event ring |
| IRQ15 | 4 | DMA1_Stream4_IRQHandler | I2S2/DAC2 — audio refill master |
| IRQ23 | 3 | EXTI9_5_IRQHandler | RST IN — PD5 rising edge; PD6/PD7 masked and polled as jack state |
| IRQ27 | 1 | TIM1_CC_IRQHandler | Main encoder A/B input capture |
| IRQ29 | 2 | TIM3_IRQHandler | 4kHz sequencer timing owner: processRealtimeEvents → triggerJacks_tick → seq_tick |
| IRQ39 | 5 | USART3_IRQHandler | MIDI DIN RX/TX; timestamps bytes with TIM2; realtime bytes → MidiRealtime ring |
| IRQ47 | 4 | DMA1_Stream7_IRQHandler | I2S3/DAC1 — audio slave (flags only) |
| IRQ54 | 6 | TIM6_DAC_IRQHandler | 1kHz — counters + foreground front-panel service flag |
| IRQ55 | 7 | TIM7_IRQHandler | 5kHz — LCD queue drain |
| IRQ67 | 5 | OTG_FS_IRQHandler | USB MIDI |

TIM6 now schedules `timebase_serviceFrontPanel()` from the foreground loop.
Shift-register exchange, PD6/PD7/PB4/PB6 jack detect, encoder-button debounce, and
endless-pot angle processing no longer run in the TIM6 ISR.

---

## Audio Pipeline (AudioCodecManager.c)

`audioCodec_init()` is the **single hardware entry point**.

**Main loop pattern:**
```c
if (audioCodec_queueFreeSlots() > 0) {
    // Fill one AUDIO_DMA_FRAMES hardware slot as three OUTPUT_DMA_SIZE blocks.
    for (frame = 0; frame < AUDIO_DMA_FRAMES; frame += OUTPUT_DMA_SIZE)
        mixer_calcNextSampleBlock(&buf[frame * 2], &buf2[frame * 2]);
    audioCodec_commitRenderBuffer();
}
```

**OUTPUT_DMA_SIZE = 32** is the effective LXR-master DSP/control block (confirmed correct in Session 019 — do NOT revert to 16).
**AUDIO_DMA_FRAMES = 96** is the hardware DMA half; render budget per queued hardware slot is **2.18ms** = 471,288 cycles at 216MHz.

**DSP render must remain in the main loop.** Moving it to the DMA ISR creates a hard 2.18ms ceiling — when DSP expands beyond the budget the system locks up with no graceful degradation. Main loop allows unlimited expansion with underruns as the graceful signal.

**SPSC queue**: 2-slot queue. `ready_head` is owned by ISR, `ready_tail` by main loop, and `ready_count` tracks free/full state for queueing and diagnostics.

**CPU-use widget**: `audioCodec_getQueueFreePercent()` uses DWT cycle-counter accounting of `ready_count < 2`. This is an audio queue-free pressure meter, not generic MCU utilization.

**24-bit output path (Session 022)**: `audioOutBuffer`/`audioOutBuffer2` are `sample_mx_t` (int32_t, signed-24 value in container). `pack_half()` emits a true 24-bit payload in both halfwords of each I2S frame. Do NOT re-add `LSW = 0` zeroing. Do NOT add a `>> 8` shift in `sampleMix_toS24()` — int16 voice values enter the mixer already scaled as `int16 << 8` via `bufferTool_convertInt16ToSampleMix()`; adding `>>8` at pack time was the loudness regression fixed in session 022.

**Zero startup underruns**: boot SD operations complete before `audioCodec_init()`.
Runtime kit/all/performance sound-apply completion is chunked after audio starts
(Session 027). Session 044 made the worker type-safe: it clears outgoing
targets, reset/image-applies at most one quiet pending slot per pass, then keeps
the Scene gate active while the existing Instrument cursor rebinds all sources.
Boot establishes a synchronous pre-audio image, then starts this exact ordinary
worker after `audioCodec_init()` so LFO/velocity installation matches a manual
Scene switch.

### Internal DAC — Must Never Be Enabled

The STM32F765 has an internal 12-bit DAC on PA4 (DAC1_OUT) and PA5 (DAC2_OUT). These pins are also ADC1_IN4 and ADC1_IN5, used as slider inputs for RV6 and RV5. **The internal DAC must never be enabled.** Accidental DAC peripheral clock enable or pin mode change silently corrupts slider ADC readings.

`TIM6_DAC_IRQHandler` is the vector table name for IRQ54 because TIM6 and the internal DAC share an IRQ line by ST hardware design. **This does not mean the DAC is in use.** Audio is handled entirely by external CS4344 codecs via I2S2 and I2S3.

---

## SD Card Architecture (implemented Sessions 12, 17, and Phase 2 start in 030)

**SD operations must never block the main loop or any ISR at priority ≤ 4.**

**Implemented solution**: asyncfatfs (Betaflight/Cleanflight library), fronted by `filesystem.c/h`. Ground-up FAT16/FAT32 reimplementation with polling-based non-blocking I/O. Replaces ChaN FatFS entirely. Decision made in Session 11, implemented in Session 12, and reorganized behind the filesystem facade in Session 17.

**Card format policy (Session 025)**: FAT16 and FAT32 are supported; MBR-FAT32 is recommended. FAT12 and exFAT are unsupported. Boot detection for unsupported layouts shows `Unsupported card` / `use MBR-FAT32` for 5 seconds and the filesystem is not mounted, so no boot load is attempted from that card.

**Architecture:**
```
Core/Bank/Scene/Preset/presetManager.c / Menu
  → filesystem.c (typed operations: Bank/Scene/Kit/Instrument/Morph/legacy globals)
    → asyncfatfs/asyncfatfs.c (afatfs_fopen/fread/fwrite/fclose/mkdir/chdir/poll)
      → asyncfatfs/sdcard_lxr02.c (sector transfer FSM, 16 bytes/burst)
        → SPI/spi_sd.c (bit-bang SPI)
```

**Key design points:**
- `afatfs_poll()` is the single entry point for all SD background work. Called from main loop initially; can migrate to TIM5 ISR later if throughput is insufficient.
- `afatfs_poll()` must be called from **one context only** — never both main loop and ISR simultaneously.
- Session 023 rate-limits idle foreground polling to reduce CPU use. Active/busy operations and filesystem initialization still poll every pass.
- 8-sector LRU cache (4KB) between filesystem logic and SD card. Cache hits are free (no SPI traffic).
- `sdcard_lxr02.c` implements `sdcard_readBlock`/`sdcard_writeBlock`/`sdcard_poll` on top of `SPI/spi_sd.c`. Each `sdcard_poll()` call clocks a burst of 16 SPI bytes (~9µs). A 512-byte sector completes in 32 polls.
- `filesystem.c` serializes operations — one SD operation at a time. Request functions return immediately; completion is signalled via callback/status.
- Asyncfatfs has five open-file slots (`AFATFS_MAX_OPEN_FILES`). Each
  `afatfsFile_t` is **188 bytes** (corrected Session 057; previously recorded
  as 328 bytes — that figure predated moving expanded delete state out of
  every handle), so the increase from three slots costs 376 bytes.
  `afatfs_chdir()` copies directory state into `currentDirectory`, so callers
  must close explicit parent/child directory handles after entering them;
  handle capacity is not a substitute for correct lifetimes. **Do not raise
  this pool to fix a stall without new evidence** — a Session 057 Bank Save
  freeze looked exactly like handle exhaustion (stalled after precisely 5
  children) and was disproven by trace evidence after bumping the pool to 8;
  the real cause was unrelated (`057_SESSION_HANDOFF_LOG.md` §13-§14).
- `filesystem.c` owns the filetype registry and add-a-filetype checklist. Non-SD clients include `filesystem.h` only.
- Authoritative filesystem and instrument-file spec lives in
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`. The former
  root `FILESYSTEM_SPEC.md` compatibility pointer was deleted in Session 033.
  Target root directories are `Bank`, `Scene`, `Kit`, `Pattern`, `Sample`,
  `Wavetable`, `Effect`, `Instrument`, and root `settings.cfg`. Current work
  includes root `Kit/`, `Instrument/`, `Scene/`, 16-Scene `Bank/` load/save,
  Kit/Instrument Morph Save, and the strict allowlisted settings.cfg format;
  current global restore is not legacy glo.cfg.
- `storageTypes.c/h` owns text storage schemas, parser/writer helpers, and
  descriptor-keyed parameter maps. Keep it free of `asyncfatfs` calls and keep
  function names prefixed `storage_`.
- Normal kit load now scans root `Kit/` directories named `NNN Name` or
  compatibility `NNN_Name`, then loads `kitset.kcg` plus six instrument files.
  Normal Kit Save now creates/opens a numbered Kit directory and writes
  `kitset.kcg` plus six instrument files. Numbered library slots are direct
  `000..999`; slot `000` is real for all filetypes. KitMrp Save uses the same
  directory shape and writes the current interpolated Morph state into both
  normal and morph endpoint sections.
- Scene Load/Save uses root `Scene/NNN Name/` folders with `sceneset.scg`,
  embedded `Kit <name>/`, draft `pattern.pat`, and placeholder `effects.fx`.
  `sceneset.scg` never stores `name`.
- Bank Load/Save uses root Bank/NNN Name/ folders with bankset.bcg and
  Bank-local two-digit Scene children 00..15. It loads/saves the selected set
  of resident Scenes using the version-2 manifest rather than a one-Scene
  bridge. Bank Load is always mask-selective: it intersects the requested mask
  with discovered children and never treats an empty intersection as “all.”
  Unselected resident Scene payloads, HCNAMES rows, and present-state remain
  unchanged.
- Session 036 adds asyncfatfs LFN component creation/object iteration through
  `afatfs_mkdir_lfn()`, `afatfs_fopen_lfn()`, `afatfs_opendir_lfn()`, and
  `afatfs_findNextObject()`. These preserve SFN display case, create VFAT LFN
  entries, support case-sensitive matching for production LFN opens, return
  generated 8.3 aliases for identity opens, and expose file/directory object
  kind. They are now used by product name repair, Kit/Scene/Bank
  scan/load/save, root Instrument scan/load/save, and the hidden Instrument
  temporary file. Future save code should
  reuse/extend these filesystem-owned primitives rather than creating local FAT
  writers in callers.
- Dot-prefixed files/directories are real filesystem objects. asyncfatfs
  exposes them; product scanners filter only after object iteration. In
  particular `.hctmp.<ext>` is excluded from Instrument indexes and repair.
- Filesystem-level exact-object recursive delete/recreate now exists for Kit
  and root Scene replacement. It is non-atomic; no old/temporary promotion or
  power-loss-safe commit is claimed. **Root Bank replacement is different as
  of Session 057**: the Bank directory itself is scanned/reused (not deleted
  and recreated), and each selected child is individually deleted-then-
  written in place — non-selected resident children are never touched. See
  `057_SESSION_HANDOFF_LOG.md` §10.
  Hidden AutoSave A/B publication uses its separate commit-last contract in
  `AUTOSAVE.md`; it is not the abandoned per-library-file dot-backer design.
- Root `Instrument/` is a separately scanned, type-filtered source pool.
  Instrument Load shows a `kit` row above numbered row `000`. On menu entry or
  voice change, the original voice is saved as
  `Instrument/<type>/.hctmp.<ext>` and its name is retained in one nine-byte
  Menu label. Returning to `kit` reloads that exact file through the ordinary
  one-candidate stage. The reversible source is invalidated on Scene, voice,
  type, load-type, mode, or nested-menu exit; the dirty hidden file may remain
  on SD. A rapid negative encoder delta at the upper boundary must clamp to
  `kit` and clear stale deferred pool requests so it can scroll downward again.
- Root Instrument Save is entered from Save-page VOICE press and writes one
  resident Scene/voice slot to `Instrument/<stem.ext>` using the same
  descriptor-keyed text writer and `self` serialization rule as Kit Save.
- Instrument Load is not merely a raw one-slot memcpy. Preset owns its completion
  transaction: clear every runtime modulation owner referencing the outgoing
  slot before the type/image replacement, reset the incoming runtime, request
  all six Morph/runtime applies, and normalize/rebind all source targets after
  the images exist. Keep mode, Scene, destination, and source request locked
  through both read and commit phases.
- Instrument metadata is firmware-owned registry data, not file content.
  Basic types are unrestricted; at most two Advanced types may be present;
  Choke enables generic VOICE7 `<base>_choke` lookup. Current flags are Drum
  and Snare Basic, Cymbal Advanced, HiHat Advanced|Choke.
- Canonical HiHat decay file keys are `amp_envelope_decay` and
  `amp_envelope_decay_choke`; accept legacy `amp_envelope_decay_closed/open`
  aliases in storage. Non-Choke slot 6 uses generated Scene setting/morph
  endpoint `slot6_track7_amp_envelope_decay` when its descriptor table has the
  base decay; that alternate is Scene-modulatable as `7dc`.
- Kit scan keeps display names in the shared cache and uses FAT short open
  aliases only as operation-local compatibility state. If a card only exposes
  a short alias such as `001SLA~1`, scan falls back to the leading three-digit
  slot so the kit remains loadable.
- Product name repair canonicalizes root numbered folders as `NNN Name`,
  Bank-local children as `SS Name`, and Instrument leaf names as at most eight
  stem cells plus the registry extension. It repairs one rename candidate,
  flushes, and rescans; decimal suffixes remain within eight cells. This is
  ordered FAT mutation, not journaled/crash-atomic repair. No `.hcrepair`
  transaction file exists.
- Large pattern/performance/all files are streamed in bounded chunks and are not staged wholesale in RAM.
- The legacy `kitBrowser.c/h` bridge is retired. Kit, pattern, performance,
  and all-file paths use typed filesystem/Menu accessors and direct slot
  handling.
- Boot path: synchronous polling loop before `audioCodec_init()` (audio not
  running, blocking OK). After a successful mount, boot scans the root Kit,
  Scene, and Bank libraries, writes their slot-ordered `.hcindex` files, then
  scans and writes each registry-owned Instrument index one type at a time.
  Because the one shared name cache is disposed between Instrument types, boot
  reloads `/Bank/.hcindex` before initial Bank selection. There is no opaque
  root `.hcindex` RNG marker in the current design. Boot does not rewrite
  `/.hcnames` from resident SRAM after the initial load: doing so would erase
  names for mask-unselected Scenes that no longer exist in `scene_t`.
- Historical globals compatibility (Session 025): the former glo.cfg/ALL
  binary 22/23-byte behavior is retained here only as an archive note.
  Current filesystem globals use strict keyed settings.cfg version 1 with
  an allowlisted global scope and no glo.cfg fallback.

**SD_init() from `SPI/sd_routines.c` still needed** at boot to bring card to SPI mode (CMD0/CMD1/CMD8/ACMD41). Called before `afatfs_init()`. The rest of sd_routines.c (SD_readSingleBlock, busy-wait loops) is superseded.

**Hardware SPI is impossible** — SD pins have no mapping to any free SPI peripheral on this board, and SPI5/6 are on unbonded TFBGA100 ports. See Session 10 log for full analysis.

**ISR migration path** (future): move `afatfs_poll()` from main loop to TIM5 ISR at priority 6, 10kHz. Everything at priority ≤ 5 preempts it. SD cards tolerate arbitrary SPI clock stretching from preemption. To migrate: init TIM5, move call site, remove main-loop call. No asyncfatfs code changes needed.

**Approaches confirmed wrong** (do not retry):
- Blocking SD calls in main loop — f_open blocks 1–50ms, kills audio
- "Chunking at FatFS-call granularity in main loop" — f_open is still one blocking call, insufficient
- Forking ChaN FatFS for async conversion — 20+ functions, 28 yield points, 6 levels of nesting, unacceptable risk
- NB_FatFS — C++ with heap alloc, lambdas, no FAT16, no _FS_TINY support, 9500 lines

---

## Sample Flash Loading (implemented Session 18)

User samples are installed with explicit modal operations from the Load page:

- `Load:[Samples ]` reads `/samples`, erases sectors 6-11, installs the accepted files, then reads `/loops`, preserves the normal samples, appends as many looped samples as fit, and skips the rest.
- The separate visible `SampLoop` menu item was removed in Session 023; the two existing loaders still run sequentially under the single Samples operation.
- Both loaders accept only mono PCM 16-bit 44.1kHz WAV files. Unsupported files are silently skipped.
- Directory entries are sorted lexicographic by the full long filename when LFN data is present, with ASCII case folded for sort. This is not natural numeric sort; e.g. `Loop 10.wav` sorts before `Loop 2.wav`.
- Installed sample menu labels are compact waveform names: `s01` through `s99`, then `sA0` upward.
- The full encoder-click parameter display shows the filename-derived 8-character display name. Stems longer than 8 characters are compressed as first 4 chars, CGRAM char `0x00`, last 3 chars, preserving case.
- `SampleInfo.size` is now `uint32_t`; the high bit is currently used as `SAMPLE_INFO_LOOP_FLAG`, leaving 31 bits for frame count.
- Metadata and display-name tables live at the top of sample flash; audio payload grows up from `0x08080000`.
- Flash writes use `sampleFlash.c/h`, which hard-rejects sectors below 6 and invalidates D-cache after erase/program.
- Audio is suspended before flash writes and fully reinitialized after. Hardware testing confirmed audio now comes back without reboot after sample load.

Sample flash map:

| Region | Range |
|--------|-------|
| App flash | `0x08008000-0x0807FFFF` |
| Sample data | `0x08080000-0x081FF69F` |
| `SampleInfo[120]` | `0x081FF6A0-0x081FFC3F` |
| Display names[120][8] | `0x081FFC40-0x081FFFFF` |

Important caveat: long samples are not fully solved. `SampleInfo.size` is 32-bit, but oscillator playback still uses the legacy `phase >> 17` index path. Treat long-sample support as unfinished until oscillator phase/index math is widened and hardware tested.

---

## Encoder (SW42)

- **Algorithm**: Dannegger difference — NOT LUT
- **Seed**: `last = new & 3` in `encode_init()` — NOT `(new+3)&3`
- **Divide**: round-toward-zero in `encode_read4()` — NOT `>>= 2`
- **Acceleration**: 8-entry `ts_dirs[]` timestamp buffer, 1×–4× linear multiplier, 100ms decay
- **Rebound suppression**: `ts_dirs[]` majority-direction check — do not remove
- `encode_read1/read2` permanently removed — do not re-add
- When moving `encode_read4()` to any interrupt context: TIM1 (priority 1) can
  preempt lower-priority service code and corrupt `enc_delta` mid-read. The
  current `cpsid/cpsie` guards are acceptable from foreground; an ISR caller
  should use a shadow-copy handoff instead.

---

## Display / Menu

- Full menu system is wired: voice pages, global/MIDI page, load/save page.
- **LCD queue**: head/tail-only SPSC — do NOT reintroduce `lcd_q_count`
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte
- `buttonHandler_processEvents()`: `if` not `while` — intentional
- **Knob repaint**: `menu_knobs_dirty` + `menu_serviceKnobRepaint()` is RV1-4 only
- **endlessPots**: `atan2f(b, a)` — do NOT change argument order
- Saturation in `menu_encoderChangeParameter` / `menu_handleLoadSaveMenu`: int16 sum + clamp
- MODE/SELECT/VOICE button navigation and LED feedback are wired.
- The cosmetic boot splash sequence is all LEDs → title → menu.
- Canonical `.SND` save length is restored to 236 bytes; short kits load with zero-filled tails.
- Display stability under rapid button mashing depends on the SPSC LCD ring behavior above.
- Multi-knob RV1-RV4 repaint collapse is intentional and should remain scoped to that input path.
- Global `cpu` is a read-only audio queue-free pressure widget, not a general MCU utilization meter.
- Runtime kit/all/performance load completion should use
  `menu_startSoundApply()` / `preset_tickDrumsetApply()`; do not reintroduce
  direct `preset_sendDrumsetParameters()` calls in post-audio completion paths.
  The worker must remain active after the six image bits clear until the
  all-source LFO/velocity rebind cursor also drains.

---

## DSP / RNG Rules

- **Compiler**: global flags are `-O2 -flto`; DSP source files use the Makefile's more-specific `-Ofast` rule.
- **FPU**: explicitly enabled in `sysclk_init()` via CPACR
- **VLAs**: forbidden in DSP voice files. Use `static int16_t buf[OUTPUT_DMA_SIZE]`. Snare/Cymbal fixed Session 8, HiHat fixed Session 12.
- **`GetRngValue()`**: returns `int16_t`. Explicit `(int16_t)` cast + `& 0x7FFF` mask at every call site.
- **LFO noise**: `lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f`
- **`RCC_AHB2ENR`**: address is `0x40023834` — NOT `0x40023830`
- **`RNG_CR`**: direct write `RNG_CR = RNG_CR_RNGEN` — NOT `|=`
- **DTCM**: not DMA-accessible

---

## Boot / Init Order (do not reorder)

```c
EXTI_IMR = 0;          // MUST be first
sysclk_init();
lcd_init();            // MUST be before lcd_tim7_init()
lcd_tim7_init();
encode_init(); din_init(); dout_init(); adc_init(); endlessPots_init();
triggerJacks_init();
dsp_init();
seq_init();            // seeds pattern/sub-step defaults; required before sequencer use
euklid_init();
som_init();
initMidiUart(); usb_init();
filesystem_initCardAndMountBlocking(); // card SPI mode + afatfs mount, pre-audio
menu_init();           // calls memset on parameter_values — do NOT also memset in main()
// Boot scans Kit/Scene/Bank, writes their slot-ordered .hcindex files, then
// scans/writes each Instrument type index one at a time; reload Bank index
// after Instrument generation disposes the shared name cache.
// Synchronous boot load tries lowest Bank, then lowest Scene, then lowest Kit, then defaults
// Do NOT snapshot resident names to /.hcnames here; targeted load/save operations own it.
// Synchronous globals load (settings.cfg) via preset_loadGlobals + polling + menu_pollPresetStatus
audioCodec_init();     // single audio entry point — AFTER all SD boot ops
sequencerTimer_init(); // TIM3 4kHz sequencer owner — AFTER audioCodec_init()
// main loop: filesystem_tick() + menu_pollPresetStatus() every iteration
// main loop: midi_service() for DIN/USB drain + flush
// main loop: led_processSeqLedState() (NOT seq_tick — TIM3 owns that)
```

### Sequencer / PATGEN Reminders
- Sequencer-to-LED feedback uses `seq_ledState` in `ledHandler.c`; do not reintroduce the removed front-panel parser bridge.
- `led_processSeqLedState()` drains sequencer LED events in the main loop. Do not move it to an ISR without auditing PatternData and LED RMW races.
- `Core/MIDI/frontPanelParser.c/h` is deleted. Do not replace it with a generic bridge; call owner APIs directly.
- New pattern/track/step/automation work should enter through `pat_*` APIs in `Core/Bank/Scene/Pattern/PatternData.c/h`.
- Sequencer no longer exposes `seq_patternSet`, `seq_tmpPattern`, or `seq_selectedStep` compatibility names in live code. Playback and recording must use PatternData helpers such as `pat_readStep()`, `pat_getEffectiveTrackLength()`, `pat_recordNote()`, and `pat_eraseMainStepSubSteps()`.
- `pat_tmpPattern` is the remaining active-pattern load buffer. Leave it until the 17th Scene/background-bank-load design replaces it; it should be the only temporary pattern storage needed.
- `seq_offsetTrackStepIndexForRotation()` is a narrow runtime hook used by PatternData; UI code should call `pat_setTrackRotation()`.
- `Core/Bank/Scene/Preset/` owns Preset code location. Public names intentionally remain `preset_*`, `parameterArray_*`, and `paramArray_*`; do not rename only part of this API.
- `parameter_values[]` and `parameters2[]` still live in Menu and are known future migration targets for the instrument/file redesign, not a cleanup to do casually.
- Normal Kit/KitMrp load is directory-based through root `Kit/`. Legacy flat
  `.SND` morph paths remain implementation leftovers, not the current promoted
  filesystem model.
- MIDI notes/channels do not belong in `kitset.kcg` or instrument files. They
  belong in future scene settings.
- `BUTTON_TIMEOUT` is milliseconds on this port (`500u`), not original AVR ticks (`38 * 13.107ms`).
- `EuklidGenerator.c` matches original LXR. PATGEN distribution depends on `__CLZ(0) == 32`; do not replace the shim with `__builtin_clz()`.
- `seq_tick()` is owned by `TIM3_IRQHandler`. **Do NOT add seq_tick() back to the main loop.**
- `midiParser_processRealtimeEvents()` and `triggerJacks_tick()` are also owned by TIM3. Do NOT call them from the main loop.
- `voiceControl_processPending()` is called only inside `audio_check_and_render()` before `mixer_calcNextSampleBlock()`. Do NOT call from ISR context.
- DIN TX FIFO inserts and USB MIDI writes from foreground code must use short critical-section wrappers to avoid corruption from TIM3 clock/note output.

### MIDI / Clock Reminders (Session 019)
- TIM2 is a shared free-running 1 µs counter. Do NOT reset it on each pulse. Use unsigned subtraction for deltas.
- TIM5 is free and reserved for future `afatfs_poll()` ISR migration. Do NOT use it for anything else.
- PAR_EXT_SYNC values in order: `off` / `usb` / `din` / `pls` / `aut`. PAR_BPM minimum is 1; value 0 no longer means external sync.
- CC1 on the global MIDI channel controls MORPH as a 7-bit input to a 0..255 parameter: values 0..126 map to `value * 2`, and 127 maps to 255 so the endpoint is reachable. This is handled in the incoming channel-MIDI path, not in `midiParser_ccHandler()`. Do not add CC1 to the CC handler.
- BAR1/BAR2 use `midiParser_playVoiceMidiNote(voice, vel)`. Do NOT revert to direct `voiceControl_noteOn/Off()`.
- Default voice MIDI notes: Drum1=36 … Drum7=42. Overridden by CC2_MIDI_NOTE per-voice setting.
- RST IN semantics: GPIO input with pull-up; low-to-high rising edge resets pattern position without toggling transport or sending MIDI stop/start.
- MIDI realtime bytes (0xF8/FA/FB/FC) are routed to the MidiRealtime ring in the USART3 ISR and must NOT disturb the channel-message running-status parser state.

---

### Morph / Endless-Pot Reminders
- `preset_morph()` is rate-limited by `preset_morphTick()`. Descriptor Morph now runs per voice from Scene-owned instrument images through `presetMorphEngine`; global `mrp` bulk-sets all six per-voice values, and LFO Morph modulation is a hidden overlay centered on the retained per-voice base.
- Do not add a morph skip cache. The request/pass generation scheduler must send a full final pass at the latest morph value.
- Legacy MorphKit load/save still uses `parameters2[]` and flat `.SND` behavior. Do not treat it as the final descriptor instrument morph persistence path.
- RV1-RV4 are analog endless pots, not the digital Gray-code encoder. The driver uses raw A/B snapshot baselines, `ENDLESS_POT_DEADZONE = 20`, `ENDLESS_POT_TIMEOUT_MS = 5000`, and `ENDLESS_POT_DELTA_TIMEOUT_MS = 20`.
- Only `PAR_MORPH` gets endless-pot double angular speed. Do not apply this to all `DTYPE_0B255`; BPM drift exposed that as too broad.

---

## Known Issues / Technical Debt

### Session 051 carryover and known defects

- **Root Scene Load trace and AutoSave publication are hardware-accepted.**
  The final direct Scene/Bank `.hcindex` callback now snapshots status then
  calls `filesystem_ack()` before Menu teardown. A SeaWaked/024 load into
  resident Scene 15 emitted `R=0x8000`, Kit/Scene `L`, `F/W`, and successful
  `A/V/M/C/P/T`; `.hcprms2` advanced from generation 5 to 6. The terminal
  acknowledgement is mandatory in this existing callback and must not be
  moved into a new load/persistence path.
- **Root Scene HCNAMES exit fix is implemented and single-destination
  hardware-confirmed.**
  `menu_switchPage()` now admits a nonzero
  `menu_residentNameDirtySceneMask`, and the Scene type boundary flushes before
  a later Kit-family payload can overwrite the identity block. Scene 015
  Machine -> Scene 15 produced correct Scene/Kit/Instrument rows and
  generation-2 publication. Multi-destination, deferred/toggle exit,
  Scene->KitMrp->Kit hazard, and failed-load preservation still need evidence.
  Do not add another writer.
  The current `-` token remains the valid inherited HCNAMES source token.
- **Bank Load persistence is implemented and hardware-verified (Session 052).**
  `settings.cfg active_bank` follows the committed restore slot and the AutoSave
  Bank scene-present mask equals the effective selected-child union (re-marked
  on a no-op). See `052_SESSION_HANDOFF_LOG.md`. The three refactor targets
  this bullet used to list as deferred in `SCOPING_TARGETS.md` are now all
  **resolved in Session 057**: Bank Save present-mask union (P1) is fixed, the
  once-per-boot settings mark is accepted deliberately (P2, not gated), and
  the boot Kit-quarantine pass (`KQ019KST`) was replaced with lazy
  quarantine-on-failed-load. See `057_SESSION_HANDOFF_LOG.md` §2-§3, §8-§9 —
  note the boot-safety regression test for the quarantine replacement was not
  yet hardware-run.
- The current diagnostic build retains 2,048 trace records (16,384 B) and
  64-record/512-B append batches. This is temporary approved logging-only RAM.
  The Session 052 B-witness diagnosis is complete; restore the 64-record default
  and regenerate the memory manifest on the next rebuild.
- **InstrumentMrp `kit` row fix is implemented and hardware-confirmed.**
  It displays the selected slot's HCNAMES name, uses a Morph-only hidden
  snapshot, and restores only Morphable Morph endpoints. The first pass showed
  the restore committed the staged normal image into the Morph endpoints; the
  repaired build dispatches a Morph-to-Morph staged commit via
  `filesystem_loadedInstrumentWasMorphTemporary()`. Type, Normal image, name,
  and source remain unchanged.
- Root Instrument and InstrumentMrp AutoSave are now accepted hardware work,
  not future whole-object scope. The root fixture proved `J=0x03`, `I=0x07`,
  and 76/76 accepted bytes; the combined generation-3 fixture proved Drum-2
  Morph-only persistence. That historical 64-entry trace could wrap under
  multiple loads; the current temporary diagnostic ring is 2,048 entries, but
  compare durable HCPRMS generations as the final proof in either build.

- Every AutoSave CRC path is now byte-bounded at 128 bytes per filesystem
  tick: initial creation, neither-valid recovery, candidate validation, and
  transformed-copy generation. It uses retained cursors and no delay,
  record-sized buffer, or ordinary-runtime pacing. Never restore the rejected
  generic one-millisecond pacing layer; it made Bank operations extremely slow
  and did not eliminate audio glitches.
- A missing-record setup selector defect was fixed: creation now uses the
  retained A/B target selector, not the CRC accumulator that later reuses the
  same scratch field. The later successful card fixture has both hidden files
  at exactly 34,768 bytes with committed headers.
- One reproduction after that correction timed out during initial B creation:
  A was complete (34,768 bytes) and B stopped at exactly 32,768 bytes. The
  resulting eight-byte `ASENSURE` `/bootlog.bin` proves that the ten-second
  boot deadline fired while ensure was active. It supports, but does not prove,
  an AsyncFATFS cluster-extension/cache/SD transport stall. Several later
  boots succeeded and produced no bootlog, so this is intermittent and not
  resolved. The logging-only 64-byte capsule plus read-only AsyncFATFS/SD
  snapshots are in the build to capture a recurrence; they make no retry,
  allocator, timing, or recovery-policy change.
- The one-second debounced `settings.cfg` writer was re-tested by the user and
  is fine. It is now source-free; make no settings change without new contrary
  evidence.
- Repeated runtime loading of `000 Full` and `013 LoadTst` while playing, plus
  saves over slots 024 and 009, produced no heard audio glitch after the trace
  flush admission guard. This is a useful stability result, not proof that all
  Bank Save/Load or AutoSave interactions are complete.
- Native recursive tree delete was reimplemented on 2026-08-19. Card-fixture
  and hardware acceptance remain pending; do not claim power-loss atomicity or
  reintroduce `old*`/temporary promotion as a workaround.
- **Known bug — runtime Bank Load can switch the playing Scene.** Defer the
  request-time active-Scene preservation change until Bank Load/Save is
  otherwise stable. Boot must retain its saved-default-Scene behavior.
- **Deferred tooling — developer-log converter.** When AutoSave is complete
  and logging formats are stable, add a read-only `/tools/` script that turns
  copied `SD_CARD/` development outputs into dated project-root
  `dev_log_date.txt`. Do not implement it yet; it must target the settled
  `asavetrc.bin` and bootlog/capsule contracts.
- The settled future semantic rule remains: Bank slot/name are payload, not
  record-selection identity. The current live-Bank validator boundary still
  needs reconciliation before Phase-2 whole-object Bank publication.

### Session 059 closeout (2026-08-31)

- Gate A makes short create, LFN create, and same-parent rename reserve a full
  sector-local `0xE5`/`0x00` run. A moved terminator run clears and persists its
  target sector before retiring the old tail. A stopped-playback Bank Save was
  about 10 seconds; Kit Load/Save, Scene Load/Save, and Instrument Save were
  reported working.
- Gate B initializes only the first sector of an appended directory cluster.
  Later sectors remain hidden behind `0x00` until Gate A clears one before
  exposure. A future roughly 20 KiB Scene Pattern uses the regular-file path
  and does not change this directory rule.
- Typed Instrument indexes reject blank/nonprintable/overlength, temporary,
  duplicate, and unsorted rows. Missing, empty, or corrupt metadata rebuilds
  only the selected type; real FAT/SD/read/scan/close/write faults remain
  errors. Menu acknowledges failure and cannot dispatch a deferred payload.
- Healthy Instrument index loads now open `.hcindex` directly without the
  former physical directory prescan. The repair and direct-open path were not
  hardware-retested. `tools/verify_instrument_indexes.py` remains the read-only
  card-copy validator.
- Gate B was subsequently hardware-tested through a failed-child Bank Load,
  full `04x Full` Bank Load, and full save as `Bank/050 Full`. The copied card
  passes Bank-tree, root-index, HCNAMES, settings, and AutoSave CRC/generation
  checks; see `059_SESSION_HANDOFF_LOG.md` section 13. Raw-sector/FAT16/
  fragmented-chain coverage remains unrun. One nonreproducible Kit-list error
  after rapid scrolling remains explicitly deferred.
- That capture has a valid 99-row Drum `.hcindex`, but Snare, Cymbal, and HiHat
  `.hcindex` files remain absent. The Bank path is accepted; the documented
  all-types boot refresh and runtime recovery for those three Instrument types
  remain unverified.
- Final build: `text=385,420`, `data=404`, `bss=96,176`, image 385,840 bytes;
  `afatfs=6,984`, `fs_list_cache_name=9,000`. Both gates add zero retained
  SRAM and use no Pattern-reserved SRAM1 or delay-line-reserved DTCM.
- Durable detail: `knowledge_files/log_archive/059_SESSION_HANDOFF_LOG.md`.
  The four root `S059_*` working documents are superseded and may be deleted.

### Session 060 Phase C source fields (2026-09-03)

- Implemented the zero-growth Scene/Kit/Instrument HCNAMES source projection:
  two little-endian source bytes are routed from the filesystem-owned masked
  source register and marked through the canonical AutoSave dirty mask. Scene
  and Kit parameter starts move from 8 to 10; Instrument normal starts from
  11 to 13; section, payload, mask, and record sizes remain unchanged.
- Whole Instrument, Kit, and Scene-without-Pattern commit markers now include
  source bytes while HCNAMES-owned names remain excluded. Instrument, Kit, and
  Scene Save completions pair source-register staging with one, seven, and
  eight source dirty marks respectively; Bank row zero remains source-free.
- Clean build and image packaging passed: `text=389,036`, `data=400`,
  `bss=96,192`; firmware payload 389,436 bytes and packaged image 389,452
  bytes. The approved RAM impact is about 20 bytes of automatic foreground
  stack only, with no persistent allocation or wire-record growth.
- Card round-trip evidence from the 2026-09-03 Phase C hardware pass is
  recorded in `S060PHASE_C_AUTOSAVE_SOURCE.md`: source bytes verified at the
  wire offsets (Scene +8, Kit +8, Instrument +11), INHERIT/direct tokens
  correct, record sizes unchanged, and an in-place first-drain upgrade
  observed. Phase D's dedicated load/save-to-convergence fixtures (Section 5
  tests 1-4 in `S060PHASE_D_RE_DIRTY.md`) remain pending on hardware.

### Session 060 Phase D re-dirty audit (2026-09-03)

- `S060PHASE_D_RE_DIRTY.md` was re-verified against commit `fe0eedc`: every
  inventory site in its Section 4a exists and is wired as claimed (immediate
  compound load markers, save-completion source marking, refreshed-flag set
  at all load/save terminal boundaries, post-drain
  `objectFullyCaptured()` gating, phases 70-76 `.hcnamtmp` → `.hcnames`
  safe rewrite, and terminal-callback witness clearing).
- No source change was required and none was made; Phase D adds zero
  persistent SRAM and zero wire-format growth. The deferred re-dirty request
  mask from the parent plan is unnecessary because load/save completions
  run while the filesystem facade owns the operation, so their immediate
  atomic dirty marks cannot race an active drain.
- Clean rebuild and image packaging reproduced the Phase C linked sizes
  (`text=389,036`, `data=400`, `bss=96,192`; 389,452-byte image).
- Remaining Phase D work is hardware verification only: Section 5 tests 1-4
  (load-to-convergence, save-to-convergence, load-during-drain race, and
  overlapping loads), each ending with a card-copy check of `.hcnames`
  sources, missing `R` suffixes after convergence, and matching autosave
  source bytes.

### Resolved / Changed in Session 048

- HCNAMES is now the paired name/source authority. The filesystem-owned 258-B
  register replaces the retired 32-B SceneData source array; `settings.cfg`
  writes no Scene-source values and legacy keys are ignored only for migration.
- Root-pool Instrument commits mark type plus owned Normal/Morphable Morph
  bytes immediately; compatible InstrumentMrp commits mark only Morphable
  Morph bytes. Hidden temporary `kit` restore remains non-marking.
- Corrected the direct HCNAMES completion acknowledgement and added the
  approved one-byte queued page exit, so normal hardware exit releases the
  facade for trace and AutoSave scheduling. Added trace stages `I`, `J`, and
  `N` without a logging-off allocation.
- The permanent detail and hardware fixtures are in
  `knowledge_files/log_archive/048_SESSION_HANDOFF_LOG.md`.

### Resolved / Changed in Session 047

- Replaced the remaining unbounded whole-record CRC work with a uniform
  retained, 128-byte-per-tick implementation across all AutoSave setup,
  validation, recovery, and copy paths.
- Fixed the boot ensure A/B filename selector corruption caused by reusing the
  selector scratch field as a CRC accumulator.
- Kept the accepted settings persistence path unchanged after user retest.
- Added a logging-only `ASENSURE` forensic capsule, read-only lower-layer
  snapshot APIs, and `tools/decode_bootlog.py`; both logging-on and logging-off
  builds passed. The normal logging-on image was rebuilt after the change.
- The permanent detail and hardware chronology are in
  `knowledge_files/log_archive/047_SESSION_HANDOFF_LOG.md`.

### Resolved / Changed in Session 046

- Boot Kit quarantine and Bank Load have one ten-second operation deadline plus
  non-rearming substep labels. Ordinary operation failure and timeout reach the
  same bounded best-effort boot failure writer without being converted into
  success.
- Kit validation distinguishes valid content, invalid content, and I/O abort.
  Only proven invalid content may reach quarantine rename; an interrupted FAT
  operation leaves the Kit intact and aborts index publication.
- Boot consumes failed index/load request acceptance and terminal status before
  Menu acknowledgement or empty-library fallback can hide the failure.
- HCNAMES direct access is case-insensitive and every create-capable path uses
  one shared root absence proof. Duplicate or failed proof never mutates the
  card. Hardware retest did not reproduce the dual HCNAMES entry.
- The `DEV_MODE_LOGGING`-only 520-byte AutoSave lifecycle trace is present at
  `c9807fa`. Its autonomous append has a completion callback that returns the
  facade to `IDLE`; failed appends retain pending records.
- User-testable scalar owner coverage is accepted complete: Scene,
  Kit/Instrument, and Scene-owned MIDI channel/note. No separate Bank scalar UI
  test exists. Whole-object publication remained future work at the Session
  046 close; Session 048 later accepted the Instrument boundaries.
- The deletion-safe Session 046 record is
  `knowledge_files/log_archive/046_SESSION_HANDOFF_LOG.md`.

### Resolved / Changed in Session 045

- Accepted foundation at `326a8a1`, retained by `c9807fa`: exact 34,768-byte hidden A/B records; one
  canonical 3,856-byte dirty mask; bounded asynchronous mask/value capture
  with atomic take/re-dirty semantics; typed Bank/Scene/Kit/Instrument scalar
  markers; and the then-33-line v1 settings schema.
- Pattern and Effect persistence, applying a hidden winner at boot, and
  crash-recoverable promotion into explicit library files remain unimplemented.
  `AUTOSAVE.md` is authoritative; stale historical plans and the old AutoSave
  section in `FILESYSTEM_SPEC.md` are not.

### Resolved / Changed in Session 044
- Cold boot initializes SceneData before tagged runtime construction. DRM's
  zero enum value can no longer turn raw Scene BSS into six accidental Drum
  owners.
- Boot and deferred Scene activation share the same clear -> all incoming
  type/image apply -> all-source LFO/velocity rebind lifecycle. Hardware
  confirmed the initial active Scene's Snare controls and both reported LFO
  destinations now work without a Scene/target round trip.
- Top-level Load:Bank chains Bank index completion into its child preview and
  gates input until the destination mask is resident, fixing unchanged-slot
  zero-mask rejection.
- Root Scene/Bank Load finishes HCNAMES, preserves the completed Bank result,
  applies DSP, and reloads the unchanged root index read-only last. Save alone
  owns physical numbered-root scan/index rebuild.
- Accepted OK/OW operations display `...`, suppress cursors, and reset to the
  bracketed type row only after their real terminal work.
- Four SD boot-pacing holds are present, but the motivating intermittent hang
  was seen once and is not reproducible. Do not claim it resolved.
- Session 044's final static allocation was 12,280 B DTCM and 66,776 B SRAM1.
  That SRAM1 value is historical. The current Session 051 logging-on linked
  build reports `bss=95,176 B`; no RAM owner or allocation moved, and
  `SRAM_MANIFEST.md` remains the binding allocation record.

### Resolved / Changed in Session 042
- `/.hcnames` is the authoritative fixed-row name register. Runtime identity is
  one Bank + one Scene + one Kit + six Instrument names (81 bytes), never
  per-Scene arrays or retained keys.
- Session 042 separated the 9,000-byte browser cache from the 2,048-byte
  non-Pattern validation stage; do not recombine them. Session 058 later gave
  HCNAMES its own 1,161-byte mirror, so HCNAMES no longer borrows the browser
  cache.
- Scene/Kit/Instrument share that stage; Scene Pattern reads directly into the
  final Scene after settings+Kit commit. Pattern load is intentionally
  non-atomic for now.
- Bank Load preserves every unselected Scene and HCNAMES block. Session 058
  replaced the former one-child rescans with one bounded 16-name capture.
- Instrument Load's original `kit` source is an on-card `.hctmp.<ext>` plus one
  nine-byte label, not a second staged Instrument. A prior two-image preview
  design was removed.
- Five asyncfatfs handles are linked (+376 bytes versus three), but directory
  handles must still be closed after `chdir`.
- Final rapid-backspin boundary handling was build-verified but was not
  hardware-retested before Session 042 closed.

### Resolved / Changed in Session 034
- Instrument Load is complete for current instrument types. The root pool holds
  converted `.drm`, `.snr`, `.cym`, and `.hat` files; browser order is
  per-type alphanumeric with a one-based display number saturated at 999.
  New-format root Instrument Save was added in Session 036.
- The visible Load/Save menu no longer exposes Pattern, MorphKit, Perform, and
  All. The promoted top-level type cycler is Kit/Scene/Bank; Instrument Load is
  entered by VOICE press on Load, and Instrument Save
  is entered by VOICE press on Save. Kit/Instrument Load scenes use
  `pat_sceneHasActiveSteps()` for LED base state. Kit Load permits a
  zero-or-more Scene toggle mask; Instrument Load/Save selects exactly one
  Scene. A selected Scene blinks.
- `kit_t` no longer retains Instrument display names or file stems.
  Provenance/display identity comes from HCNAMES and the one 72-byte
  filesystem identity block.
- The direct Instrument loader caused the observed Voice 2 long-decay/locked
  parameter condition: loading any Drum file into that slot stayed bad, while
  loading a Kit repaired it. The new staged transaction prevents live Scene
  mutation during I/O and clears/rebuilds all modulation/runtime ownership on
  commit. It also prevents rapid encoder, destination, Scene, mode, or preview
  events from interleaving with a load.
- Still unresolved: Pattern `Step` stores canonical 16-bit destinations but
  `AutomationNode` is still legacy byte CC/CC2 based; migrate before enabling
  descriptor step automation. Direct descriptor LFO writes were repaired in
  Session 035 through InstrumentManager descriptor-domain adapters, but normal
  modulation-node reset/original-value paths still enumerate fixed global
  pools rather than InstrumentManager's dynamic pools.
- Converter follow-up: regenerate instrument values by live descriptor key/ID,
  not a hardcoded descriptor order. The Session 034 investigation found 42
  mismatched finite LFO target pairs among 55 active first pairs in the
  generated pool; this did not cause the long-decay fault, but the files need
  conversion-path repair before trusting their assignment data.

### Resolved / Changed in Session 030
- Phase 2 root filesystem spec began in `FILESYSTEM_SPEC.md`; after the
  Session 032 documentation consolidation and Session 033 closeout, the
  authoritative filesystem and instrument-file spec is
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
  The old root pointer file was deleted in Session 033.
  Target root layout is `Bank`, `Scene`, `Kit`,
  `Pattern`, `Sample`, `Wavetable`, `Effect`, `Instrument`, plus future root
  `settings.cfg`. At Session 030 this covered root `Kit/` and root
  `Instrument/` load work; later Sessions 035-038 added new-format Kit Save,
  root Instrument Save, KitMrp Save, and InstrumentMrp Save.
- `SD_CARD/Kit/` generated from legacy `Pxxx.SND` files. Folders use preferred
  `NNN Name` convention, for example `004 Moch to`; `_` remains accepted for
  compatibility.
- New `Core/Hardware/SD/storageTypes.c/h` owns Phase 2 kit text schemas,
  numbered-folder parsing, kitset/instrument validation, and instrument
  descriptor-key parsing. After Session 032, instrument values write Scene
  descriptor storage rather than a `ParameterArray` key map. All functions in
  this layer use the `storage_` prefix and must remain independent of
  `asyncfatfs`.
- Normal kit load now reads root `Kit/` directories: scan cache ->
  `kitset.kcg` -> six instrument files. Normal Kit Save writes the same
  directory shape; KitMrp Save writes the same directory shape with current
  interpolated Morph values duplicated into both endpoint sections. Legacy
  `FS_FILE_MORPH` / MorphKit compatibility still uses legacy `.SND`.
- `asyncfatfs` now sets opened handle type from the FAT directory entry so
  opened directories can be entered/scanned reliably.
- Kit scan has a FAT short-alias fallback for space-named folders when LFN
  reconstruction is unavailable.
- MIDI note/channel settings were intentionally removed from `kitset.kcg`; they
  are future scene settings. Directory kit loads leave `PAR_MIDI_NOTE1..7`
  unchanged for now.
- Final hardware status: menu/init directory kit load worked after discovery
  fixes; final short-alias fallback still needs hardware smoke-test.

### Resolved / Changed in Session 032
- Descriptor-backed VOICE pages are populated from
  `Core/DSP/Instruments/*/*Parameters.c` layouts. `menuPages.h` remains the
  static-page table, but instrument cells resolve through InstrumentManager and
  the active Scene slot.
- Instrument files load into Scene descriptor images. `kitset.kcg` owns slot
  type/file/audio route; `[params]` and `[morph]` instrument sections own
  descriptor values.
- `InstrumentManager` owns descriptor registry lookups, dynamic menu layouts,
  and descriptor-to-DSP runtime apply. Direct runtime writes use descriptor
  binds; special non-offset parameters use explicit shaper/setter handling.
- `instrument_decimation` and `velo_mod_amount` are `ROW_NOBIND_IMAGE`
  parameters: morphable, modulatable, and automatable image values without a
  direct struct-offset bind.
- Parser keys allow at least 32 bytes. Session 034 canonicalized HiHat decay
  to `amp_envelope_decay` / `amp_envelope_decay_choke` and retains legacy
  closed/open aliases for compatibility.
- Session 033 resolved the known descriptor Morph and LFO/velocity runtime
  modulation gaps. The remaining descriptor target runtime gap is step
  automation.

### Resolved / Changed in Session 029
- Pattern storage ownership moved further into `Core/Bank/Scene/Pattern/PatternData.c/h`. Live code no longer uses `seq_patternSet`, `seq_tmpPattern`, `seq_selectedStep`, `SEQ_DEFAULT_NOTE`, or `SEQ_NEXT_RANDOM*`.
- PatternData now owns staged pattern commit, playback-safe step reads, effective-length reads, live note record mutation, live erase mutation, and legacy file shuffle import through `pat_commitStagedPattern()`, `pat_readStep()`, `pat_getStepProbability()`, `pat_getStepNote()`, `pat_getStepVolume()`, `pat_getEffectiveTrackLength()`, `pat_recordNote()`, `pat_eraseMainStepSubSteps()`, and `pat_setAllShuffle()`.
- Sequencer still owns timing, transport, quantization, runtime step indices, MIDI output, random next-pattern resolution, and recording/erase gates, but storage mutation/readbacks now go through `pat_*` helpers.
- `Core/Preset/` moved to `Core/Bank/Scene/Preset/`. `Makefile` uses `-ICore/Bank/Scene/Preset` and sources `Core/Bank/Scene/Preset/presetManager.c` / `Core/Bank/Scene/Preset/ParameterArray.c`. `main.c` includes `ParameterArray.h` directly for `parameterArray_init()`.
- Staging/global audits written: active-pattern load staging stays until the 17th Scene/background-bank-load design; new `filesystem.c` scratch/snapshot buffers and load/save menu polling are intentionally left alone; globals should eventually become canonical scene-level, bank-level, and system-level settings structs.

### Resolved / Changed in Session 028
- `Core/MIDI/frontPanelParser.c/h` removed from live code. Former protocol opcodes are now direct owner calls documented in `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`.
- New `Core/Bank/Scene/Pattern/PatternData.c/h` owns Pattern storage/edit APIs; Euklid and SOM moved from `Core/Sequencer/` to `Core/Bank/Scene/Pattern/`.
- Sequencer LED feedback no longer uses parser callbacks. Sequencer writes `seq_ledState`; foreground `led_processSeqLedState()` performs LED/Menu/Button-aware rendering in ledHandler.
- Sound parameter application is direct through Preset (`preset_applySoundParameter`, `preset_applyVelocityModTarget`, `preset_applyLfoModTarget`); MIDI channel/routing/filter config is direct through MidiParser setters.
- Pattern file serialization reaches PatternData through pointer helpers and uses `PATTERNDATA_STAGING_PATTERN` for active-pattern load staging.

### Resolved / Changed in Session 027
- Runtime kit/all/performance load completion no longer applies all six sound modulation-routing voices in one foreground burst. After audio starts, `menu_startSoundApply()` arms the chunked apply and `menu_tickSoundApply()` lets `preset_tickDrumsetApply()` apply one voice per foreground pass before operation-specific UI/global/pattern follow-up runs. `AUDIO_DMA_FRAMES` remains 96; direct 64-frame latency testing is deferred until this path is hardware-tested.

### Resolved / Changed in Session 023
- CPU scheduling refactor completed: TIM6 keeps only 1ms counters and a foreground service due flag; shift-register exchange, PB jack detect, encoder-button debounce, and endless-pot scanning run from `timebase_serviceFrontPanel()` at about 500Hz.
- LCD servicing reduced to 5kHz at priority 7. `lcd_waitForIdle()` is used only in modal sample/loop load screens after audio has been suspended, so status text fully renders before flash work blocks.
- Slider taper mapping now uses a 4096-entry boot LUT derived from `SLIDER_LOG_TAPER_DB`, removing repeated foreground `powf()` calls.
- Oscillator interpolation is bounded by `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current test build and limited to audible oscillator waveform targets; user-sample interpolation remains enabled.
- Oscillator-only ITCM is enabled. Filter/distortion ITCM annotations remain present but disabled through `ENABLE_EFFECT_INITCM_CODE=0` after hardware CPU monitor testing looked worse.
- `Load:[Samples ]` now uses the sample/loop flow documented in Sample Flash Loading.
- Main encoder direction reversals clear only sub-detent residue, avoiding the persistent two-click feel after fast direction changes.

### Resolved in Session 022
- ~~24-bit DMA frame LSW forced to 0x0000~~ — **RESOLVED**. `pack_half()` now emits true signed-24 payload in both halfwords. `sample_mx_t` (int32_t) carries signed-24 audio; int16 voice values enter mixer as `int16<<8` via `bufferTool_convertInt16ToSampleMix()`.

### Pending from Session 022 (not resolved)
- **dth global menu option not yet wired** — complete plan in DITHER_AUDIT.md Steps 1–8. Prerequisite infrastructure (`sample_mx_t` path) is now in place. Next step: PAR_16BIT_DITHER enum + menu text + menuPages.h slot + dtype/default + mixer gate.
- **Voice sync-block and distortion widening deferred** — DrumVoice/Snare/CymbalVoice/HiHat/distortion remain int16_t* for now. Pinned plan in DITHER_AUDIT.md. Revisit when voice/distortion gain staging can be validated after widening.
- **Hardware listening test for 24-bit path needed** — loudness regression was fixed analytically (removed extra >>8); hardware confirmation still required.

### Resolved in Session 16
- ~~preset_morph() stub / burst risk~~ — **RESOLVED**. Morph now uses original interpolation and a one-parameter-per-main-loop worker.
- ~~preset_sendModTarget() fall-through bug~~ — **RESOLVED**. `CC_VELO_TARGET` now breaks before `CC_LFO_TARGET`.
- ~~Morph return-to-zero could leave late DSP parameters stale~~ — **RESOLVED**. No skip cache; request/pass generations guarantee a full final pass at latest morph value.
- ~~RV1-RV4 page-change and idle ghost edits~~ — **RESOLVED** by raw A/B snapshots, page-change `snapshotAll()`, post-delta rebaseline, and pre-delta false-start cancellation.
- ~~Global BPM drift from broad endless-pot double-speed rule~~ — **RESOLVED**. Double angular speed now applies only to `PAR_MORPH`.

### Resolved in Session 17
- ~~Preset save/load pattern/all/performance stubs~~ — **RESOLVED**. Kit, MorphKit, Pattern, Performance, All, and Globals now route through typed async filesystem operations.
- ~~Morph-kit load/save not connected~~ — **RESOLVED**. Morph load writes `parameters2[]`; morph save writes interpolated values with mod-target exceptions.
- ~~Slow envelopes from widened DSP block~~ — **RESOLVED**. `OUTPUT_DMA_SIZE` matches the effective LXR-master block size of 32 frames; `AUDIO_DMA_FRAMES` remains 96 for hardware DMA.
- ~~Sequencer/mainboard tick drift from reference~~ — **RESOLVED**. `systick_ticks` is 4kHz again; TIM6 `time_sysTick` remains 1kHz for UI/service timing.
- ~~Screensaver display glitches~~ — **RESOLVED** with explicit LCD off/on phases and clear-on-exit.
- ~~Load-page empty slots and fast-spin name/display races~~ — **RESOLVED** by typed async request tagging and whole-frame LCD queue preflight.

### Resolved in Session 019
- ~~USART3 RX not configured~~ — **RESOLVED**. Interrupt-driven RX/TX, dual TX FIFO, non-blocking.
- ~~DIN TX polled/blocking~~ — **RESOLVED**. TXE interrupt drains realtime FIFO before normal FIFO.
- ~~USB MIDI RX/TX not serviced~~ — **RESOLVED**. midi_service() in main loop.
- ~~BAR1/BAR2 DSP race~~ — **RESOLVED**. Voice trigger pending ring + audio-boundary drain.
- ~~BAR1/BAR2 not through MIDI recording path~~ — **RESOLVED**. midiParser_playVoiceMidiNote() with assigned/default note numbers.
- ~~PD3 EXTI3 diagnostic in production~~ — **RESOLVED**. Real PC13/PD4/PD5 backend.
- ~~EXTI4/EXTI9_5 pointing at Default_Handler~~ — **RESOLVED**. IRQ10/IRQ23 wired.
- ~~seq_tick() in main loop~~ — **RESOLVED**. TIM3 4kHz ISR owns it.
- ~~MIDI realtime bytes processed without timestamp~~ — **RESOLVED**. USART3 ISR timestamps every byte; realtime bytes route to the MidiRealtime ring.
- ~~PAR_BPM=0 external sync toggle~~ — **RESOLVED**. PAR_EXT_SYNC global (SyncInpt).
- ~~CC1 MORPH double-fire~~ — **RESOLVED**. CC1→MORPH in incoming channel path only.
- ~~OUTPUT_DMA_SIZE=16 (2× EG/LFO rate)~~ — **RESOLVED**. Corrected to 32.

### Resolved in Session 020
- ~~Slider-to-parameter mapping not designed~~ — **RESOLVED**. RV5-RV10 now feed dedicated `slider_vol[]` gains and are applied as independent post-voice multipliers in `mixer.c` (not as base `voice.vol` writes).
- ~~Slider zipper from block-edge gain jumps~~ — **MITIGATED**. Per-block interpolation (`last_gain -> current_gain`) added in mixer; log taper mapping added in adc path (`SLIDER_LOG_TAPER_DB`).

### Resolved in Session 18
- ~~SampleMemory.c no-op stub~~ — **RESOLVED**. SampleMemory now validates/caches flash metadata and display names for up to 120 installed samples.
- ~~Sample flash region only proposed~~ — **RESOLVED**. Linker caps app flash at `0x0807FFFF`; sectors 6-11 are reserved for sample storage.
- ~~Load:[Samples] no-op~~ — **RESOLVED**. `/samples` full reinstall and `/loops` append-loop installer are wired through the Load page.
- ~~Audio resume after modal sample writes failed~~ — **RESOLVED**. `audioCodec_suspend()`/`audioCodec_resume()` now fully stop/reset/restart DMA, I2S, and PLLI2S.

### Resolved in Refactor Session
- ~~audioTest.c, sineBufferTest.c, duplicate copy files~~ — **RESOLVED** by consolidation into `AudioCodecManager.c`.
- ~~Scattered DMA ISRs~~ — **RESOLVED**. DMA1 Stream 4 and Stream 7 ISRs now live in `AudioCodecManager.c`.
- ~~audioCodec_packHalf() public wrapper~~ — **RESOLVED**. Removed; ISRs call `pack_audio_half()` directly.

### Resolved in Session 15
- ~~Sequencer step buttons unreliable / no sequenced voices~~ — **RESOLVED**. `BUTTON_TIMEOUT` corrected to 500ms and missing `seq_init()` added at boot.
- ~~Sequencer tempo 4x slow~~ — **RESOLVED** for gross BPM in Session 15; Session 17 restored `systick_ticks` to the original 4kHz LXR mainboard tick while UI millisecond timing stays on `time_sysTick`.
- ~~PATGEN/Euklid writes steps but LEDs do not update~~ — **RESOLVED**. Visible generated main-step LEDs refresh after steps/rotation changes.
- ~~PATGEN/Euklid generated steps front-stacked~~ — **RESOLVED**. `__CLZ` shim now emits ARM `clz`; `__CLZ(0)` returns 32 as original Euklid expects.
- ~~copyClearTools.c frontPanel calls commented out~~ — **RESOLVED**. Direct `seq_clear*` / `seq_copy*` calls wired.
- Euclid and SOM parser backends are wired. Trigger backend was still stubbed at Session 15 and was later resolved in Session 019.

### Resolved in Session 14
- ~~buttonHandler/menu/LED audit connectivity gaps~~ — **RESOLVED** for audit-defined paths. Connections documented in `BUTTONHANDLER_MENU_AUDIT_RESULTS.md` and `LED_AUDIT_SUMMARY.md`.

### Resolved in Session 13
- ~~PAR_VOICE_LFO1-6 not reaching modulation targets during kit load~~ — **RESOLVED**. Preset modulation-target helpers call `modNode_setDestination()` directly.

### Resolved in Session 12
- ~~SD card operations block the main loop~~ — **RESOLVED** by asyncfatfs.
- ~~HiHat VLA stack corruption~~ — **RESOLVED**. Static arrays.
- ~~preset_morph() index 127 memory corruption~~ — **RESOLVED**. Index 127 skipped.

### High Priority
1. ~~No hi-hat/wrong tagged Instrument at startup~~ — **RESOLVED in Session
   044** by SceneData-before-DSP initialization plus the complete post-audio
   Scene clear/image/rebind lifecycle.
2. ~~Trigger backend still stubbed~~ — **RESOLVED in Session 019 Phase 5**. PC13 CLK OUT, PD4 CLK IN, PD5 RST IN, and trigger PPQ menu wiring are implemented; hardware bench validation still needed.
3. ~~BAR1/BAR2 race condition~~ — **RESOLVED in Session 019 Phase 7**. All voice triggers deferred through voiceControl pending ring; drained at audio boundary.
4. **Long sample playback is not 32-bit clean yet** — `SampleInfo.size` is `uint32_t`, but `Oscillator.c` still derives the address index with the legacy `phase >> 17` path.
5. **MIDI/clock/jack bring-up only** — All Session 019 features are build-verified only. Hardware bench testing required before claiming correctness.
6. **Load/save button display glitch fix NOT YET APPLIED** — Fix is a two-line reorder in `menu_switchPage()` `case LOAD_PAGE:`: update `menu_activePage` BEFORE calling `menu_resetSaveParameters()`. Full details in `LOAD_SAVE_GLITCH_ASSESSMENT.md`. Do not restructure further.

### Medium Priority
5. **ResonantFilter.c double literals** — lines 141, 167: `0.5*in` and `1.0 - f_lp2` cause software double emulation in SVF_calcBlockZDF hot loop. Change to `0.5f` and `1.0f`.
6. **DrumVoice.c VLA** — line 228: `int16_t modBuf[size]` still present, should be static.
7. **BufferTools.c float division** — line 120: `i/(size-1.f)` per sample in hot loop.
8. ~~TIM2 not initialised~~ — **RESOLVED in Session 019**. TIM2 is the shared 1 MHz free-running timestamp source. Do NOT reset on pulse. Do NOT use for SD ISR (TIM5 reserved for that).
9. ~~MidiParser RX not connected~~ — **RESOLVED in Session 019**. Full MIDI in/out including clock, sync, CC1→MORPH, and BAR1/BAR2 MIDI path implemented. Hardware validation pending.
14. Final RV1-RV4 endless-pot noise fix needs long idle hardware soak, especially on global BPM page.
15. Synced LFO tempo still needs audit/fix: current code path has used a hardcoded 130 BPM instead of `seq_getBpm()`.
16. **PAR_EXT_SYNC / PAR_FETCH slot conflict with LXR037** — `PAR_EXT_SYNC` (MIDI auto-sync) occupies the parameter array slot where `PAR_FETCH` lived in LXR037. `.SND`, `.ALL`, `.PRF` files saved on LXR-02 and loaded on an LXR037 (or vice versa) may have that byte misinterpreted. No fix needed now; resolve before any cross-system file interchange.

### Lower Priority
16. Sample loader naming/display/order needs hardware soak after Session 18 LFN changes. Sort is whole-filename lexicographic with ASCII case folded; display names preserve case.
17. SELECT_1 LED dark at boot — enhanced-firmware-only fix
18. ~~Sequencer needs dedicated TIM ISR~~ — **RESOLVED in Session 019**. TIM3 owns sequencer timing.

---

## Failed Approaches — Do Not Retry

- **Blind one-millisecond filesystem runtime pacing**: slowed Bank operations
  to tens of seconds/about a minute and delayed rather than eliminated two
  audible glitches. Bound the actual CRC/work loop by bytes across retained
  filesystem passes; do not sleep around unbounded work. Session 046, rolled
  back.
- **Bundled `/devlog.bin` consolidation and duplicate-safe append rewrite**:
  the hardware test timed out during boot, produced no DEVLOG, and left
  `/.hcprms2` at 32,768 bytes. Do not reapply the patch wholesale or allow
  logger recovery to hide the original operation failure. Session 046, rolled
  back.
- **Treating failed open as proof of absence**: AsyncFATFS append/write modes
  include CREATE, so error fall-through can create duplicate same-visible-name
  FAT entries. Complete and close a case-insensitive scan, classify
  zero/one/multiple matches, and preserve every scan/open error.
- **TIM7 idle gating**: wakeup race → freeze. Session 6.
- **Broad repaint coalescing**: freezes and lag. Session 6.
- **`val >>= 2` in `encode_read4()`**: asymmetric floor divide. Session 7.
- **`last = (new+3)&3` encoder seed**: wrong direction click. Session 4/5.
- **`RNG_CR |= RNG_CR_RNGEN`**: RMW on unpowered peripheral → stall. Session 8.
- **`ready_count` shared RMW in audio queue**: non-atomic. Session 8.
- **VLAs in DSP voice files**: silent stack overflow. Session 8.
- **DSP render in DMA ISR**: hard 2.18ms ceiling, no graceful degradation, system locks on overrun. Session 10.
- **Blocking SD calls in main loop**: f_open blocks 1–50ms, kills audio render. Session 10.
- **SD chunking at FatFS-call granularity in main loop**: f_open is still one blocking call, insufficient. Session 10.
- **Forking ChaN FatFS for async conversion**: 20+ functions need state machine conversion, 28 yield points, 6 levels of call nesting — unacceptable risk and complexity. Session 11.
- **NB_FatFS library**: C++ with heap allocation (new/delete), lambdas, global voidPtr for context, no _FS_TINY support, no FAT16, 9500 lines, designed for hardware DMA callbacks. Session 11.
- **preset_morph() sending index 127 through the old MIDI_CC packing**: `(127+1) & 0x7f = 0` → `paramNr = 0 - 1 = 65535` → wild write to `midiParser_originalCcValues[65536]`. Memory corruption. Must skip index 127. Session 12.
- **VLAs in HiHat_calcSyncBlock**: `int16_t mod1[size], mod2[size]` — silent stack corruption with -O2. Must use static arrays. Session 12.
- **SD_sendCommand() from sdcard_lxr02.c**: Deasserts CS after R1 response — breaks data transfer. Must use send_cmd_keep_cs() instead. Session 12.
- **Two back-to-back filesystem requests**: storage FSM is single-operation. Must wait for completion or ack. Session 12/17.
- **Old CC_VELO_TARGET/CC_LFO_TARGET bridge during kit load**: routing modulation-target changes through the removed parser path did not properly establish modulation targets in the merged single-MCU context. Use Preset modulation-target helpers instead. Session 13.
- **Morph skip cache**: can skip necessary DSP restores after returning to morph 0. Morph must send a complete final pass at the latest value. Session 16.
- **Endless-pot double speed for all `DTYPE_0B255`**: made global BPM drift more visible. Only `PAR_MORPH` gets double angular speed. Session 16.
- **Bare `n == 0` as asyncfatfs EOF test**: `afatfs_fread()` returns 0 while the SD buffer is still being populated — it is not an EOF signal. All EOF checks must use `n == 0 && afatfs_feof(op_file)`. Session 026.

---

## Toolchain

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -flto
```

DSP source files are compiled by the Makefile's more-specific `-Ofast` rule.

Image format: `[8B "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
Boot: hold main encoder button while powering on.
