# Phase D: Re-Dirty Mechanism — Expanded Implementation Schedule

Source: `S060_AUTOSAVE_UPDATE_READER_PREP.md` Sections 6, 10, and 12 (Phase D).

## What Phase D delivers

When a sub-object is loaded or saved mid-session, the autosave record must
converge to the new state. Phase D ensures the autosave writer re-captures
the affected bytes and, once fully captured, clears the refreshed flag and
safe-rewrites `.hcnames` so the boot reader can distinguish between
"autosave is authoritative" and "reload from library."

---

## 1. Parent plan specification (Section 10)

The parent plan describes Phase D as six steps:

1. Add a per-Scene re-dirty request mask (`uint16_t`) to the writer workspace.
2. At drain entry (phase 56), OR in all mutation bits for re-dirty-flagged
   Scenes before the normal mask scan.
3. Set re-dirty flags from load/save completions.
4. Save completions: re-dirty only name+source bits (10 B per object: 8 name +
   2 source) for the saved object and all contained sub-objects.
5. Load completions: re-dirty all bits for the loaded object and all contained
   sub-objects.
6. Clear re-dirty flags after they are consumed by the drain.

SRAM cost estimate: ~4–6 bytes for a uint16_t scene mask plus Kit/Instrument
flags.

---

## 2. Current state: what's already implemented (code-site audit)

Phases B2 and C have already built all of the functional machinery that
Phase D was designed to provide. This section maps each Phase D requirement
to existing code with exact file, line, and function references.

### 2a. Load completions re-dirty all mutation bits

**Parent plan requirement:** "Request the autosave writer to re-dirty all
mutation bits for the loaded object at the next drain entry."

**Already implemented by:** Compound dirty markers called from Preset layer
load callbacks. The markers fire **immediately** from the load completion
callback, not deferred to the next drain entry.

#### 2a.1 Atomic core: `autosave_markPayloadOffsetDirty()`

- **File:** `Autosave.c:133–157`
- **What:** The sole scalar dirty-production funnel. Checks
  `autosave_mutation_tracking_enabled` and `payload_offset` bounds, then
  calls `autosave_maskByteOr()` which uses `autosave_irqSave()` /
  `autosave_irqRestore()` to atomically OR one bit into
  `autosave_dirty_mask[]`. Returns 1 if the bit was accepted.
- **Why:** Every compound marker ultimately funnels through this single
  point, guaranteeing that foreground and interrupt-side mutations coalesce
  safely into the single canonical 3,856-byte SRAM mask.
- **Affiliates:** All `autosave_mark*Dirty()` functions;
  `autosave_maskBitTake()` (Autosave.c:1629) is the drain-side consumer that
  atomically reads and clears the same bits.

#### 2a.2 Compound markers: implementation map

| Marker | File:Line | Coverage | Called from |
|--------|-----------|----------|------------|
| `autosave_markWholeInstrumentDirty(scene, slot)` | Autosave.c:1303–1390 | 3 type bytes + 2 source bytes + every live Normal descriptor + every Morphable Morph descriptor; HCNAMES-owned name (bytes 3..10) deliberately excluded | `presetManager.c:1883` (root Instrument Load completion) |
| `autosave_markInstrumentNormalDirty(scene, slot)` | Autosave.c:1244–1270 | Every live Normal descriptor index | Future copy boundary (reserved) |
| `autosave_markInstrumentMorphDirty(scene, slot)` | Autosave.c:1272–1301 | Every Morphable Morph descriptor | `presetManager.c:1676` (InstrumentMrp), `presetManager.c:1772` (KitMrp Morph copy) |
| `autosave_markKitDirty(scene)` | Autosave.c:1392–1429 | Kit params (2 bytes) + Kit source (2 bytes via `markSourceDirty` at line 1416) + 6 × `markWholeInstrumentDirty` (line 1419) | `presetManager.c:277` (normal Kit Load completion) |
| `autosave_markEffectDirty(scene)` | Autosave.c:1431–1448 | Zero parameter bits today (AUTOSAVE_EFFECT_PARAM_COUNT == 0); future Effect ownership extends here | Called from `markSceneWithoutPatternDirty` (Autosave.c:1475) |
| `autosave_markSceneWithoutPatternDirty(scene)` | Autosave.c:1450–1486 | Scene params (40 bytes) + Scene source (line 1473) + Effect stub (line 1475) + Kit compound (line 1476, which cascades to 6 Instruments) | `presetManager.c:490` (root Scene Load), `presetManager.c:541` (Bank-child Scene Load) |
| `autosave_markSceneWithPatternDirty(scene)` | Autosave.c:1488–1500 | Delegates to `markSceneWithoutPatternDirty` (line 1498); Pattern stub reserved at line 1499 | Reserved for future Pattern persistence |
| `autosave_markResidentBankDirty()` | Autosave.c:1502–1525 | All 5 Bank fields (line 1518–1519) + every present Scene via `markSceneWithoutPatternDirty` (line 1523) | `filesystem.c:7039` (mismatched winner), `filesystem.c:22789` (runtime setup), `filesystem.c:24029` (re-enable) |

#### 2a.3 Source dispatch in `autosave_markSourceDirty()`

- **File:** `Autosave.c:1195–1242`
- **What:** Converts an HCNAMES row coordinate (0..128) into the matching
  2-byte source field's payload-relative base, then calls
  `autosave_markPayloadOffsetDirty()` for both bytes. Bank row 0 is a no-op
  (the Bank section has no source field). Scene source sits at
  `AUTOSAVE_SCENE_SOURCE_OFFSET` (byte 8). Kit source sits at
  `AUTOSAVE_KIT_OFFSET + AUTOSAVE_KIT_SOURCE_OFFSET` (byte 648). Instrument
  source sits at kit-instruments-base + slot×192 +
  `AUTOSAVE_INSTRUMENT_SOURCE_OFFSET` (byte 11 within each Instrument).
- **Why:** Source provenance is filesystem-owned, but its two serialized
  bytes must join the same canonical mask as owner-driven parameter mutations.
  The HCNAMES row coordinate ensures the dirty marker and
  `autosave_objectFullyCaptured()` share the exact same wire-scope mapping.
- **Affiliates:** `filesystem_setResidentSource()` (filesystem.c:5293),
  `autosave_getSourceByte()` (Autosave.c:222–235),
  `autosave_objectFullyCaptured()` (Autosave.c:1571–1627).

#### 2a.4 Why immediate works (no deferred mask needed)

The parent plan preferred deferred re-dirtying ("the load callback is not
the right place to mutate the mask wholesale") out of concern that immediate
mask mutation could interfere with an active drain. In practice, the atomic
per-bit operations are safe:

- If the drain's phase-56 scanner (filesystem.c:7179) has not yet scanned
  past the bit, it captures the new value in the current cycle.
- If the drain has already scanned past the bit, the bit stays set and is
  captured in the next cycle.
- In both cases, `autosave_objectFullyCaptured()` returns false until all
  bits are clean, so the refreshed flag stays set until full capture.

The immediate approach is **strictly better** than deferred: it can capture
some or all changes in the current cycle, whereas a deferred mask OR at
drain entry always adds one cycle of latency.

---

### 2b. Save completions re-dirty source bits

**Parent plan requirement:** "Re-dirty only the name and source mutation bits."

**Already implemented by:** Phase C's `autosave_markSourceDirty()` calls at
all three save completion families in `filesystem.c`. Each call is paired
with the corresponding `filesystem_setResidentSource()` that changes the
source value.

#### 2b.1 Instrument Save completion

- **File:** `filesystem.c:14095–14113`
- **Source staging:** `filesystem_setResidentSource()` at line 14095 sets
  `FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT` (0x1FFD) on the saved Instrument's
  HCNAMES row.
- **Dirty marking:** `autosave_markSourceDirty()` at line 14103 marks the
  2-byte source field for the same Instrument row.
- **Refreshed flag:** `filesystem_setResidentRefreshed()` at line 14110 sets
  bit 13 on the same Instrument row.
- **Scope:** 1 Instrument row.

#### 2b.2 Kit Save completion

- **File:** `filesystem.c:15799–15831`
- **Source staging:** `filesystem_setResidentSource()` at line 15799 sets
  `op_slot` on the Kit row; loop at lines 15810–15813 sets
  `FS_RESIDENT_SOURCE_INHERIT` (0x1FFF) on 6 Instrument rows.
- **Dirty marking:** `autosave_markSourceDirty()` at line 15805 (Kit) and
  loop at lines 15815 (6 Instruments).
- **Refreshed flag:** `filesystem_setResidentRefreshed()` at line 15823
  (Kit) and loop at lines 15828 (6 Instruments).
- **Scope:** Kit + 6 Instruments = 7 rows.

#### 2b.3 Scene Save completion

- **File:** `filesystem.c:17383–17413`
- **Source staging:** `filesystem_setResidentSource()` at line 17383 (Scene),
  line 17390 (Kit → INHERIT), loop at lines 17400–17402 (6 Instruments →
  INHERIT).
- **Dirty marking:** `autosave_markSourceDirty()` at line 17388 (Scene),
  line 17393 (Kit), loop at line 17404 (6 Instruments).
- **Refreshed flag:** `filesystem_setResidentSceneRefreshed()` at line 17413
  covers Scene + Kit + 6 Instruments in one call (8 rows total).
- **Scope:** Scene + Kit + 6 Instruments = 8 rows.

#### 2b.4 Bank Save completion (row zero only)

- **File:** `filesystem.c:16625–16628`
- **Source staging:** `filesystem_setResidentSource()` at line 16625 sets
  `op_slot` on `FS_IDENTITY_BANK_ROW` (row 0).
- **Refreshed flag:** `filesystem_setResidentRefreshed()` at line 16628.
- **Note:** No `autosave_markSourceDirty()` call here because the Bank
  section has no source field in the autosave payload.
  `autosave_markSourceDirty()` explicitly returns immediately for
  `AUTOSAVE_HCNAMES_BANK_ROW` (Autosave.c:1200–1201).

**Name bytes are deliberately excluded** — see Section 3 below.

---

### 2c. Refreshed flag set at load/save completions

**Already implemented by:** Phase B2's `filesystem_setResidentRefreshed()`
(filesystem.c:5545–5555) and `filesystem_setResidentSceneRefreshed()`
(filesystem.c:5558–5573).

#### 2c.1 `filesystem_setResidentRefreshed()` implementation

- **File:** `filesystem.c:5545–5555`
- **What:** Sets bit 13 (`FS_RESIDENT_SOURCE_REFRESHED_FLAG`, 0x2000) in
  `fs_resident_source[row]`. Invalid rows (≥ `FS_RESIDENT_NAMES_ROW_COUNT`,
  which is 129) are silently ignored.
- **Why:** RAM-only witness that a library Load/Save replaced this object's
  resident payload. The bit reaches the card through the next post-drain
  HCNAMES safe-rewrite.
- **Affiliates:** `filesystem_clearResidentRefreshedCaptured()` (the only
  code that clears bit 13); `filesystem_formatResidentNameLine()` (reads the
  bit to decide the `R` suffix).

#### 2c.2 `filesystem_setResidentSceneRefreshed()` implementation

- **File:** `filesystem.c:5558–5573`
- **What:** Compound helper that calls `setResidentRefreshed()` on the Scene
  row (line 5569), Kit row (line 5570), and all 6 Instrument rows (loop at
  lines 5571–5573).
- **Why:** A Scene Load/Save replaces the entire hierarchy; each sub-object's
  refreshed flag must be set independently because autosave clears them
  per-object only when that object's own payload interval is clean.

#### 2c.3 Complete call site table

| Completion | File:Line | Function called | Rows refreshed |
|-----------|----------|----------------|---------------|
| Scene Load | filesystem.c:11875 | `setResidentSceneRefreshed(scene_index)` | Scene + Kit + 6 Instruments (8 rows) |
| Kit Load | filesystem.c:10376–10384 | `setResidentRefreshed()` × 7 | Kit + 6 Instruments (7 rows) |
| Instrument Load | filesystem.c:13701–13704 | `setResidentRefreshed()` × 1 | 1 Instrument |
| Bank Load (empty) | filesystem.c:12659 | `setResidentRefreshed(FS_IDENTITY_BANK_ROW)` | Bank row 0 |
| Bank Load (children) | filesystem.c:12835 | `setResidentRefreshed(FS_IDENTITY_BANK_ROW)` | Bank row 0 |
| Instrument Save | filesystem.c:14110 | `setResidentRefreshed()` × 1 | 1 Instrument |
| Kit Save | filesystem.c:15823–15830 | `setResidentRefreshed()` × 7 | Kit + 6 Instruments |
| Scene Save | filesystem.c:17413 | `setResidentSceneRefreshed(scene)` | Scene + Kit + 6 Instruments (8 rows) |
| Bank Save | filesystem.c:16628 | `setResidentRefreshed(FS_IDENTITY_BANK_ROW)` | Bank row 0 |

---

### 2d. Post-drain: check objectFullyCaptured, clear refreshed, rewrite HCNAMES

**Already implemented by:** Phase B2's post-commit pipeline. This subsection
traces the complete data path with exact code locations.

#### 2d.1 `autosave_objectFullyCaptured()` — the cleanliness query

- **File:** `Autosave.c:1571��1627`
- **What:** Maps one HCNAMES row coordinate (0..128) to its complete wire
  interval in `autosave_dirty_mask[]`, then scans every bit in that interval.
  Returns 1 only when every byte in the object's reserved payload scope is
  clean (no dirty bit set).
- **Scope mapping:**
  - Bank (row 0): payload bytes 0..127 (128 bytes)
  - Scene (rows 1..16): 1,920 bytes each (full Scene section including
    Effect reserve and Kit)
  - Kit (rows 17..32): 1,280 bytes each (Kit header + 6 Instruments)
  - Instrument (rows 33..128): 192 bytes each
- **Why:** The intervals intentionally include reserved bytes (Effect padding,
  Kit tail). Current markers never set reserved cells, so this test is
  conservative — a future owner can only make it stricter.
- **Affiliates:** Called from `filesystem_autosaveDrainHasRefreshWork()`
  (filesystem.c:6717), `filesystem_clearResidentRefreshedCaptured()`
  (filesystem.c:6739), and `filesystem_formatResidentNameLine()`
  (filesystem.c:20420).

#### 2d.2 `filesystem_autosaveDrainHasRefreshWork()` — the decision gate

- **File:** `filesystem.c:6699–6722`
- **What:** Scans all 129 `fs_resident_source[]` rows for any row where both
  (a) bit 13 is set and (b) `autosave_objectFullyCaptured(row)` returns true.
  Returns 1 if any such row exists.
- **Guard:** If `hcnames_mirror_valid != FS_HCNAMES_MIRROR_VALID` (line
  6713), returns 0 immediately. A stale or empty mirror must never be
  serialized to card.
- **Why:** This is the decision point between "drain is complete, finish
  normally" and "drain is complete, but loaded/saved objects need their
  refreshed flags persisted to `.hcnames`."
- **Affiliates:** Called from `filesystem_autosaveDrainAfterCommit()` (line
  6757).

#### 2d.3 `filesystem_autosaveDrainAfterCommit()` — the entry point

- **File:** `filesystem.c:6746–6765`
- **What:** If `autosaveDrainHasRefreshWork()` returns false, sets
  `op_file_version = 0` and calls `filesystem_finish(FS_STATUS_DONE)` (normal
  drain completion). Otherwise, sets `op_file_version = 1` (a transient
  formatter mode flag), invalidates the mirror
  (`hcnames_mirror_valid = FS_HCNAMES_MIRROR_INVALID`, line 6763), and enters
  phase 70.
- **Called from two drain completion points:**
  1. `filesystem.c:7170` — after mask merge when no dirty bits exist (a load
     set the refreshed flag but no edits are pending).
  2. `filesystem.c:7634` — after the autosave target commit (the normal
     post-drain convergence path).
- **Why:** The two call sites ensure HCNAMES convergence runs regardless of
  whether the drain had any parameter mutations to capture.

#### 2d.4 Phases 70–76: HCNAMES safe-rewrite state machine

| Phase | File:Line | Operation |
|-------|-----------|-----------|
| 70 | filesystem.c:7637–7650 | Open `.hcnamtmp` for writing via `afatfs_fopen_lfn()` |
| 71 | filesystem.c:7652–7663 | Wait for open callback; initialize row cursor (`op_item_offset = 0`) |
| 72 | filesystem.c:7665–7699 | Stream all 129 rows via `filesystem_formatResidentNameLine()`. The formatter at line 20418–20421 suppresses the R suffix for rows where `current_op == FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN && op_file_version != 0 && autosave_objectFullyCaptured(row)`. Close file on completion. |
| 73 | filesystem.c:7702–7713 | Wait for close callback; chdir to root; advance to sync |
| 74 | filesystem.c:7715–7725 | `afatfs_sync()`; then remove old `.hcnames` via `afatfs_removeObjects_lfn()` |
| 75 | filesystem.c:7728–7745 | Wait for remove; rename `.hcnamtmp` → `.hcnames` via `afatfs_renameObject_lfn()` |
| 76 | filesystem.c:7748–7759 | Wait for rename; set `hcnames_mirror_valid = FS_HCNAMES_MIRROR_PUBLISH_PENDING` (line 7758); call `filesystem_finish(FS_STATUS_DONE)` |

#### 2d.5 Mirror validity state machine

The `hcnames_mirror_valid` variable (filesystem.c:935) tracks three states:

| State | Value | Meaning |
|-------|-------|---------|
| `FS_HCNAMES_MIRROR_INVALID` | 0 | Mirror untrustworthy; no HCNAMES rewrite permitted |
| `FS_HCNAMES_MIRROR_VALID` | 1 | Mirror authoritative; HCNAMES rewrite allowed |
| `FS_HCNAMES_MIRROR_PUBLISH_PENDING` | 2 | Rewrite completed, waiting for final sync to promote |

**Transitions:**
- INVALID → VALID: `filesystem.c:3941–3942` — shared final sync promotes
  after the facade's mandatory sync gate.
- VALID → INVALID: `filesystem.c:6763` — `autosaveDrainAfterCommit()` takes
  ownership before the rewrite starts; also `filesystem.c:4836` (mirror
  invalidation on certain error paths).
- PUBLISH_PENDING → VALID: `filesystem.c:3941–3942` — final sync promotion.
- PUBLISH_PENDING → INVALID: `filesystem.c:3527–3528` — error/stall discards
  trust so next consumer must reload.

#### 2d.6 Terminal callback: refreshed flag clearing

- **File:** `filesystem.c:22649–22658`
- **What:** After the shared final sync, if `status == FS_STATUS_DONE` and
  `op_file_version != 0` (line 22655), calls
  `filesystem_clearResidentRefreshedCaptured()` (line 22656) and resets
  `op_file_version = 0` (line 22657).
- **`filesystem_clearResidentRefreshedCaptured()`:** (filesystem.c:6724–6744)
  Scans all 129 rows. For each row where bit 13 is set AND
  `autosave_objectFullyCaptured(row)` returns true, clears bit 13. Rows whose
  objects are still dirty retain their refreshed flag for the next retry.
- **Why:** Clearing only happens after the `.hcnames` file is durably on
  card. An ERROR preserves the flag because `op_file_version` stays nonzero
  but the terminal callback only clears on `FS_STATUS_DONE`.

---

### 2e. Deferred re-dirty request mask

**Parent plan specified:** A `uint16_t` per-Scene mask in the writer workspace.

**Not implemented, and not needed.** The immediate compound markers (2a)
and immediate source marking (2b) provide the same correctness guarantees
as the deferred mechanism. The deferred mask would:
- Add ~4–6 bytes of persistent SRAM
- Add a mask-OR step at drain entry
- Add load/save completion calls to set the mask
- Introduce a new ordering dependency (mask must be consumed before clearing)

All of this complexity is avoided by the existing immediate marking path.

---

## 3. Analysis: name-byte re-dirtying is unnecessary

The parent plan specifies that save completions should re-dirty "name and
source" bits — 10 bytes per object (8 name + 2 source). Phase C implements
source marking but deliberately excludes name bytes. This section explains
why.

### 3a. Name bytes have no live getter

`autosave_getLivePayloadByte()` (Autosave.c:689–) dispatches these domains:

| Domain | Payload-relative range | Getter |
|--------|----------------------|--------|
| Bank slot | +0..+1 | `bank_restoreBankSlot()` |
| Bank name | +2..+9 | `bank_displayName()` via `autosave_nameByte()` |
| Bank fields | +10..+14 | explicit getters per field |
| Scene source | Scene+8..+9 | `autosave_getSourceByte()` (Autosave.c:785) |
| Scene parameters | Scene+10..+49 | `autosave_getSceneParameter()` |
| Effect parameters | Scene+137..+639 | `autosave_getEffectParameter()` (zero live count today) |
| Kit source | Kit+8..+9 | `autosave_getSourceByte()` (Autosave.c:826) |
| Kit parameters | Kit+10..+11 | explicit slot6/track7 getters |
| Instrument type | Inst+0..+2 | `instrumentManager_registryEntry()->type_text` |
| Instrument source | Inst+11..+12 | `autosave_getSourceByte()` (Autosave.c:892) |
| Instrument Normal | Inst+13..+84 | descriptor-indexed via registry |
| Instrument Morph | Inst+85..+156 | descriptor-indexed via registry (Morphable only) |

**Not dispatched (return 0):**
- Scene name: Scene+0..+7
- Kit name: Kit+0..+7
- Instrument name: Inst+3..+10
- Scene padding: Scene+50..+127
- Kit padding: Kit+12..+127
- Effect type/name: Scene+128..+136
- Instrument padding: Inst+157..+191

When the drain encounters a dirty bit for a name byte, it calls
`getLivePayloadByte()`, gets 0, and skips the byte without adding it to
the patch list. The dirty bit is consumed but no write occurs.

### 3b. Compound markers exclude names by design

From `Autosave.c:1319` (inside `markWholeInstrumentDirty`): "the
HCNAMES-owned name is deliberately excluded." The markers begin at
type/source offsets and skip the name region:
- `markWholeInstrumentDirty` starts at type byte 0 (3 bytes), then jumps
  to source offset 11 (2 bytes), then Normal offset 13 and Morph offset 85.
- `markKitDirty` marks Kit parameters (offset 10+) and Kit source, then
  delegates to `markWholeInstrumentDirty` for each slot.
- `markSceneWithoutPatternDirty` marks Scene parameters (offset 10+) and
  Scene source (offset 8), then delegates to Effect and Kit markers.

Name bytes (offsets 0..7 for Scene/Kit, offsets 3..10 for Instrument) are
never touched.

### 3c. Names in the autosave record may be stale after a load

After a Scene Load changes the resident scene, the autosave record retains
the old scene's name from initial creation. This is acceptable because:

1. **The reader uses `.hcnames` for identity, not autosave record names.**
   The `.hcnames` file carries the current name, source, and refreshed flag.
   The reader's load decision depends on source fields, refreshed flags, and
   mutation-bit cleanliness — not on autosave record names.

2. **Adding a name getter would require new RAM access.** The HCNAMES name
   mirror (`hcnames_name_mirror[]`) is a `filesystem.c`-private array
   (filesystem.c:924). To read from it, a new public accessor and dispatch
   would be needed, reversing the dependency (currently filesystem.c →
   Autosave.h, not the reverse).

3. **Re-dirtying names without a getter is a no-op.** Marking name bytes
   dirty then having `getLivePayloadByte()` return 0 wastes drain cycles.

### 3d. Conclusion

Name-byte re-dirtying serves no correctness purpose. The 8-byte name in
each sub-object header is a diagnostic artifact from initial record creation.
If a future Phase E requirement needs self-consistent autosave record names,
a name getter can be added at that time.

---

## 4. Remaining Phase D work: no code changes required

Given the audit above, Phase D has no new code to implement. Every
functional requirement is satisfied by existing code from Phases B2 and C.

### 4a. Code-change inventory (all marked NO CHANGE)

This table is the complete set of code sites relevant to Phase D. Every
entry is verified present and correct. No additions, removals, or
modifications are required.

#### Autosave.h — wire contract and API (no changes)

| Line | Symbol | Status | Notes |
|------|--------|--------|-------|
| 76–79 | Source field geometry defines | Present | `AUTOSAVE_SCENE_SOURCE_OFFSET`, `AUTOSAVE_KIT_SOURCE_OFFSET`, `AUTOSAVE_INSTRUMENT_SOURCE_OFFSET`, `AUTOSAVE_SOURCE_BYTES` — Phase C geometry absorbed into existing reserved space |
| 80–91 | HCNAMES row constants | Present | `AUTOSAVE_HCNAMES_ROW_COUNT` (129), base offsets for Scene/Kit/Instrument row ranges |
| 248–256 | Source field static asserts | Present | 4 Phase C asserts verifying source-to-parameter contiguity and Kit structure |
| 424 | `autosave_objectFullyCaptured()` declaration | Present | Phase B2 query function |
| 462 | `autosave_markSourceDirty()` declaration | Present | Phase C source-only dirty marker |
| 491–509 | Compound marker declarations | Present | `markWholeInstrumentDirty`, `markKitDirty`, `markEffectDirty`, `markSceneWithoutPatternDirty`, `markSceneWithPatternDirty`, `markResidentBankDirty` |

#### Autosave.c — dirty mask and live-byte projection (no changes)

| Line | Symbol | Status | Notes |
|------|--------|--------|-------|
| 133–157 | `autosave_markPayloadOffsetDirty()` | Present | Atomic core with IRQ-save, tracking guard, trace |
| 222–235 | `autosave_getSourceByte()` | Present | Phase C static helper; reads `filesystem_residentSource()` and extracts LE byte |
| 689–930+ | `autosave_getLivePayloadByte()` | Present | Full dispatch including source bytes at Scene+8, Kit+8, Instrument+11; names intentionally not dispatched |
| 1195–1242 | `autosave_markSourceDirty()` | Present | Phase C; maps HCNAMES row to payload source offset, marks 2 bytes |
| 1303–1390 | `autosave_markWholeInstrumentDirty()` | Present | Type + source + Normal + Morph; name excluded; trace emitted |
| 1392–1429 | `autosave_markKitDirty()` | Present | Kit params + Kit source + 6 × WholeInstrument; trace emitted |
| 1431–1448 | `autosave_markEffectDirty()` | Present | Zero-count stub; safe for future extension |
| 1450–1486 | `autosave_markSceneWithoutPatternDirty()` | Present | Scene params + Scene source + Effect + Kit cascade; trace emitted |
| 1488–1500 | `autosave_markSceneWithPatternDirty()` | Present | Delegates to WithoutPattern; Pattern stub reserved |
| 1502–1525 | `autosave_markResidentBankDirty()` | Present | All Bank fields + all present Scenes |
| 1571–1627 | `autosave_objectFullyCaptured()` | Present | Phase B2; scans full reserved interval per object type |

#### filesystem.h — public API and constants (no changes)

| Line | Symbol | Status | Notes |
|------|--------|--------|-------|
| 616–620 | `FS_IDENTITY_BANK_ROW`, `FS_IDENTITY_ROW_COUNT` | Present | Identity row enumeration |
| 634–639 | Source tokens and flags | Present | `FS_RESIDENT_SOURCE_INHERIT` (0x1FFF), `UNKNOWN` (0x1FFE), `INSTRUMENT_DIRECT` (0x1FFD), `REFRESHED_FLAG` (0x2000), `DIRTY_FLAG` (0x8000), `VALUE_MASK` (0x1FFF) |
| 642–643 | `filesystem_residentSource()`, `filesystem_setResidentSource()` | Present | Public read/write accessors for the 13-bit source value |

#### filesystem.c — state machine, flags, and convergence (no changes)

| Line | Symbol | Status | Notes |
|------|--------|--------|-------|
| 140–142 | `FS_RESIDENT_NAMES_ROW_COUNT` | Present | 129 rows (1 Bank + 16 Scenes + 16 Kits + 96 Instruments) |
| 905 | `fs_resident_source[]` | Present | 129-entry `uint16_t` array; source + refreshed + dirty flags |
| 924 | `hcnames_name_mirror[]` | Present | 129×9 char array; the HCNAMES name register |
| 932–935 | `FS_HCNAMES_MIRROR_*` constants and `hcnames_mirror_valid` | Present | 3-state validity flag |
| 1318–1322 | Forward declarations for 3 Phase B2 functions | Present | `autosaveDrainHasRefreshWork`, `clearResidentRefreshedCaptured`, `autosaveDrainAfterCommit` |
| 5200–5252 | Row-mapping helpers | Present | `residentInstrumentRow()`, `residentKitRow()`, `residentSceneRow()` — prevent row-offset drift between callers |
| 5293–5318 | `filesystem_setResidentSource()` | Present | Sets source value while preserving refreshed/dirty flags |
| 5545–5555 | `filesystem_setResidentRefreshed()` | Present | Sets bit 13; invalid rows ignored |
| 5558–5573 | `filesystem_setResidentSceneRefreshed()` | Present | Compound: Scene + Kit + 6 Instruments |
| 6699–6722 | `filesystem_autosaveDrainHasRefreshWork()` | Present | Scans 129 rows for refreshed+captured; mirror guard |
| 6724–6744 | `filesystem_clearResidentRefreshedCaptured()` | Present | Clears bit 13 only for captured rows |
| 6746���6765 | `filesystem_autosaveDrainAfterCommit()` | Present | Decision gate: finish vs. phase 70 |
| 7039 | `autosave_markResidentBankDirty()` call | Present | Mismatched winner path; seeds full re-capture |
| 7166–7176 | Drain clean-mask path calling `autosaveDrainAfterCommit()` | Present | First call site (no dirty bits but possible refresh work) |
| 7632–7634 | Drain post-commit calling `autosaveDrainAfterCommit()` | Present | Second call site (normal post-drain path) |
| 7637–7759 | Phases 70–76 HCNAMES safe-rewrite | Present | Complete temp-write/sync/remove/rename/sync pipeline |
| 10376–10384 | Kit Load refreshed flag calls | Present | Kit + 6 Instruments |
| 11870–11875 | Scene Load refreshed flag call | Present | Via `setResidentSceneRefreshed()` |
| 12659 | Bank Load (empty) refreshed flag | Present | Row 0 |
| 12835 | Bank Load (children) refreshed flag | Present | Row 0 |
| 13701–13704 | Instrument Load refreshed flag | Present | 1 Instrument row |
| 14095–14113 | Instrument Save source+dirty+refreshed | Present | Phase C pairing |
| 15799–15831 | Kit Save source+dirty+refreshed | Present | Phase C pairing (7 rows) |
| 16625–16628 | Bank Save source+refreshed | Present | Row 0 only; no source dirty (Bank has no autosave source field) |
| 17383–17413 | Scene Save source+dirty+refreshed | Present | Phase C pairing (8 rows) |
| 20395–20422 | `formatResidentNameLine()` R-suffix suppression | Present | Uses `objectFullyCaptured()` + `op_file_version` to suppress R for captured rows during drain rewrite |
| 22649–22658 | Terminal callback refreshed clearing | Present | `clearResidentRefreshedCaptured()` on success; `op_file_version` reset |
| 22789 | Runtime setup `markResidentBankDirty()` | Present | Initial full-capture seed |
| 24029 | Re-enable `markResidentBankDirty()` | Present | OFF→ON transition re-seed |

#### presetManager.c — Preset layer load callbacks (no changes)

| Line | Symbol | Status | Notes |
|------|--------|--------|-------|
| 277 | `autosave_markKitDirty(scene_index)` | Present | Normal Kit Load completion |
| 490 | `autosave_markSceneWithoutPatternDirty(scene_index)` | Present | Root Scene Load completion |
| 541 | `autosave_markSceneWithoutPatternDirty(scene_index)` | Present | Bank-child Scene Load completion |
| 1676 | `autosave_markInstrumentMorphDirty(scene, slot)` | Present | InstrumentMrp compatible endpoint copy |
| 1772 | `autosave_markInstrumentMorphDirty(scene, slot)` | Present | KitMrp Morph copy |
| 1883 | `autosave_markWholeInstrumentDirty(target_scene, slot)` | Present | Root Instrument Load completion |

---

## 5. Verification testing of the end-to-end lifecycle

Since no code changes are required, Phase D's remaining work is hardware
verification testing. These tests verify that the already-implemented
machinery works correctly end-to-end.

### Test 1: Load → drain → refreshed flag cleared → HCNAMES rewritten

**Preconditions:** Booted with a known bank, autosave active, card mounted.

**Steps:**
1. Note the `.hcnames` file contents on card (expected: no R suffixes on
   any row after a clean boot and full initial drain).
2. Perform a Scene Load (any scene, any slot).
3. Allow 2–3 drain cycles to complete (approximately 10–15 seconds of idle).
4. Pull the card and inspect.

**Expected results:**
- `.hcnames` was safe-rewritten (fresh file, no stale `.hcnamtmp`).
- The loaded scene's row shows the load source slot number (not UNKNOWN or
  the old value).
- The loaded scene's Kit row shows INHERIT (`-` token).
- All 6 Instrument rows for the loaded scene show INHERIT.
- The `R` suffix is absent on all rows that were refreshed (refreshed flag
  cleared after full capture).
- The autosave record's 2-byte source fields at the Scene, Kit, and
  Instrument payload offsets match the `.hcnames` source values.

**Code path exercised:**
1. `presetManager.c:490` → `autosave_markSceneWithoutPatternDirty()` →
   compound cascade → `autosave_markPayloadOffsetDirty()` for all live bytes.
2. `filesystem.c:11875` → `filesystem_setResidentSceneRefreshed()` → bit 13
   set on 8 rows.
3. Drain phase 56 captures live values → phase 7634 →
   `filesystem_autosaveDrainAfterCommit()` → `autosaveDrainHasRefreshWork()`
   returns 1 → phase 70 HCNAMES rewrite.
4. `formatResidentNameLine()` at line 20418–20421 suppresses R for captured
   rows.
5. Terminal callback at line 22655 → `clearResidentRefreshedCaptured()` →
   bit 13 cleared on captured rows.

### Test 2: Save → drain → refreshed flag cleared → HCNAMES rewritten

**Preconditions:** Resident bank with at least one Scene slot available.

**Steps:**
1. Perform a Scene Save (or Kit Save or Instrument Save).
2. Allow drain cycles to complete.
3. Pull the card and inspect.

**Expected results:**
- `.hcnames` source values reflect the saved slot numbers.
- `R` suffix is absent (refreshed cleared after source capture).
- Autosave record source bytes match the `.hcnames` source values.

**Code path exercised:**
1. `filesystem.c:17383–17413` (for Scene Save) → source staged, dirty
   marked, refreshed set.
2. Drain captures the 2-byte source values via `autosave_getSourceByte()`.
3. Post-drain HCNAMES convergence as above.

### Test 3: Load during active drain (race condition)

**Steps:**
1. Trigger a Scene Load while a drain is actively scanning (load immediately
   after observing a drain start, or during a long multi-tick capture).
2. Allow the drain plus at least one subsequent drain to complete.

**Expected results:**
- The refreshed flag stays set until the next drain fully captures all bytes.
- `objectFullyCaptured()` returns false for the loaded scene during the
  partial drain (bits re-dirtied mid-scan remain set).
- The subsequent drain captures the remaining bits, then HCNAMES convergence
  clears the flag.
- No CRC validation failure on the committed record (the partial drain
  copy-forwards un-captured bytes, preserving record integrity).

**Why this is safe:** `autosave_markPayloadOffsetDirty()` uses
`autosave_maskByteOr()` with IRQ-save/restore. The drain's
`autosave_maskBitTake()` (Autosave.c:1629) atomically reads and clears one
bit at a time. A concurrent re-dirty after the take is a new bit that
survives for the next cycle.

### Test 4: Multiple overlapping loads

**Steps:**
1. Load Scene A, then immediately load Scene B before the first load's
   drain completes.
2. Allow 2–3 drain cycles to complete.

**Expected results:**
- Both scenes eventually converge: refreshed flags cleared, source fields
  correct, HCNAMES consistent.
- Scene A's bytes may be partially captured in the first drain and completed
  in the second; Scene B's bytes may start capture in the first or second
  drain depending on timing.
- The HCNAMES rewrite only fires when at least one refreshed row has a fully
  captured object, so it may take multiple drains before all rows converge.

---

## 6. Risk registry from previous phases

### From Phase A (writer speedup)

- **Off-by-one in drain chunking**: The drain processes a bounded number of
  mask positions per tick. Phase A optimized this but the bounds must be
  respected by any new mask-OR or scan operation. Phase D adds no new drain
  operations, so this risk does not apply.

### From Phase B/B2 (HCNAMES safe-write, refreshed flag)

- **HCNAMES mirror validity**: The post-drain HCNAMES rewrite reads from
  `hcnames_name_mirror[]` (filesystem.c:924) and `fs_resident_source[]`
  (filesystem.c:905). If the mirror is invalid
  (`hcnames_mirror_valid != FS_HCNAMES_MIRROR_VALID`, checked at
  filesystem.c:6713), the `autosaveDrainHasRefreshWork()` function fails
  closed (returns 0, skipping the rewrite). This guard is critical: a stale
  mirror serialized to card would corrupt the `.hcnames` file.

- **Error path preserves refreshed flags**: If the post-drain HCNAMES
  rewrite fails (disk full, I/O error), the refreshed flags are NOT cleared.
  The terminal callback at filesystem.c:22655 only clears when
  `status == FS_STATUS_DONE`. `op_file_version` stays nonzero across the
  failure, so the next successful drain retries the rewrite.

- **Safe-write atomicity**: The `.hcnamtmp` → `.hcnames` rename pattern
  (phases 74–76) ensures that a power failure at any point leaves either the
  old valid `.hcnames` or a fully synced new one. The temp file's presence
  after a power failure is harmless.

- **PUBLISH_PENDING error recovery**: If the shared final sync fails after
  the rename, the mirror transitions PUBLISH_PENDING → INVALID (filesystem.c:
  3527–3528), forcing a reload on the next consumer. The refreshed flags
  remain set because the terminal callback does not clear on ERROR.

### From Phase C (source fields)

- **Zero-growth discipline**: Phase C absorbed source fields into existing
  reserved space with no record growth. Phase D must maintain this: no new
  persistent SRAM, no new fields in the wire format. Phase D adds zero bytes.

- **Pairing source mutations with mask updates**: Every
  `filesystem_setResidentSource()` call is paired with
  `autosave_markSourceDirty()`. Verified at all sites:
  - Instrument Save: filesystem.c:14095 + 14103 ✓
  - Kit Save: filesystem.c:15799 + 15805, 15810 + 15815 ✓
  - Scene Save: filesystem.c:17383 + 17388, 17390 + 17393, 17400 + 17404 ✓
  - Bank Save: filesystem.c:16625 (no autosave source field, `markSourceDirty` returns immediately for row 0) ✓

- **Source token encoding**: Post-Phase-B2 tokens are 13-bit
  (`INHERIT = 0x1FFF`, `UNKNOWN = 0x1FFE`, `INSTRUMENT_DIRECT = 0x1FFD`).
  Defined at filesystem.h:634–636. The parent plan document still references
  old 15-bit values. Follow the code, not the document.

### General best practices

- **`make clean && make`** after any header change. No header dependency
  tracking in the Makefile; incremental builds can miss stale object files.
- **Static asserts are the safety net.** Every geometry relationship has
  a corresponding `_Static_assert`. Phase C added 4 new structural asserts
  (Autosave.h:248–261); Phase D adds none (no new geometry).
- **Audit all call sites.** The complete audit is in Section 4a above.

---

## 7. Forward declarations already in place

Phase B2 added forward declarations for three functions in `filesystem.c`
that were designed for Phase D integration:

```
static uint8_t filesystem_autosaveDrainHasRefreshWork(void);   /* line 1320 */
static void filesystem_clearResidentRefreshedCaptured(void);   /* line 1321 */
static void filesystem_autosaveDrainAfterCommit(void);         /* line 1322 */
```

All three are **fully implemented** (not just declared):

- `filesystem_autosaveDrainHasRefreshWork()` (`filesystem.c:6699–6722`):
  scans all 129 rows for refreshed + fully-captured objects. Returns 1 if
  any exist. Guards on `hcnames_mirror_valid`.

- `filesystem_clearResidentRefreshedCaptured()` (`filesystem.c:6724–6744`):
  clears bit 13 on every row where refreshed is set AND
  `objectFullyCaptured()` returns true. Called from the terminal callback
  after a successful HCNAMES convergence commit.

- `filesystem_autosaveDrainAfterCommit()` (`filesystem.c:6746–6765`):
  entry point called from drain phase completion (lines 7170, 7634). If no
  refresh work exists, finishes the drain normally. Otherwise, sets
  `op_file_version = 1` and enters phase 70 (HCNAMES temp-file writer).

These functions are called from the drain state machine at two points:
1. After mask merge when no dirty bits exist (line 7170) — handles the case
   where a load set the refreshed flag but no parameter edits are pending.
2. After the autosave target commit (line 7634) — the normal post-drain
   convergence path.

---

## 8. Implications for Phase E (boot reader)

The boot reader needs to determine, for each sub-object, whether to load
from the autosave record or from the library:

- **Case 1a** (not refreshed, mutation bits clean): load from autosave.
  The record fully represents the current state.
- **Case 1b** (refreshed): load from library source. The record may be
  mid-capture. The `.hcnames` source field tells the reader which library
  slot to load from.
- **Case 1c** (stale name/source, no library fallback): present as empty,
  display error. This is a degraded recovery path.

The writer-side preparation for Phase E is complete:
- Source fields identify where each sub-object came from (Phase C).
- The refreshed flag distinguishes "autosave is authoritative" from "reload
  from library" (Phase B2).
- The post-drain HCNAMES convergence ensures that cleared refreshed flags
  are durably persisted to card (Phase B2).
- Mutation-bit cleanliness is the gate for refreshed flag clearing, ensuring
  the record is fully captured before declaring it authoritative (Phase B2).

Phase E implementation is deferred to a future session.

---

## 9. Summary

| Phase D requirement | Status | Implemented by |
|---------------------|--------|---------------|
| Load re-dirties all mutation bits | Done | Compound markers (immediate, via Preset callbacks) |
| Save re-dirties source bits | Done | Phase C `autosave_markSourceDirty()` at save completions |
| Save re-dirties name bits | Not needed | Names have no live getter; reader uses `.hcnames` for identity |
| Refreshed flag set at load/save | Done | Phase B2 `filesystem_setResidentRefreshed()` |
| Post-drain objectFullyCaptured check | Done | Phase B2 `filesystem_autosaveDrainHasRefreshWork()` |
| Post-drain HCNAMES safe-rewrite | Done | Phase B2 drain phases 70–76 |
| Refreshed flag clearing after commit | Done | Phase B2 `filesystem_clearResidentRefreshedCaptured()` |
| Deferred re-dirty request mask | Not needed | Immediate marking is correct and lower-latency |
| SRAM cost | 0 bytes | No new persistent allocation |

**Phase D requires no new code.** All functional requirements are satisfied
by the combination of Phase B2 (refreshed flag lifecycle, post-drain
HCNAMES convergence) and Phase C (source dirty marking at save completions).
The remaining work is verification testing of the end-to-end lifecycle
described in Section 5.
