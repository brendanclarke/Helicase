# Load / Save / AutoSave Element Test Report

## Scope

Four element types were exercised with the same sequence: delete the card's
working files (`/.hcnames`, `/.hcprms*`, `bootlog.bin`, `asavetrc.bin`), boot,
press Play, load an element, edit some parameters, save the element to a new
slot, then power off and capture the card.

Snapshots:

- `SD_INST/` - Instrument load/save
- `SD_KIT/`  - Kit load/save
- `SD_SCENE/`- Scene load/save
- `SD_BANK/` - Bank load/save (froze entering Bank Save)
- `SD_FREEZE/`- boot freeze after the Scene test

The primary check is whether root `/.hcnames` reports the provenance source of
the **saved** element, not the loaded one.

## Bottom line

1. **FAIL - HCNAMES source provenance is not updated on Save.** The source
   token keeps the loaded slot instead of the saved slot. This is the headline
   defect. Clearest case: Scene saved to slot `031` but its row still says
   source `004` (the loaded slot).
2. **Kit Save did not materialize a library Kit.** No new/renamed directory
   appeared under `/Kit/`, yet the resident Kit name changed to `SoyEarsv` and
   its source stayed `013` (loaded).
3. **Instrument Save wrote the pool file but did not publish provenance.**
   `Instrument/Drum/barfd2sv.drm` was created, but no Instrument row gained the
   `@` (direct pool) source; the resident row stayed `docwird1 -`.
4. **AutoSave records still carry an empty Bank section.** This is the already
   documented boot Bank Load tracking defect (deferred in SCOPING_TARGETS.md),
   not a new regression.
5. **Two hangs need separate investigation:** Bank Save entry froze (SD_BANK),
   and a boot froze with `.hcprms2` truncated at 32 KiB (SD_FREEZE).

## HCNAMES provenance observations

The relevant `.hcnames` rows with non-default source:

| Snapshot | Row | Name | Source | Expected |
|---|---|---|---|---|
| SD_INST | 0 | Full | 008 | 008 (Bank) |
| SD_KIT | 23 (Kit 06) | SoyEarsv | 013 | saved slot |
| SD_SCENE | 7 (Scene 06) | Moch tsv | 004 | 031 (saved) |
| SD_BANK | 0 | LoadTst | 012 | 012 (no save occurred) |
| SD_FREEZE | 0 | Full | 008 | 008 |

### Scene (SD_SCENE) - clearest failure

`Scene/031 Moch tsv` was newly created, so the Scene Save to slot 031 succeeded
at the filesystem level. The resident Scene row (row 7, Scene index 6) correctly
shows the saved name `Moch tsv`, but its source token is `004` - the loaded
slot `Moch to`. The source should be `031` (the saved slot).

### Kit (SD_KIT)

The resident Kit row (row 23, Scene index 6) shows `SoyEarsv` with source
`013`. `013` is the loaded kit `SoyEared`. No `SoyEarsv` directory exists under
`/Kit/`, so the Kit Save did not create/rename a library Kit. Two things are
wrong here: the source stayed at the loaded slot, and the saved Kit never
appeared in the library.

### Instrument (SD_INST)

`Instrument/Drum/barfd2sv.drm` was newly created, so the pool-file write
worked. However, every Instrument row in `.hcnames` is still `-` (inherit),
including row 69 (`docwird1 -`) which is Scene index 6 / instrument slot 0 and
is where the trace shows the instrument load committed. A root-pool instrument
should be published with source `@` (direct). This points at the deferred
instrument HCNAMES publication not having flushed, or the save path not
staging the source.

### Bank (SD_BANK)

Row 0 is `LoadTst` with source `012`. This is the loaded Bank (settings
`active_bank=12`), and no new Bank directory exists, consistent with the user
freezing before the save. The source here is correct only because the save
never ran.

## AutoSave and trace health

The AutoSave write machinery itself is working:

- A/B ping-pong is functioning (winner flips A -> B -> A across generations).
- `V` (validated), `M` (mask merged), `C` (captured), `P` (published), `T`
  (terminal) records are present and generations increment.
- Runtime parameter edits (`D` records) are being captured and drained.
- The re-dirty/merge behavior is visible (mask merge `dirty=1` then a later
  capture).

But `tools/verify_bank_autosave.py` still FAILs on every snapshot with the same
signature:

```
present_mask=0x0000 (expected 0xffff)
active_scene=0    (expected 6)
voice_edit_mask=0x0000 (expected 0x0040)
Scene 06 payloads missing / mismatch
```

This is the boot Bank Load capture defect: the Bank Load runs before
`autosave_setMutationTrackingEnabled(1)`, so its Bank-section markers are all
rejected. It is already recorded as deferred in SCOPING_TARGETS.md and should
not be conflated with the source-provenance bug above.

## Bank Save entry freeze (SD_BANK)

Evidence:

- `/Bank/` contains no new, temporary, or partially-written Bank directory.
- `/.hcnames` row 0 is still the loaded Bank (`LoadTst 012`).
- The trace ends with `W` (writer suppressed by the Load/Save page guard;
  `canonical mask dirty=1`), i.e. the AutoSave writer was intentionally held
  while the user was in the Load/Save page.

This is consistent with a hang at/inside Bank Save page entry, before any Bank
payload work. It needs a menu-path investigation (the Bank Save page entry and
the new direct delete/recreate preflight), not just the AutoSave layer.

## Boot freeze (SD_FREEZE)

Evidence:

- `.hcprms2` is 32,768 bytes instead of 34,768 bytes (truncated).
- `.hcprms1` is the full 34,768 bytes.
- No `bootlog.bin` and no `asavetrc.bin` were produced.

The truncated inactive record is the signature of a power loss/freeze during
an AutoSave drain: the drain was writing `.hcprms2` and stopped exactly at the
32 KiB boundary. On the next boot the partial record fails validation and
`.hcprms1` remains the winner, so it is recoverable - but the drain itself
froze and needs a look at the copy/CRC phase and any 32 KiB cluster-boundary
behaviour.

## Recommendations

1. **Fix HCNAMES source publication on Save (primary).** The Save completion
   paths currently refresh the resident name cache but do not call
   `filesystem_setResidentSource(row, op_slot)` (or `@` for instruments). The
   load paths set the source; the save paths must set it to the saved slot.
2. **Investigate Kit Save not materializing.** A Kit Save to a new slot should
   produce a new `/Kit/NNN Name` directory; it did not in this test.
3. **Investigate the Bank Save entry freeze** in the menu/page code.
4. **Investigate the mid-drain boot freeze** (truncated `.hcprms2` at 32 KiB).
5. Leave the empty AutoSave Bank section as the deferred reader-milestone item
already recorded in SCOPING_TARGETS.md.

## Follow-up: overwrite-function test (SD_FREEZE2, SD_OVERWRITE_TEST)

This pass exercises the overwrite path directly. Build/config:
`DEV_MODE_DIAGNOSTIC=0`, `DEV_MODE_LOGGING=1`, `BOOT_FILESYSTEM_TIMEOUT_MS=20000`.

### Boot timeout (SD_FREEZE2)

- `bootlog.bin` = `B012S09I` (8 bytes): Bank 012 / Scene 09 / Instrument
  stage. The boot Bank Load is still timing out, now with a 20-second budget
  (was 10 s). This is the same boot Bank Load / embedded-instrument stall, just
  reaching Scene 09 before the deadline. It is not the AutoSave or HCNAMES
  layer.
- `asavetrc.bin` exists and shows the normal boot Bank Load `I`/`L` marks with
  `TRACKING_ENABLED=0`, confirming the boot-capture gap is still present.

### Second-boot overwrite pass (SD_OVERWRITE_TEST)

Boot succeeded on retry (`bootlog.bin` still `B012S09I`, stale from the
timeout above). Three defects reported.

#### 1. No Kits listed in the Kit Save menu

- `/Kit/.hcindex` is present and populated (Barf, Slak, Hard, ...), and the
  `/Kit/` directory still has 39 kits.
- So the Kit library is intact but the Kit Save menu's shared name cache was
  empty when entered. This points at a cache-tag/cache-load issue after the
  prior save operations (the save paths borrow and re-tag the single name
  cache), not a Kit-library scan failure.
- Needs a menu-path look: entering Kit Save must reload `/Kit/.hcindex` when the
  active cache tag is Scene/HCNAMES.

#### 2. Scene overwrite reported `ScnS05`

- `ScnS05` = `FS_INTERNAL_OP_SAVE_SCENE`, phase 5, which is exactly
  `filesystem_deleteSlotDirectory_tick()` - the singular overwrite resolver
  added by the recursive-delete reimplementation.
- The card shows Scene slot 019 was actually replaced: `019 Organity` ->
  `019 Rollin`, and `019 Rollin/` is a complete tree (sceneset.scg, Kit Rollin,
  six instruments, pattern.pat, effects.fx).
- So the physical overwrite completed, but the save still surfaced an error at
  the resolver phase. This means either the resolver deleted the old tree and
  then returned a spurious error, or a first attempt errored after partial work
  and a retry completed. Either way the overwrite resolver is not giving a
  clean, consistent result.

#### 3. Instrument overwrite uncertain

- `Instrument/Drum` (and Snare/Cymbal/HiHat) show no new file between
  SD_FREEZE2 and SD_OVERWRITE_TEST. An in-place overwrite would not change the
  filename, so a directory listing cannot confirm it.
- The instrument overwrite cannot be confirmed from the snapshot alone; content
  comparison of the target instrument file is needed.

### Cross-cutting

- The trace still shows the boot Bank Load running with `TRACKING_ENABLED=0`
  and `published=0` for every Scene/instrument, so the empty AutoSave Bank
  section (already deferred) is unchanged.
- `settings.cfg` is `active_bank=12` in both snapshots, matching the loaded
  `LoadTst` Bank.

### Updated recommendations

1. **Scene overwrite resolver (`ScnS05`).** Focus on
   `filesystem_deleteSlotDirectory_tick()`: it is deleting the target but
   reporting ERROR, or erroring after partial work. This is the direct
   overwrite-correctness path this whole effort targets.
2. **Kit Save menu empty cache.** Ensure the Kit Save entry reloads the Kit
   index/cache after save operations retag the shared name cache.
3. **Boot Bank Load still times out at 20 s** (`B012S09I`). The Kit-quarantine
   removal in KIT_PARSE_BOOTLOCK_RESOLVE.md does not cover this; this is the
   Bank Load embedded-instrument budget, separate from the boot quarantine.
4. Instrument overwrite needs a content-level confirmation test.
