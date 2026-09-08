# AutoSave Test Cases and Load/Save Revisions

**Status (2026-09-08): deferred test and revision backlog.** Implement Pattern
data storage first. Load/Save and AutoSave are usable enough for current work;
this document does not authorize an intervening refactor unless a new defect is
severe enough to block Pattern work.

This is the sole forward-work list for interactions among AutoSave,
`.hcnames`, `settings.cfg`, the Load/Save UI, and the filesystem browser
caches. It consolidates the still-open Load/Save items formerly scattered
through `SCOPING_TARGETS.md`. Historical explanations and resolved work remain
in that document and the session logs.

No source change accompanies this document.

---

## 1. Authority and terminology to preserve

Tests must distinguish these layers. Treating them as one cache has hidden
several earlier faults.

| Layer | Role | Important boundary |
|---|---|---|
| Resident SRAM | Live Scene, Kit, Instrument, source, and refreshed state | What sound generation and a subsequent save use now |
| `.hcindex` and its RAM list cache | Disposable browser listing for one active directory/domain | May be cleared and regenerated as the selected Load/Save type changes |
| HCNAMES mirror | Fixed resident identity/source/refreshed image used to serialize `.hcnames` | Must survive `.hcindex` disposal and asynchronous writes |
| `.hcnames` | Durable resident identity, provenance, and refreshed-register authority | Must agree with resident state at defined persistence checkpoints |
| `.hcprms1` / `.hcprms2` | CRC-protected AutoSave A/B snapshots | Embedded name bytes are deliberately not the live identity authority |
| `settings.cfg` | Durable global settings, including active Bank identity | Uses `settings.tmp` safe-write/promotion |
| Explicit Bank/Scene/Kit/Instrument files | User library data | Previewing must not mutate resident state or AutoSave dirtiness |

The settings filename is `settings.cfg`. Temporary promotion files that need
failure tests include `settings.tmp` and `.hcnamtmp`; nested Instrument editing
also uses the reversible `.hctmp.<ext>` UI scratch path.

### Required observation points

Every test below should inspect more than the final LCD result:

1. Capture the starting card image or hashes, including hidden files.
2. Observe the UI and resident selection immediately after the public Load or
   Save completion.
3. Observe again after changing the selected browser item/type.
4. Observe after leaving the Load/Save page and after all queued filesystem
   work drains.
5. Reboot from the resulting card and verify the same identity, source,
   refreshed state, parameters, and active Bank/Scene.
6. When failure injection is relevant, repeat at each file-create, sync,
   replace, delete, and close boundary.

Do not infer ordering from FAT timestamps: the device has no authoritative RTC.
Use file bytes, record generations/CRCs, trace records, and explicit UI/runtime
state instead.

---

## 2. User-visible revisions found in the initial pass

These are the first targets for the later Load/Save pass. The notes below are
an initial source-level assessment, not a completed root-cause investigation.

### LSR-01 — checkpoint Kit and Instrument HCNAMES before cache handoff

**Observed behavior:** Kit and Instrument loads update the resident name
scratch state, but the durable `.hcnames` rewrite is normally deferred until
the resident-name scratch session ends at a Load/Save family or page exit.
Changing browser type can dispose the `.hcindex` list cache without creating
that durability checkpoint.

**Initial source indication:** `menu_refreshResidentNameScratchKit()` and
`menu_refreshResidentNameScratchInstrument()` accumulate a Scene dirty mask.
`menu_endResidentNameScratchSession()` is the path that requests the physical
resident-name update. The successful Load path already updates live resident
identity/provenance; the delayed part is publication of that state to the
physical `.hcnames` file.

**Revision contract:** when the selected top-level Load item changes away from
Kit or Instrument, or another transition is about to dispose/reassign the
active `.hcindex` domain, snapshot and queue any pending HCNAMES publication.
The UI transition must not wait for the card write. Coalesce repeated changes
against the dedicated HCNAMES mirror and retain the dirty Scene mask until a
successful promote. Normal and Morph browsers must preserve their existing
identity rules: Morph preview/load must not turn Morph endpoint data into a new
Normal Instrument identity.

**Explicit tests:**

- Load one Kit, switch immediately to Instrument, then power-cycle without
  leaving Load/Save; repeat Instrument-to-Kit. Cover every transition that
  reassigns the browser domain, including Normal/Morph variants and each typed
  Instrument directory.
- Load several Kits/Instruments across several resident Scenes, switching
  type after each. Confirm every changed HCNAMES row and refreshed bit survives.
- Switch type while an earlier HCNAMES write is active. The latest mirror wins,
  no dirty row is lost, and there is no torn `.hcnames`/`.hcnamtmp` pair.
- Exercise Kit, Instrument, and Instrument Morph entry/exit. Confirm Morph-only
  work neither overwrites the Normal name/source nor causes a false identity
  refresh.
- Repeat with AutoSave OFF and ON; explicit Load must maintain HCNAMES in both
  modes.

### LSR-02 — remove the visible exit hang from Kit/Instrument Load

**Observed behavior:** on leaving Kit or Instrument Load, the old menu may
remain painted over the destination page or a Voice-mode modal for a noticeable
period.

**Initial source indication:** page switching is deferred while
`menu_storageBusy` is set, through `menu_pendingPageSwitch`. The exit path can
end the resident-name scratch session and start HCNAMES persistence; the final
repaint is then also conditional on storage becoming idle. This can explain an
old Load screen remaining visible, but timing traces are still required before
declaring HCNAMES serialization the sole cause.

**Revision contract:** accepting Exit must tear down the old browser and paint
the destination page/modal on the normal UI refresh cadence. Snapshot and
queue HCNAMES work independently; let it finish in the background. A pending
filesystem operation may delay another filesystem command, but must not retain
ownership of obsolete LCD contents or lose the requested page transition.

**Explicit tests:**

- Exit an unchanged and a dirty Kit browser to Voice mode; repeat for every
  Instrument family and Morph browser.
- Exit during `.hcindex` generation, immediately after a load, and during an
  active HCNAMES promote.
- Measure button-to-destination-paint latency and background-drain time
  separately. The former should not scale with the latter.
- Enter another non-filesystem modal before the background write completes;
  confirm no late callback restores the obsolete Load page.

### LSR-03 — blank means “not ready”; `Empty` means “proven absent”

**Observed behavior:** some Load/Save browsers, notably Bank, can temporarily
display `Empty` for an occupied slot until the scroll list refreshes.

**Initial source indication:** several filesystem name accessors synthesize
`Empty` for invalid, blank, or not-yet-ready lookup states. This collapses
“the current index has proved that no object exists here” and “the current
index cannot answer yet” into one UI value. Some Menu handoff paths already
blank the name, but the contract is not uniform.

**Revision contract:** update the slot number immediately. Display a blank name
while the correct list/index is unavailable or changing domain. Display
`Empty` only after the current, valid slot-ordered index proves absence. Never
show a previous directory's cached name during the blank interval.

**Explicit tests:**

- Cold-enter and rapidly scroll every Bank, Scene, Kit, and Instrument Load and
  Save browser while its index is missing, stale, being generated, or corrupt.
- Alternate occupied and empty slots and switch browser type before generation
  completes. Record every displayed intermediate state.
- Confirm OK/Load/Save is disabled or safely bound to the displayed coordinate
  until the current index has established whether the slot exists.
- Inject index-open/generation failure. The UI stays blank or reports a real
  error; it must not fabricate `Empty` as a successful result.

### LSR-04 — standardize asynchronous selection update/disposal

**Observed behavior:** Bank Load scrolls more slowly between occupied slots
than between `Empty` slots.

**Initial source indication:** the obvious asymmetry is not merely SEQ LED
rendering. `menu_requestBankLoadPreview()` can finish an absent slot locally,
whereas an occupied slot requests a child-Scene scan and asserts
`menu_storageBusy`. Its completion already checks that the requested slot is
still current before applying the result; that stale-result guard should
become the general browser model.

**Revision contract:** all scroll browsers should use one latest-selection
pipeline:

1. Publish the new slot number synchronously.
2. Publish its name when the correct index can answer; keep it blank before
   then.
3. Queue optional data preview asynchronously: Scene LEDs for Bank, parameters
   for Kit/Instrument, and the equivalent data for future browser types.
4. Queue any post-cache/persistence work that is still necessary.

Each request carries a selection coordinate or generation. A front-end item
change makes prior queued results obsolete; callbacks discard those results
instead of repainting or committing them. Coalesce toward the latest requested
item. Scrolling should not be input-gated by an occupied-slot preview.

**Explicit tests:**

- Compare high-rate encoder scrolling across alternating occupied/empty Bank
  slots and across Kit/Instrument slots. Slot-number response must be uniform.
- Reverse direction and change browser type while each stage is outstanding.
  No late name, LEDs, parameters, error, or OK-enabled state may belong to the
  prior coordinate.
- Verify final Bank Scene LEDs and Kit/Instrument parameters after scrolling
  stops, including partial Banks and corrupt preview children.
- Trace directory-scan time and LED-update time independently before choosing
  the implementation budget.

---

## 3. AutoSave, HCNAMES, and `settings.cfg` interaction matrix

### AS-BOOT — reader selection and fallback

Test each case with AutoSave enabled and disabled where meaningful:

- Neither record exists; only `.hcprms1` exists; only `.hcprms2` exists; both
  are valid with different generations; equal generations; generation wrap.
- Truncated record, bad magic/version/length, bad CRC, and a valid peer beside
  each failure. A bad record must never displace a valid peer.
- `settings.cfg` missing, malformed, or recovered from a valid/invalid
  `settings.tmp` while AutoSave records are independently valid or invalid.
- Clean winner: HCNAMES identity/source/refreshed state already matches the
  selected snapshot.
- Mixed winner: some resident rows match HCNAMES and others require snapshot
  restoration. Rebooting the resulting `SD_CARD_READER_9` state is the
  outstanding focused fixture for this path; the existing Reader 9 capture
  accepted the preceding all-refreshed HCNAMES-authoritative path, not this
  matching-winner path.
- All rows refreshed and authoritative from HCNAMES; no unnecessary snapshot
  value replacement.
- Identity cannot be resolved because `.hcnames`, its temp, and explicit
  fallback data are missing or corrupt. Boot must fail/fallback by the stated
  policy, never silently pair parameters with the wrong identity.
- Stale embedded name bytes in an otherwise valid HCPR record. HCNAMES remains
  the identity authority and the record remains acceptable.

For every case, assert the selected A/B generation, active Bank/Scene,
identity, values, source flags, refreshed mask, whether fallback ran, and the
post-boot on-card result.

### AS-WRITE — A/B atomicity and live changes

- Cut power or inject failure at open, write, sync, close, old-record removal,
  and promote boundaries. At least one prior valid generation must remain
  bootable.
- Dirty a field again while its snapshot/write is active. The completed record
  may contain the captured value, but the newer mutation must remain dirty and
  reach a later generation.
- Exercise dirty-mask-only, values-only, source-only, and combined changes for
  Scene, Kit, and every Instrument type.
- Verify the HCPR mask and values converge after Load, Save, partial Bank Load,
  partial Bank Save, and multiple overlapping resident loads.
- Confirm no reader/writer assumes that HCPR embedded names become fresh after
  a mid-session load.
- Retain the bounded-record regression: an exact-size valid record is accepted;
  a truncated or over/under-length record fails cleanly without a boot stall.

### AS-ENABLE — OFF/ON and Load/Save exclusion

- Boot with AutoSave OFF: no HCPR read/write should occur, while explicit
  Load/Save must still update resident state and HCNAMES as required.
- Turn AutoSave ON after OFF-mode edits. The first snapshot must capture the
  complete current resident state, not only edits made after the toggle.
- Turn AutoSave OFF during an active transaction. The admitted transaction may
  finish its atomic boundary; no new one starts afterward.
- Enter Load/Save while an AutoSave transaction is active, and make dirty state
  while Load/Save suppresses writer admission. UI entry remains responsive and
  all dirtiness drains after exclusion ends.
- Page-exit expedite must be verified on hardware: leaving Load/Save releases
  suppression and schedules pending work without a lost or duplicate write.
- Previewing or merely selecting any browser row must never create resident or
  AutoSave dirtiness. Only a successfully committed public Load/Save operation
  may do so.

### ID-COHERENCE — Bank identity across three files

- After Bank Load, Bank Save/rename, AutoSave write, and reboot, compare
  HCNAMES row 0, the active-Bank fields in `settings.cfg`, and the winning HCPR
  Bank header. Define and assert which operation publishes each field.
- Reproduce the historical mismatch fixture where settings/HCPR selected one
  Bank and HCNAMES row 0 named another. Capture traces before assigning the
  failure to publication, later reversion, or fixture history.
- Test a failure/power cut between each pair of publications. Recovery must
  choose one coherent Bank identity; it must not combine a Bank number from
  one generation with a name from another.
- Runtime Bank Load during playback must preserve the request-time active Scene
  instead of switching the playing Scene. Boot Bank restoration must continue
  to use the Bank's saved/default Scene as specified.

### NAME-PUBLISH — HCNAMES safe-write and refreshed lifecycle

- Load then allow AutoSave to drain: refreshed/source transitions and physical
  HCNAMES publication must agree after reboot.
- Save then drain: source changes must not leave a false refreshed bit.
- Load again while the first drain or HCNAMES promote is active; repeat across
  several Scenes and Instrument slots. Coalescing must preserve every row.
- Fail each `.hcnamtmp` create/write/sync/promote boundary with and without an
  older valid `.hcnames`. Recovery must never accept a partial file.
- Confirm a physical HCNAMES write occurs at every documented checkpoint and
  does not occur merely because a preview row was visited.
- Verify the dedicated HCNAMES mirror cannot be invalidated by clearing or
  reusing the shared `.hcindex` RAM cache. Audit any remaining AutoSave writer
  use of that shared cache and serialize, snapshot, or reject a destructive
  clear while a borrower is active.

### SETTINGS — safe-write and scheduling

- Test valid `settings.cfg`, valid `settings.tmp` with missing/corrupt target,
  invalid temp with valid target, two valid candidates, missing both, short
  writes, and failed promotion.
- Change active Bank, active Scene, AutoSave enable, and unrelated settings in
  close succession. Every eventual `settings.cfg` must be internally valid;
  a later change must not be cleared by completion of an older snapshot.
- Run settings, HCNAMES, AutoSave, and diagnostic-trace writes against the
  Load/Save exclusion boundary. Only one filesystem owner may advance at a
  time, but UI navigation must remain live.
- Confirm a boot fallback does not create an unnecessary settings rewrite loop.

---

## 4. Load/Save correctness and fault cases moved from `SCOPING_TARGETS.md`

These are either unverified landed fixes or deferred hardening work. They are
not all claims of a current user-visible bug.

### LS-DATA-01 — corrupt and partial object policy

- Boot with a Bank whose selected mask includes a corrupt embedded Kit. Boot
  must remain safe; validation is lazy at explicit Load.
- Directly Load a corrupt Kit: fail that Load and apply the specified quarantine
  behavior. Root Scene Load must fail if its required Kit fails. Bank Load must
  keep valid Scenes and report/omit failed children rather than fail the entire
  Bank.
- Repeat for missing/truncated `sceneset.scg`, Kit members, Instrument files,
  Pattern data, and `effects.fx`. Record the intended tolerance for each file;
  do not silently turn malformed content into a successful empty library.

### LS-SAVE-01 — overwrite and partial-save safety

- Exercise the empty-Scene and empty-Bank overwrite guards on hardware using
  every confirmation/cancel path.
- Save a partial resident Bank over an existing fuller card Bank. Only selected
  Scenes are replaced; unselected card children survive.
- Rename while saving to the same Bank slot and confirm all intended children
  survive under the new identity.
- Interrupt Scene Save at every phase. The current delete-first, multi-file
  sequence can leave a structurally partial Scene, especially before
  `effects.fx`. Rank the later repair among earlier FX write, failed-save
  cleanup, tolerant load of optional FX, or a real atomic directory commit.

### LS-DELETE-01 — recursive-delete acceptance matrix

The product path is fixed and hardware-used, but the low-level AsyncFATFS
matrix has not been run deliberately. Cover malformed/cross-sector LFN runs,
cyclic or broken parent layout, injected FAT/cache errors, exhausted handle
pool, duplicate slot folders, and nested mixed files/directories. A failure
must be bounded and must not delete outside the resolved target.

### LS-STATE-01 — callback, timeout, and error ownership

- Decide and test abort safety for `BkLd`/`ScLd` stall detectors. A timeout must
  not let a later callback mutate state for a newer request. The write-side
  redirect hazard that caused Save detectors to become trace-only must not be
  generalized to Load without evidence, but must not be ignored either.
- Capture the phase at the point of Scene Load failure. The current common
  terminal error (`ScnL48`) is insufficient to distinguish which child failed.
- Re-test the historical `B012S09I` boot Bank/embedded-Instrument stall with
  the existing per-Instrument timing breadcrumbs. Measure the real slow phase
  before changing time budgets.
- Add top-level Load/Save entry/refusal trace coverage. Instrument tracing is
  gated by the nested browser and root Scene entry lacks an equivalent event,
  which has previously made “not observable” look like “not executed.”

### LS-NAME-01 — names, indices, and temporary Instrument state

- Define and test one policy for object names over eight characters, including
  collision/duplicate handling after canonicalization. Do not implement blind
  truncation without a deterministic conflict rule.
- Test missing, stale, corrupt, and failed-to-open `.hcindex` in every browser,
  including the root Instrument directory's spelling/case. Specifically audit
  the still-case-sensitive `/Instrument/` scan open and the boot caller that
  discards `filesystem_createBootIndexBlocking()`'s return value. Boot-time
  index errors must become observable without making a repairable cache fatal.
- Re-test Kit Save materialization after multiple source/provenance states and
  verify the Save browser does not regress to the historical false `Empty`
  result.
- Hardware-test Instrument Morph's `kit` row: it shows the current authoritative
  Instrument name, restores only the selected slot's Morphable endpoints, and
  never overwrites type, Normal image, name, or source.
- Preserve `.hctmp.<ext>` reversibility through enter, preview, cancel, commit,
  page exit, card error, and power loss. Scratch cleanup must not become a
  resident identity update by itself.

---

## 5. Pattern-storage boundary for the later pass

Pattern persistence is intentionally outside the current AutoSave record
payload and is the next feature priority. Do not harden today's Scene Pattern
sequence in a way that locks in the old ownership model.

After the Pattern storage format and ownership model land, add these cases to
the Load/Save pass:

- Round-trip Pattern data through root Scene Save/Load, partial Bank Save/Load,
  and boot restoration without changing unrelated resident Patterns.
- Power-cut and corrupt-file tests at every Pattern write/read boundary.
- Align active, shown, queued, and Scene-owned Pattern indices. Preserve PERF
  queued-Scene semantics while removing accidental multiple authorities.
- Guard one-time Scene commit initialization. `pat_initPatternSet()` must not
  repeat merely because a later asynchronous `chdir` phase retries, and no
  failure may zero the last committed Pattern set.
- Decide explicitly whether the new Pattern payload belongs in a revised HCPR
  schema or a separate store. Do not assume the current record's snapshot
  reserve or version can absorb it without a size, latency, and atomicity
  calculation.
- Re-run every mixed AutoSave/HCNAMES/explicit-Scene fixture in Section 3 after
  Pattern data becomes part of the resident restoration contract.

---

## 6. Suggested execution order after Pattern storage

1. Add the missing top-level trace/latency observations and build reproducible
   card fixtures; avoid behavior changes in this step.
2. Fix the four user-visible browser items in Section 2 under one asynchronous
   selection-coordinate model.
3. Run the non-destructive coherence matrix for AutoSave, HCNAMES, and
   `settings.cfg`, including OFF/ON and overlapping loads.
4. Run power-cut/failure-injection tests for the three safe-write families and
   explicit Scene/Bank saves.
5. Address only the failures the matrix exposes, then rerun the complete boot,
   browse, Load/Save, drain, and reboot sequence.

### Resolved history deliberately not reopened

The following old notes were not migrated as live defects: the 32 KiB HCPR
truncation freeze, Bank present-mask union, `settings.cfg` safe-write itself,
the boot AutoSave reader implementation, dedicated HCNAMES mirror allocation,
and the Session 061 mixed-winner/source-type fix. Their failure modes remain
valuable regression fixtures above, but their implemented corrections should
not be redesigned without new contrary evidence.
