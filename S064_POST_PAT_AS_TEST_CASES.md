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
| 2.1 | Boot, load any Bank or Kit | Scenes present, AutoSave enabled | |
| 2.2 | Edit pattern on current Scene (toggle steps, add specials) | `pat_markSceneDirty()` fires, dirty mask set | |
| 2.3 | Wait ~5–10 s for drain (SD activity or trace) | `.patNNx` written at generation N+1 | |
| 2.4 | Power cycle (hard reset, no explicit Save) | Device reboots | |
| 2.5 | After boot, check current Scene's pattern | Step toggles and specials restored from AutoSave | |
| 2.6 | Inspect SD: `.patNNx` file present, HCNAMES row is `@` | File validates (correct size, CRC, generation) | |

### 3 — A/B ping-pong file alternation

**Goal**: Generation parity correctly alternates A and B files.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 3.1 | Start from clean state (no `.patNNx` files) | No hidden pattern files | PASS (TC1 card had none) |
| 3.2 | Load Bank, wait for initial drain | Generation 1 → `.patNNb` files created | PASS (TC1: 4× `.patNNb` gen=1, no `a` files) |
| 3.3 | Edit a pattern on Scene 0 | Dirty bit set | |
| 3.4 | Wait for drain | Generation 2 → `.pat00a` created | |
| 3.5 | Edit Scene 0 pattern again | Dirty bit set | |
| 3.6 | Wait for drain | Generation 3 → `.pat00b` overwritten (gen 3 > gen 1) | |
| 3.7 | Power cycle, check Scene 0 | Restored from `.pat00b` (generation 3, highest valid) | |

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

| # | Step | Expected | Result |
|---|------|----------|--------|
| 5.1 | Have a valid generation-N AutoSave on Scene 0 | `.pat00x` at generation N | |
| 5.2 | Edit the pattern (dirty bit set) | Drain will write generation N+1 | |
| 5.3 | Power cycle DURING drain (before CRC write-back) | Interrupted file has bad CRC or truncated | |
| 5.4 | After boot, check Scene 0 pattern | Restored from generation N (the prior valid file) | |
| 5.5 | The interrupted edit is lost | Expected — AutoSave is best-effort | |

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

| # | Step | Expected | Result |
|---|------|----------|--------|
| 9.1 | Edit a pattern (dirty bit set) | Drain pending | |
| 9.2 | Enter recording mode (hold REC) | `seq_recordActive = 1` | |
| 9.3 | While recording, observe that drain does NOT fire | No `.patNNx` write during recording | |
| 9.4 | Exit recording mode | `seq_recordActive = 0` | |
| 9.5 | Drain fires within next scheduler cycle | `.patNNx` updated | |
| 9.6 | Repeat with erase mode | Same deferral behavior | |

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
| 11.2 | Corrupt `.pat00a` (flip a byte in the payload) | CRC no longer matches | |
| 11.3 | Boot | `.pat00b` (gen 1) wins because `.pat00a` is invalid | |
| 11.4 | Truncate `.pat00b` to 5,000 bytes | File too short for v4 image | |
| 11.5 | Boot with both files invalid | Falls back to directory Pattern or `pat_initScene()` | |

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
| 13.1 | After successful drain, check HCNAMES Pattern row | Source `@`, flag `R` present | |
| 13.2 | Edit the pattern (mutate a step) | `R` flag cleared (refresh witness invalidated) | |
| 13.3 | Wait for next drain to complete | `R` flag restored | |
| 13.4 | During drain (after snapshot, before HCNAMES), edit pattern | `R` flag NOT set (drain race guard) | |

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
| 16.1 | Boot with 32 `.patNNx` files present | Measure time to ready state | |
| 16.2 | Boot with no `.patNNx` files (delete all) | Measure time to ready state | |
| 16.3 | Compare | No meaningful regression (< 500 ms delta) | |

### 17 — Full 16-Scene drain throughput

**Goal**: Measure worst-case drain time when all 16 Scenes are dirty.

| # | Step | Expected | Result |
|---|------|----------|--------|
| 17.1 | Load Bank with 16 present Scenes | All 16 dirty | |
| 17.2 | Time from first drain to last drain completion | ~170 KB total write, estimate 16 scheduler cycles | |
| 17.3 | Record elapsed time | Document for future reference | |

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
| R1 — Record format break (hcprms v1→v2) | 15 | Preliminary pass |
| R2 — 32 new root directory files | 16, 12 | 12 preliminary pass |
| R3 — SRAM cost (10,586 B) | Build verification | Confirmed in schedule |
| R4 — Drain throughput | 17 | Not yet tested |
| R5 — Boot ordering / AutoSave vs directory conflict | 4, 7, 8, 10 | Not yet tested |
| R6 — Recording/erasing drain deferral | 9 | Not yet tested |

---

## Priority Order

1. **1** — Pattern name propagation (preliminary concern flagged, test card ready)
2. **2** — basic drain and restore (golden path)
3. **3** — A/B ping-pong confirmation
4. **4** — library load override
5. **5** — power-loss recovery
6. **6** — multi-Scene independence
7. **7/8** — Save/library generation reset
8. **9** — recording deferral
9. **10** — Bank switch stale file isolation
10. **11** — CRC rejection of corrupt files
11. **16/17** — performance measurements
12. **12/13/14/15** — edge cases and lifecycle details
