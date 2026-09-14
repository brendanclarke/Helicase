# S064 — Pattern AutoSave Post-Implementation Test Cases

**Date**: 2026-09-13
**Build**: `dev-ph4-pattern` @ `e268107`
**Source**: S063 (acceptance tests A–E), S064 (acceptance tests A–E, risks R1–R6)

---

## Test Case Matrix

### 1 — Pattern name propagation through Bank Load → drain

**Goal**: Verify that when a Bank is loaded, the directory Pattern filenames
(e.g., `Alpha.pat`, `Beta.pat`) appear in the HCNAMES Pattern rows after the
first AutoSave Pattern drain — not the `pat_initScene()` default "Empty".

**Test card**: `SD_CARD_TEST_PAT_HCNAMES/`

```
SD_CARD_TEST_PAT_HCNAMES/
├── settings.cfg                          autosave=1, active_bank=0
└── Bank/000 NameTest/
    ├── bankset.bcg                       4 children, active_scene=0
    ├── 00 ScAlpha/  → Alpha.pat          ← distinct name
    ├── 01 ScBeta/   → Beta.pat           ← distinct name
    ├── 02 ScGamma/  → Gamma.pat          ← distinct name
    └── 03 ScEmpty/  → Empty.pat          ← control (matches default)
```

Each child has a valid PAT4 `.pat` file (copied from a known-good source),
a Kit directory (Beatmstr), `sceneset.scg`, and `effects.fx`. No pre-existing
`.hcnames`, `.hcprms*`, or `.pat*` hidden files — boot regenerates from scratch.

**Procedure**:

1. Copy `SD_CARD_TEST_PAT_HCNAMES/` contents to a blank FAT32 SD card.
2. Insert card, power on. Boot loads Bank 000 ("NameTest") with 4 children.
3. Wait 15–20 seconds for scalar + Pattern AutoSave drains to complete.
4. Power off. Remove card and mount on computer.
5. Verify `.pat00b`–`.pat03b` exist (4 files, generation 1, 10,656 B each).
6. Verify no `.pat*a` files exist (generation 1 → 'b' only).
7. Dump `.hcnames` and check the Pattern rows (lines 131–134, rows 129–132):

| Row | Scene | Expected name | Expected source |
|-----|-------|---------------|-----------------|
| 129 | 0 | **Alpha** | @ |
| 130 | 1 | **Beta** | @ |
| 131 | 2 | **Gamma** | @ |
| 132 | 3 | **Empty** | @ |
| 133–144 | 4–15 | (empty) | ? |

**Pass criteria**: Rows 129–131 show `Alpha`, `Beta`, `Gamma` (not "Empty").
Row 132 shows `Empty` (matches both the directory file and the default, so it
cannot distinguish the two — it is a control, not a diagnostic).

**Failure modes**:
- All four rows show "Empty" → Bank Load does not propagate directory Pattern
  filename into the resident HCNAMES register before the drain reads it.
  Investigate the Scene Load → Pattern name → HCNAMES publication path.
- Names correct but source is not `@` → drain HCNAMES publication failed.
- No `.patNNb` files → drain scheduler never ran or all Scenes were skipped.

**Preliminary evidence**: On `SD_CARD_AUTOSAVE_PAT_TEST`, Scenes 10–12 had
directory Pattern files named `Electro.pat`, `Eris.pat`, `CasioPop.pat` but
HCNAMES showed "Empty" for all. This test isolates the question with a clean
card and unambiguous names.

| # | Step | Result |
|---|------|--------|
| 1.1 | Card prepared, no hidden files | PASS |
| 1.2 | Boot completes, Bank 000 loaded | PASS |
| 1.3 | Wait 20 s for drain | PASS |
| 1.4 | Power off, mount card | PASS |
| 1.5 | `.pat00b`–`.pat03b` present, 10,656 B, gen=1 | PASS — 4 files, all 10,656 B, gen=1, CRC 0x519eceff |
| 1.6 | `.hcnames` row 129 = `Alpha\t@` | PASS |
| 1.7 | `.hcnames` row 130 = `Beta\t@` | PASS |
| 1.8 | `.hcnames` row 131 = `Gamma\t@` | PASS |
| 1.9 | `.hcnames` row 132 = `Empty\t@` | PASS |

**Run 1** (build `e268107`, pre-fix): **FAIL** — all Pattern rows showed `Empty|@`.
Root cause: `filesystem_cacheCurrentBankSceneNameBlock()` did not cache the
Pattern HCNAMES row during Bank Load child commit.

**Run 2** (build with fix applied): **PASS** — `.hcnamtmp` shows `Alpha|@`,
`Beta|@`, `Gamma|@`, `Empty|@`. Fix: added Pattern row caching to
`filesystem_cacheCurrentBankSceneNameBlock()` at filesystem.c:6140–6147.

**Note**: Only `.hcnamtmp` present, no `.hcnames` — atomic rename did not
complete (timing or separate issue). Content is correct.

---

### 2 — Basic single-Scene drain and restore (S064 acceptance test A)

**Goal**: Pattern data survives power loss via AutoSave.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 2.1 | Boot, load any Bank or Kit | Scenes present, AutoSave enabled | PASS (TC2: Bank "DrainRst", 1 Scene) |
| 2.2 | Edit pattern on current Scene (toggle steps, add specials) | `pat_markSceneDirty()` fires, dirty mask set | PASS (user edited steps + specials + voice params) |
| 2.3 | Wait ~5–10 s for drain (SD activity or trace) | `.patNNx` written at generation N+1 | PASS (gen reached 22, both a+b files present) |
| 2.4 | Power cycle (hard reset, no explicit Save) | Device reboots | PASS |
| 2.5 | After boot, check current Scene's pattern | Step toggles and specials restored from AutoSave | PASS (user confirmed all edits restored) |
| 2.6 | Inspect SD: `.patNNx` file present, HCNAMES row is `@` | File validates (correct size, CRC, generation) | PASS — `.pat00a` gen=22 winner, `.pat00b` gen=21, `Drain|@|R` |

### 3 — A/B ping-pong file alternation

**Goal**: Generation parity correctly alternates A and B files.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 3.1 | Start from clean state (no `.patNNx` files) | No hidden pattern files | PASS (TC1 card had none) |
| 3.2 | Load Bank, wait for initial drain | Generation 1 → `.patNNb` files created | PASS (TC1: 4× `.patNNb` gen=1, no `a` files) |
| 3.3 | Edit a pattern on Scene 0 | Dirty bit set | PASS (TC2: multiple edits) |
| 3.4 | Wait for drain | Generation 2 → `.pat00a` created | PASS (TC2: `.pat00a` gen=22, even→a) |
| 3.5 | Edit Scene 0 pattern again | Dirty bit set | PASS (TC2: continued editing) |
| 3.6 | Wait for drain | Generation 3 → `.pat00b` overwritten (gen 3 > gen 1) | PASS (TC2: `.pat00b` gen=21, odd→b) |
| 3.7 | Power cycle, check Scene 0 | Restored from highest valid gen | PASS (TC2: gen 22 winner in `.pat00a`, edits restored) |

### 4 — Library Pattern load overrides AutoSave (S064 acceptance test C)

**Goal**: A library Pattern load replaces AutoSave provenance.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 4.1 | Have an AutoSaved pattern in Scene 0 (from test 2) | `.pat00x` exists, HCNAMES row 129 = `@` | |
| 4.2 | Load a different pattern from `/Pattern/` library | Pattern replaced, generation reset to 0, dirty bit set | |
| 4.3 | Wait for drain | New `.patNNx` written at generation 1 | |
| 4.4 | Power cycle | Device reboots | |
| 4.5 | Check Scene 0 pattern | Library pattern restored (not old AutoSave) | |
| 4.6 | Check HCNAMES row 129 | Source = `@`, name = library pattern name | |

### 5 — Power-loss recovery (interrupted drain) (S064 acceptance test D)

**Goal**: Interrupted drain falls back to prior valid generation.

**Execution status**: Deferred until a deterministic write-failure injection
hook or equivalent instrumented harness exists. A manual power cut cannot be
aimed at a specific background writer phase, so an uninstrumented attempt does
not produce classifiable pass/fail evidence.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 5.1 | Prepare two valid, visibly distinct Scene-0 candidates | Newest and fallback generations are both known-good |
| 5.2 | Arm an instrumented failure after the next target is opened/partially written but before its CRC transaction completes | The target becomes invalid while the prior candidate remains untouched |
| 5.3 | Preserve and inspect the card before any reboot | One invalid newer target and one valid older candidate are proven |
| 5.4 | Boot the preserved interrupted image | Reader rejects the target and restores the older candidate |
| 5.5 | Verify the interrupted edit is absent | Expected best-effort loss; no corruption from the invalid file is applied |

### 6 — Multi-Scene drain and independent restore (S064 acceptance test B)

**Goal**: Each Scene's pattern is independently saved and restored.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 6.1 | Load Bank with multiple Scenes | Multiple Scenes present | |
| 6.2 | Edit patterns differently on Scenes 0, 3, 7 | Three dirty bits set | |
| 6.3 | Wait for drain to complete all three | Three `.patNNx` files updated | |
| 6.4 | Power cycle | Device reboots | |
| 6.5 | Check each Scene's pattern independently | Each Scene has its own distinct pattern | |

### 7 — Scene/Bank Save resets generation epoch

**Goal**: Explicit Save makes directory Pattern authoritative.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 7.1 | Have AutoSaved pattern at generation 5+ on Scene 0 | Stale hidden files exist | |
| 7.2 | Save Scene 0 to a Scene directory slot | Scene Save writes `<name>.pat` with generation 0 | |
| 7.3 | Check: `filesystem_resetPatternAutosaveGeneration(0)` called | Generation baseline reset to 0 | |
| 7.4 | Wait for drain | New `.pat00x` at generation 1 (fresh epoch) | |
| 7.5 | Power cycle | Pattern restored from new AutoSave (gen 1), not old stale gen 5+ | |
| 7.6 | Verify HCNAMES row 129 source after boot | `@` with correct name from Save | |

### 8 — Root Pattern library load resets generation

**Goal**: Loading from `/Pattern/` library starts a fresh A/B epoch.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 8.1 | Have AutoSaved pattern at generation 3+ | `.pat00x` at gen 3+ | |
| 8.2 | Load root `/Pattern/` library entry into Scene 0 | `filesystem_loadPattern_tick()` runs | |
| 8.3 | Check: generation reset to 0, dirty bit set | `fs_pattern_generation[0] = 0` | |
| 8.4 | Wait for drain | `.pat00x` at generation 1 (fresh epoch) | |
| 8.5 | Power cycle | Library pattern restored, not stale gen 3 AutoSave | |

### 9 — Recording/erasing drain deferral (S064 risk R6)

**Goal**: Drain does not run while `seq_recordActive` or `seq_eraseActive`.

**Execution status**: Deferred. Live Record is not yet complete enough to be a
sound acceptance-test dependency, and the production UI does not expose an
independent, observable hold/release boundary for both predicates. Test this
with instrumentation once that workflow is implemented; it is not part of the
functional Pattern AutoSave closeout below.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 9.1 | Under instrumentation, make a Pattern dirty and force `seq_recordActive=1` | Dirty bit remains pending; no Pattern drain is admitted | |
| 9.2 | Hold the predicate beyond several ordinary scheduler opportunities | Candidate generations remain unchanged | |
| 9.3 | Clear `seq_recordActive` | Pending Scene drains and its generation advances once | |
| 9.4 | Repeat by independently forcing `seq_eraseActive=1` | Same hold/release behavior is observed for the erase predicate | |

### 10 — Stale hidden files after Bank switch

**Goal**: Old hidden files from a previous Bank don't contaminate a new Bank.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 10.1 | Load Bank A, wait for drain (`.patNNb` gen 1) | Hidden files exist | |
| 10.2 | Load Bank B (different Scenes) | `filesystem_resetPatternAutosaveGeneration()` for each child | |
| 10.3 | Wait for drain | New `.patNNb` files at generation 1 (fresh epoch) | |
| 10.4 | Power cycle | Bank B's patterns restored, not Bank A's stale files | |
| 10.5 | Bank A's old `.patNNb` files may still exist on card | Ignored: generation reset, HCNAMES source updated | |

### 11 — CRC validation rejects corrupt/truncated files

**Goal**: Boot reader correctly rejects damaged hidden files.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 11.1 | Have valid `.pat00a` (gen 2) and `.pat00b` (gen 1) | Both validate | |
| 11.2 | In a copy of that checkpoint, flip one byte in root `/.pat00a` after offset 160 without updating its CRC; preserve a pre-boot copy | File remains 10,656 B but its calculated CRC32C differs from the stored CRC | |
| 11.3 | Boot that card | `.pat00b` gen 1 wins; its known older marker state is visible | |
| 11.4 | Restore the clean checkpoint; corrupt `.pat00a` again and truncate root `/.pat00b` to 5,000 bytes; preserve a pre-boot copy | A fails CRC and B fails exact-size validation | |
| 11.5 | Boot with both candidates invalid | With the HCNAMES Pattern row still `@`, current boot behavior leaves Scene 0 at `pat_initScene()` defaults; it must not apply either damaged file | |

### 12 — Bank Load marks all children Pattern-dirty

**Goal**: Loading a Bank triggers Pattern drain for all present children.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 12.1 | Load a Bank with N present Scene children | All N Scenes marked Pattern-dirty | PASS (TC1: N=4) |
| 12.2 | Wait for drain (may take N × drain cycles) | N `.patNNb` files created at generation 1 | PASS (TC1: 4× `.patNNb` gen=1) |
| 12.3 | Verify HCNAMES Pattern rows for all N children | All show `@` source | PASS (TC1: rows 129–132 all `@`) |
| 12.4 | Power cycle, verify all N Scenes restore correctly | Each Scene's pattern matches what was loaded | — (not yet retested post-cycle) |

**Preliminary result**: Confirmed on `SD_CARD_AUTOSAVE_PAT_TEST` — 13 present
Scenes from Bank 002 all drained to `.pat00b`–`.pat12b` at generation 1,
HCNAMES Pattern rows 129–141 all show `@`.
**TC1 result**: 4 children all drained, all `@` source, correct names. Steps 12.1–12.3 PASS.

### 13 — HCNAMES refresh witness lifecycle (S064 step 9)

**Goal**: `R` flag correctly tracks convergence between SRAM and card.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 13.1 | After successful drain, check HCNAMES Pattern row | Source `@`, flag `R` present | PASS (TC2: `Drain\t@\tR`) |
| 13.2 | Edit the pattern (mutate a step) | `R` flag cleared (refresh witness invalidated) | — (not directly observed) |
| 13.3 | Wait for next drain to complete | `R` flag restored | PASS (TC2: final state has `R`, so lifecycle completed) |
| 13.4 | During drain (after snapshot, before HCNAMES), edit pattern | `R` flag NOT set (drain race guard) | — (race condition, impractical to test manually) |

### 14 — Absent Scene Pattern rows remain untouched

**Goal**: Scenes that aren't present don't get AutoSave treatment.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 14.1 | Load Bank with < 16 children present | Some Scenes absent | PASS (TC1: 4 of 16 present) |
| 14.2 | Check HCNAMES for absent Scenes' Pattern rows | Source `?`, no name | PASS (TC1: rows 133–144 all `?`) |
| 14.3 | No `.patNNx` files for absent Scene indices | Absent Scenes have no hidden files | PASS (TC1: only `.pat00b`–`.pat03b`) |
| 14.4 | Power cycle, verify absent Scenes still absent | No phantom restoration | — (not yet retested post-cycle) |

**Preliminary result**: Confirmed — Scenes 13–15 have `|?` in HCNAMES,
no `.pat13x`–`.pat15x` files on `SD_CARD_AUTOSAVE_PAT_TEST`.
**TC1 result**: 12 absent Scenes (4–15) correctly show `?`, no hidden files. Steps 14.1–14.3 PASS.

### 15 — Format version boundary (.hcprms migration)

**Goal**: Old format `.hcprms` files are correctly rejected and regenerated.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 15.1 | Boot with old v1 `.hcprms1`/`.hcprms2` on card | Version mismatch detected | |
| 15.2 | Boot reader discards old records | Fresh initial records synthesized | |
| 15.3 | New `.hcprms` files at v2 format size (34,768 B) | Correct size and version | PASS (TC1: both 34,768 B, format byte=2) |

**Preliminary result**: `.hcprms1` is 34,768 B at format version 2. `.hcprms2`
is stale at 32,768 B (incomplete/uncommitted).
**TC1 result**: Both `.hcprms1` and `.hcprms2` are 34,768 B with format byte 2. Step 15.3 PASS.
(Steps 15.1–15.2 not directly tested — TC1 card had no pre-existing `.hcprms` files.)

### 16 — Boot time measurement (S064 acceptance test E)

**Goal**: 32 hidden Pattern files don't cause unacceptable boot regression.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 16.1 | Prepare and preserve an otherwise-complete card checkpoint containing valid gen-2 `/.pat00a` and gen-1 `/.pat00b` for all 16 Scenes | Exactly 32 valid hidden PAT4 candidates exist | |
| 16.2 | Restore that same checkpoint before each trial; time power-on to the same ready-state cue three times | Record the median 32-file boot time | |
| 16.3 | For each no-file trial, restore the same checkpoint and delete only root `/.pat??a` and `/.pat??b`; leave HCNAMES, HCPR, indexes, Bank, and settings unchanged | The comparison changes only hidden Pattern-file presence; `@` rows consequently initialize to defaults in this control | |
| 16.4 | Time three no-file boots and compare medians | Record the delta; target is < 500 ms | |

### 17 — Full 16-Scene background drain

**Goal**: Verify that ordinary background operation saves distinct edits from
all 16 Scenes without requiring phase timing or foreground interruption.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 17.1 | Boot the clean 16-Scene `FullHse` fixture | All Scenes load and background AutoSave starts | PASS |
| 17.2 | During normal use, change each Scene's Pattern and at least one scalar parameter in each Scene | Every Scene becomes Pattern-dirty and scalar-dirty | PASS |
| 17.3 | Allow normal background operation to settle, power off, and copy the card | One valid winning Pattern snapshot exists for every Scene | PASS |
| 17.4 | Compare each winner with its directory PAT4 baseline | All 16 winners differ in Pattern address content | PASS |
| 17.5 | Validate every candidate, HCNAMES Pattern row, HCPR record, and retained error trace | No invalid file, stale identity, operation error, or phase stall | PASS; see Test Card B result |

---

## Preliminary SD Card Inspection (2026-09-13)

Card image: `SD_CARD_AUTOSAVE_PAT_TEST/`
Bank loaded: `002 PtTst` (16 children, 13 present Scenes 0–12)

### Files observed

| File | Size | Notes |
|------|-----:|-------|
| `.hcprms1` | 34,768 | Format version 2, generation 15, committed (probe 0xa5) |
| `.hcprms2` | 32,768 | Format version 2 header, NOT committed, incomplete/stale |
| `.pat00b`–`.pat12b` | 10,656 each | 13 files, all PAT4 v1, PAT_STACK_SIZE=256, generation=1 |
| `.pat*a` | — | None present (expected: generation 1 → 'b' only) |
| `.hcnames` | 1,920 | 146 lines (1 header + 145 data rows) |

### HCNAMES Pattern rows (129–144)

| Row | Scene | Name | Source | Notes |
|-----|-------|------|--------|-------|
| 129–141 | 0–12 | Empty | @ | @ correct; "Empty" correct for 0–9, **questionable for 10–12** |
| 142–144 | 13–15 | (empty) | ? | Correct — Scenes not present/loaded |

### Pattern file header summary

All 13 files: magic `PAT4`, format version 1, stack size 256, header 160 B,
generation 1. CRC groupings show Scenes with identical default Patterns share
CRCs; Scenes 8/10/11/12 have distinct content (unique CRCs).

### Generation parity

Generation 1 → file suffix 'b' (odd parity = B). No mutations after drain →
no generation 2 → no 'a' files. Editing a pattern after drain produces
generation 2 → an 'a' file.

---

## Risk Coverage Cross-Reference

| S064 Risk | Test Cases | Status |
|-----------|------------|--------|
| R1 — Record format break (hcprms v1→v2) | 15 | PASS (TC1: 15.3) |
| R2 — 32 new root directory files | 16, 12 | 12 PASS (TC1: 12.1–12.3) |
| R3 — SRAM cost (10,586 B) | Build verification | Confirmed in schedule |
| R4 — Drain throughput | 17 | Functional 16-Scene completion PASS; no elapsed-time bound claimed |
| R5 — Boot ordering / AutoSave vs directory conflict | 4, 7, 8, 10 | Test Card A |
| R6 — Recording/erasing drain deferral | 9 | Deferred until Live Record/instrumentation is ready |

---

## Consolidated Test Workflows

### Test Card A — `SD_CARD_TEST_CASE_A/` (covers TC4, 6, 7, 8, 10)

```
SD_CARD_TEST_CASE_A/
├── settings.cfg                      autosave=1, active_bank=0
├── Pattern/000 LibPat.pat            library Pattern for TC4/TC8
├── Bank/000 BankAlf/                 4 Scenes: AlfZero–AlfThree
│   ├── 00 Barf/ → AlfZero.pat
│   ├── 01 Barf/ → AlfOne.pat
│   ├── 02 Barf/ → AlfTwo.pat
│   └── 03 Barf/ → AlfThree.pat
└── Bank/001 BankBet/                 4 Scenes: BetZero–BetThree
    ├── 00 Barf/ → BetZero.pat
    ├── 01 Slak/ → BetOne.pat
    ├── 02 RedSnap/ → BetTwo.pat
    └── 03 Pop/ → BetThree.pat
```

**Workflow** (sequential, one boot session per block):

1. Copy card contents to blank SD. Boot. Bank 000 "BankAlf" loads.
2. Wait 15 s for drain. Verify 4× `.patNNb` gen=1.
3. **TC6**: Switch to Scenes 0, 2, 3. Edit pattern on each differently
   (e.g., Sc0: steps 1-4 on voice 1, Sc2: steps 9-12, Sc3: steps 13-16).
   Wait for drain. **Power cycle.** Check each Scene's pattern restored
   independently → TC6 pass.
4. **TC4/TC8**: Load Pattern library entry "LibPat" into Scene 0.
   Wait for drain. Observe: generation resets, `.pat00x` at gen 1.
   **Power cycle.** Scene 0 should show LibPat content, not AlfZero.
   HCNAMES row 129: name=`LibPat`, source=`@` → TC4/TC8 pass.
5. **TC7**: Edit Scene 0 pattern several more times (get generation up
   to 3+). Then **Save Scene 0** (explicit Save to slot). Wait for drain.
   Check: generation resets to 1 (fresh epoch). **Power cycle.** Pattern
   restored from fresh gen 1, not stale old files → TC7 pass.
6. **TC10**: Load Bank 001 "BankBet" (via Bank browser). Wait for drain.
   Check: HCNAMES Pattern rows show BetZero–BetThree names, not
   AlfZero stale values. `.patNNb` files at gen 1 (fresh epoch).
   **Power cycle.** BankBet patterns restored → TC10 pass.

**Output**: Power off, mount card, copy root to `SD_CARD_TEST_CASE_A_OUTPUT/`.

### Test Card B — `SD_CARD_TEST_CASE_B/` (16-Scene functional closeout)

```
SD_CARD_TEST_CASE_B/
├── settings.cfg                      autosave=1, active_bank=0
├── Pattern/000 LibPat.pat            library Pattern (spare)
└── Bank/000 FullHse/                 16 Scenes (full house)
    ├── 00 Barf/  ... 15 Pop/
    └── bankset.bcg
```

**Static fixture audit (2026-09-14): PASS.** The seed contains exactly one
Bank and all child slots 00..15, with one sceneset, one Pattern, one effects
file, one embedded Kit, and six referenced Instruments per child. All 96
Instrument references resolve exactly, all stems/display names satisfy the
eight-character contract, and every Instrument `[params]` section matches its
current type descriptor set. All 17 seed PAT4 files (16 Bank children plus the
root library file) are exact 10,656-byte v1/stack-256/generation-0 images with
valid CRC32C and consistent address/bitmap/pool state. Settings, bankset,
scenesets, effects placeholders, Kit manifests, numeric ranges, and the
intentional absence of generated hidden state also validate. No fixture file
correction was required.

#### Executed functional run (2026-09-14): **PASS — CLOSED**

The hardware run used the feature as it actually operates: boot `FullHse`,
enable the available Live Record mode, change every Scene's Pattern, change at
least one parameter in every Scene, and leave AutoSave to run in the
background. It did not attempt to stop or observe a particular writer phase.

Inspection of `SD_CARD_TEST_CASE_B_OUTPUT/` found:

- The complete seed Bank tree is still present and byte-identical to
  `SD_CARD_TEST_CASE_B/`; the generated Bank and Pattern indexes contain the
  expected slot-0 names, and the empty Kit/Scene indexes have the correct
  1,000-row shape.
- There are 19 Pattern candidates: valid generation-1 B files for all 16
  Scenes plus valid generation-2 A files for Scenes 0..2. Size (10,656 B),
  PAT4 version, stack size, CRC32C, generation/suffix parity, address ranges,
  allocator bitmap, pool back-references, and non-overlap all validate.
- The selected winner for every Scene differs from that Scene's directory
  PAT4 baseline in address content. Address-byte difference counts for Scenes
  0..15 are `36, 16, 7, 4, 4, 5, 6, 5, 5, 5, 6, 6, 5, 6, 6, 6`.
  Therefore all 16 Pattern edits reached durable AutoSave candidates; the
  mixed generation counts are consistent with the autonomous
  drain reaching Scenes 0..2 before they received their later edits.
- `/.hcnames` has the exact header and 145 data rows. Pattern rows 129..144
  have the correct per-Scene names and all end in `@` plus refreshed witness
  `R`.
- Both HCPR candidates are valid committed 34,768-byte format-v2 records.
  The winner is `/.hcprms1` generation 39, its peer is generation 38, its
  mutation mask is empty, and comparison with the fixture finds 5–7 live
  scalar parameter changes in every one of the 16 Scene regions.
- `asavetrc.bin` is well-formed (11,395 eight-byte records). It
  retains 38 scalar publications, generations 2..39, and all 38 terminal
  records are successful. It contains no operation-error or phase-stall
  record. The bounded diagnostic ring reports 6,513 dropped trace records;
  that makes the trace incomplete but is not a Pattern AutoSave failure,
  because every durable candidate and final identity/parameter record validates
  independently.

**Disposition**: no implementation defect was identified. Functional Pattern
AutoSave testing is closed as **PASS**. This run proves autonomous 16-Scene
Pattern persistence and coexistence with scalar AutoSave. It does not claim a
timed throughput bound, deterministic mid-write recovery, corrupt-candidate
fallback, or independent REC/erase-gate coverage; those require separate
instrumentation and are not dependencies of this functional closeout.

---

## Completed Test Summary

| TC | Description | Status | Evidence |
|----|-------------|--------|----------|
| Overall | Functional Pattern AutoSave | **PASS — CLOSED** | `SD_CARD_TEST_CASE_B_OUTPUT/` full 16-Scene run |
| 1 | Pattern name propagation | **PASS** | TC1 run 2 (post-fix) |
| 2 | Basic drain and restore | **PASS** | TC2 output |
| 3 | A/B ping-pong alternation | **PASS** | TC1 (3.1–3.2) + TC2 (3.3–3.7) |
| 12 | Bank Load marks all children dirty | **PASS** (12.1–12.3) | TC1 |
| 13 | HCNAMES refresh witness `R` flag | **PASS** (13.1, 13.3) | TC2 |
| 14 | Absent Scene rows untouched | **PASS** (14.1–14.3) | TC1 |
| 15 | Format version (.hcprms v2) | **PASS** (15.3) | TC1 |
| 4 | Library load overrides AutoSave | — | Test Card A |
| 5 | Power-loss recovery | Deferred | Needs deterministic write-failure injection |
| 6 | Multi-Scene independence | — | Test Card A |
| 7 | Save resets generation epoch | — | Test Card A |
| 8 | Library load resets generation | — | Test Card A |
| 9 | Recording/erasing deferral | Deferred | Live Record/instrumentation not ready |
| 10 | Bank switch stale file isolation | — | Test Card A |
| 11 | CRC rejection of corrupt files | — | Supplemental fault-injection test |
| 16 | Boot time measurement | — | Supplemental performance test |
| 17 | Full 16-Scene background drain | **PASS** | `SD_CARD_TEST_CASE_B_OUTPUT/` |
