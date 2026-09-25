# S070 — T2(a) Pattern Load Generation Fix: Implementation Plan

Session: S070 · Branch: `dev-ph5-effects` · 2026-09-25.
Source: T2(a) FAIL in `S070_PHASE4_TESTING_FINAL.md`, predicted by F4 in §2.

---

## 1. Defect summary

After an explicit Pattern load (library, Scene, or Bank), the next boot
restores the pre-load Pattern instead of the loaded one.

**Root cause:** every Pattern load path resets `fs_pattern_generation[scene]`
to zero. The next AutoSave drain writes the loaded Pattern to a hidden file
at generation 1. But the pre-load hidden file (the other A/B half) still
carries a higher generation from the previous epoch. At boot, the reader
picks the higher generation and restores the stale Pattern.

**Observed:** T2(a) loaded `002 blankPat.pat` into a Scene. After reboot,
the Scene showed the pre-load Pattern. Card evidence in `SD_CARD_T2A`.

---

## 2. Current code path (why it fails)

### Pattern library load — `filesystem_loadPattern_tick()` completion

```
filesystem_loadPattern_tick()                filesystem.c:14688
  (phase 14, success path)
    memcpy(target, source, ...)              filesystem.c:14830  ← Pattern in RAM
    fs_pattern_generation[si] = 0u;          filesystem.c:14835  ← RESET TO ZERO
    autosave_markPatternDirty(si);           filesystem.c:14836
    bank_invalidateSdCleanScene(si);         filesystem.c:14837
  filesystem_startPatternHcnamesUpdate()     filesystem.c:14841
    → HCNAMES Pattern row source = library slot (not @)
```

### Scene/Bank load — `presetManager.c` completion callbacks

```
on_scene_load_complete()                     presetManager.c:486
  filesystem_resetPatternAutosaveGeneration(scene_index)   ← presetManager.c:495
                                             filesystem.c:23541  → gen = 0
  autosave_markSceneWithPatternDirty(scene_index)          ← presetManager.c:496

on_bank_load_complete()                      presetManager.c:539
  filesystem_resetPatternAutosaveGeneration(scene_index)   ← presetManager.c:549
                                             filesystem.c:23541  → gen = 0
  autosave_markSceneWithPatternDirty(scene_index)          ← presetManager.c:550
```

### First drain after load

```
filesystem_autosavePatternDrain…()           filesystem.c:24477
  generation = fs_pattern_generation[scene] + 1u  → 0 + 1 = 1
  fs_pattern_generation[scene] = 1u          filesystem.c:24480
  filename: .patNNa (generation 1 is odd? no: 1 & 1 = 1 → file b)
    Actually: generation parity selects A/B.
    1 is odd → file b. The old file b has gen 5+ from the previous epoch.
    Overwrite: file b now has gen 1.
    Old file a still has gen 4 (or whatever the previous epoch wrote).
```

Wait — parity: `dst[6] = (generation & 1u) ? 'b' : 'a'` (line 2873).
Gen 1 → file b. Gen 2 → file a.

So after gen-0 reset and one drain:
- File b: gen 1 (loaded Pattern)
- File a: gen 4 from pre-load epoch (stale)
- Boot: gen 4 > gen 1 → file a wins. **STALE PATTERN RESTORED.**

After a second drain:
- File a: gen 2 (loaded Pattern, overwrites old gen 4)
- File b: gen 1 (loaded Pattern)
- Boot: gen 2 > gen 1 → file a wins. Correct.

Two full drains are needed for the loaded Pattern to survive. The maximum
drain latency is 5 s, so the window is up to 10 s. Power-off within that
window restores the pre-load Pattern.

### Boot reader — `filesystem_patternAutosaveBootReaderBlocking()`

```
filesystem_patternAutosaveBootReaderBlocking()  filesystem.c:27961
  for each scene:
    fs_pattern_generation[scene] = 0u           filesystem.c:27973  ← init
    scan .patNNa and .patNNb:
      pick winner = highest valid generation     filesystem.c:27989
    if row source != @ (PATTERN_AUTOSAVE):
      fs_pattern_generation[scene] = 0u         filesystem.c:28002  ← ALSO ZERO
      continue  → hidden files ignored, Bank/library Pattern used
    if row source == @:
      fs_pattern_generation[scene] = winner_gen filesystem.c:28005
      load winner into resident region
```

The non-`@` branch (line 28002) also resets to zero. This creates a second
vector: after a load, the HCNAMES row is non-`@` (library slot or INHERIT).
At boot, the reader correctly ignores hidden files and uses the directory
source. But it sets generation to 0. If the user then edits the Pattern
(without another load), the first drain writes gen 1 and sets the row to
`@`. The next boot compares gen 1 against the old hidden files (gen 4+).
Gen 4 wins. The edit is lost and the pre-load Pattern is restored.

---

## 3. Fix

**Principle:** never reset `fs_pattern_generation[]` to zero at load time.
Let it continue from its current value, which is always the highest valid
generation on the card (set by the boot reader). The next drain increments
it, producing a generation higher than any existing candidate.

### Three sites to change

| Site | File | Line | Current | Fix |
|------|------|------|---------|-----|
| A | `filesystem.c` | 14835 | `fs_pattern_generation[si] = 0u` | Remove the assignment |
| B | `filesystem.c` | 23541 | `fs_pattern_generation[scene_index] = 0u` | Remove the assignment (inside `filesystem_resetPatternAutosaveGeneration`) |
| C | `filesystem.c` | 28002 | `fs_pattern_generation[scene] = 0u` | `fs_pattern_generation[scene] = winner_generation` |

### What stays unchanged

- **`memset(fs_pattern_generation, 0, ...)` at lines 23361 and 23431:** these
  are card-mount and facade-destroy initialization. The boot reader
  repopulates valid winners afterwards. No change needed.

- **`fs_pattern_generation[scene] = 0u` at line 27973:** per-scene init at
  the top of the boot reader loop body. Overwritten by either line 28002
  (non-`@`, now `= winner_generation`) or line 28005 (`@`, already
  `= winner_generation`). If no valid candidates exist, it stays at 0,
  which is correct — there are no old files to compete with.

- **`autosave_markPatternDirty(si)` / `autosave_markSceneWithPatternDirty()`
  at all load completion sites:** still needed. The loaded Pattern must be
  drained to a hidden file.

- **`bank_invalidateSdCleanScene(si)` at library load completion:** still
  needed. The loaded Pattern is not the Bank child's Pattern.

- **Drain generation increment at lines 24133 and 24477:** unchanged.
  `generation = fs_pattern_generation[scene] + 1u` now produces the
  boot reader's winner generation + 1, which is strictly greater than both
  existing candidates.

- **`autosave_generationIsNewer()` at line 2178 (`Autosave.c`):** uses
  signed difference `(int32_t)(candidate - reference) > 0`. Correct for
  the new behavior: the new generation is always numerically greater than
  the old, not wrapping.

---

## 4. Verified code state

All line numbers verified against `dev-ph5-effects` at commit `2f5b3d2`.

| Symbol | File | Line(s) | Kind |
|--------|------|---------|------|
| `fs_pattern_generation[]` | `filesystem.c` | 1826 | static uint32_t[16] |
| `filesystem_loadPattern_tick()` | `filesystem.c` | 14688–14847 | static fn |
| `filesystem_resetPatternAutosaveGeneration()` | `filesystem.c` | 23538–23542 | public fn |
| Declaration | `filesystem.h` | 376 | public decl |
| Call from Scene Load | `presetManager.c` | 495 | load completion |
| Call from Bank Load | `presetManager.c` | 549 | load completion |
| Boot reader init per-scene | `filesystem.c` | 27973 | loop body init |
| Boot reader non-`@` branch | `filesystem.c` | 28000–28003 | conditional |
| Boot reader `@` branch | `filesystem.c` | 28005 | assignment |
| Semantic drain gen increment | `filesystem.c` | 24477–24480 | drain start |
| Non-semantic drain gen increment | `filesystem.c` | 24133–24136 | drain start |
| A/B filename parity | `filesystem.c` | 2873 | `(gen & 1) ? 'b' : 'a'` |
| Generation in PAT4 header | `filesystem.c` | 2886–2893 | bytes 10..13 LE |
| Boot reader candidate compare | `filesystem.c` | 27989 | `generation > winner_generation` |
| `autosave_generationIsNewer()` | `Autosave.c` | 2178–2182 | signed diff |

---

## 5. Change list

Three changes in `filesystem.c`, one in `filesystem.h` (contract comment
update). No RAM change, no new public API, no file format change.

---

### Change 1 of 3 — Remove generation reset at library Pattern load completion

**Operation:** MODIFY `filesystem_loadPattern_tick()`.
**File:** `Core/Hardware/SD/filesystem.c`
**Location:** Line 14835, inside phase 14 success path.

#### Current code (lines 14832–14837)

```c
                /* A root-library Pattern replacement starts a new hidden-file
                 * generation epoch and must be durably re-captured before its
                 * old AutoSave pair can be considered authoritative. */
                fs_pattern_generation[si] = 0u;
                autosave_markPatternDirty(si);
                bank_invalidateSdCleanScene(si);
```

#### New code

```c
                /*
                 * The loaded Pattern must be durably re-captured before its
                 * old AutoSave pair can overwrite it. The generation is NOT
                 * reset: the boot reader seeded it to the highest existing
                 * candidate, so the next drain produces a strictly higher
                 * value and the loaded Pattern wins on the next boot. A
                 * reset to zero would leave the first drain at generation 1,
                 * losing to any prior candidate at generation >= 2.
                 */
                autosave_markPatternDirty(si);
                bank_invalidateSdCleanScene(si);
```

#### What changed

| Before | After |
|--------|-------|
| `fs_pattern_generation[si] = 0u;` | Removed |
| Comment says "starts a new epoch" | Comment explains why reset is NOT done |

---

### Change 2 of 3 — Remove generation reset in `filesystem_resetPatternAutosaveGeneration()`

**Operation:** MODIFY function body and rename.
**File:** `Core/Hardware/SD/filesystem.c`
**Location:** Lines 23538–23542.

This function is called from `presetManager.c` at Scene Load (line 495) and
Bank Load (line 549) completion. Both sites also call
`autosave_markSceneWithPatternDirty()` immediately after, so the dirty
marking is already handled.

#### Current code (lines 23530–23542)

```c
/*
 * What: starts the next hidden-file drain for the selected Scene at
 * generation 1 and target `.pat00a`-style file A. Inputs: a successfully
 * committed library/Scene/Bank Pattern load. Output: one filesystem-owned
 * generation baseline is cleared; no file I/O occurs. Affiliates: Preset
 * Scene/Bank load completion and the Pattern drain scheduler.
 */
void filesystem_resetPatternAutosaveGeneration(uint8_t scene_index)
{
    if (scene_index < SCENE_COUNT && scene_index < 16u)
        fs_pattern_generation[scene_index] = 0u;
}
```

#### New code

```c
/*
 * Acknowledge a Pattern replacement so the next drain captures it durably.
 *
 * What: invalidates the card-clean authority for this Scene's Pattern so
 * the AutoSave drain scheduler knows the resident region has diverged from
 * whatever the on-card child holds. Why: after a Scene/Bank load, the
 * resident Pattern is new content that must be written to a hidden file
 * before the next boot. The generation is NOT reset: the boot reader
 * seeded it to the highest valid candidate's value, so the next drain
 * produces a strictly higher generation and the loaded Pattern wins over
 * any pre-existing A/B pair. A reset to zero would leave the first drain
 * at generation 1, losing to any prior candidate at generation >= 2.
 *
 * Inputs: resident Scene index. Output: sd-clean invalidated; generation
 * unchanged. The caller marks the Scene dirty for drain separately.
 * Affiliates: presetManager.c Scene/Bank load completion,
 * bank_invalidateSdCleanScene(), and the Pattern drain scheduler.
 */
void filesystem_patternAutosaveOnLoad(uint8_t scene_index)
{
    if (scene_index < SCENE_COUNT && scene_index < 16u)
        bank_invalidateSdCleanScene(scene_index);
}
```

#### Associated changes

**`Core/Hardware/SD/filesystem.h` (line 376):** rename declaration and
update contract comment.

Current:

```c
/*
 * What: starts the next hidden-file drain for the selected Scene at
 * generation 1 and target `.pat00a`-style file A. Inputs: a successfully
 * committed library/Scene/Bank Pattern load. Output: one filesystem-owned
 * generation baseline is cleared; no file I/O occurs. Affiliates: Preset
 * Scene/Bank load completion and the Pattern drain scheduler.
 */
void filesystem_resetPatternAutosaveGeneration(uint8_t scene_index);
```

New:

```c
/*
 * Acknowledge a Pattern replacement for AutoSave continuity.
 *
 * What: invalidates sd-clean authority so the drain scheduler knows the
 * resident Pattern has diverged. The hidden-file generation is NOT reset:
 * the next drain produces a value one higher than the boot reader's winner,
 * ensuring the loaded Pattern beats any pre-existing A/B pair. Inputs: a
 * Scene index whose Pattern was just replaced by a load. Output: sd-clean
 * invalidated; no generation change, no file I/O. Affiliates: Preset
 * Scene/Bank load completion, bank_invalidateSdCleanScene(), and the
 * Pattern drain scheduler.
 */
void filesystem_patternAutosaveOnLoad(uint8_t scene_index);
```

**`Core/Bank/Scene/Preset/presetManager.c` (lines 495 and 549):** rename
call sites.

Line 495 (Scene Load completion):

```c
                /* A directory-backed Pattern replacement starts its own
                 * hidden AutoSave generation epoch before dirty marking. */
                filesystem_resetPatternAutosaveGeneration(scene_index);
```

→

```c
                filesystem_patternAutosaveOnLoad(scene_index);
```

Line 549 (Bank Load completion):

```c
                /* The loaded Bank child owns a fresh Pattern source; its next
                 * AutoSave image must begin at generation 1/file A. */
                filesystem_resetPatternAutosaveGeneration(scene_index);
```

→

```c
                filesystem_patternAutosaveOnLoad(scene_index);
```

---

### Change 3 of 3 — Seed generation from winner in boot reader non-`@` branch

**Operation:** MODIFY `filesystem_patternAutosaveBootReaderBlocking()`.
**File:** `Core/Hardware/SD/filesystem.c`
**Location:** Line 28002, inside the non-`@` branch.

#### Current code (lines 27997–28003)

```c
        /* A directory/library source deliberately ignores stale hidden files;
         * also discard their generation so the replacement's next drain
         * starts a fresh A/B epoch rather than continuing unrelated data. */
        if (filesystem_residentSource(filesystem_residentPatternRow(scene)) !=
            FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE) {
            fs_pattern_generation[scene] = 0u;
            continue;
        }
```

#### New code

```c
        /*
         * A directory/library source is authoritative: do not load hidden
         * files. Seed the generation from the winner so the next drain
         * produces a value strictly higher than any existing candidate.
         * Without this seed, a drain after a load or edit would write
         * generation 1, which loses to a pre-existing candidate at
         * generation >= 2 once the HCNAMES row transitions to @.
         */
        if (filesystem_residentSource(filesystem_residentPatternRow(scene)) !=
            FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE) {
            fs_pattern_generation[scene] = winner_generation;
            continue;
        }
```

#### What changed

| Before | After |
|--------|-------|
| `fs_pattern_generation[scene] = 0u` | `fs_pattern_generation[scene] = winner_generation` |
| Comment says "discard generation, fresh epoch" | Comment explains the seed and what breaks without it |

---

## 6. How the fix resolves each scenario

### Scenario A — Library Pattern load, power off after one drain

Before fix:
1. Load Pattern. Gen reset to 0. Dirty marked.
2. Drain fires: gen 0+1=1, writes file b (odd). Old file a has gen 4.
3. Power off. Boot: row is `@` (drain set it). Gen 4 > gen 1. File a wins.
   **Stale Pattern restored.**

After fix:
1. Load Pattern. Gen stays at 5 (boot reader's winner). Dirty marked.
2. Drain fires: gen 5+1=6, writes file a (even). Old file b has gen 5.
3. Power off. Boot: row is `@`. Gen 6 > gen 5. File a wins.
   **Loaded Pattern survives after one drain.**

### Scenario B — Bank Load, power off after one drain

Identical to A. `filesystem_patternAutosaveOnLoad()` no longer resets gen.
The next drain writes gen (boot winner + 1), which beats both old files.

### Scenario C — Boot with non-`@` row, edit, drain, power off

Before fix:
1. Boot: row is library slot (non-`@`). Old files have gen 4/5. Gen set to 0.
2. User edits Pattern. Drain fires: gen 0+1=1, writes file b. Row → `@`.
3. Power off. Boot: row is `@`. Gen 5 (old file a) > gen 1. **Stale restore.**

After fix:
1. Boot: row is library slot. Old files have gen 4/5. Gen set to 5 (winner).
2. User edits. Drain fires: gen 5+1=6. Row → `@`.
3. Power off. Boot: gen 6 > gen 5. **Edit survives.**

### Scenario D — No prior hidden files

1. Boot: no valid candidates. `winner_valid = 0`. Loop continues at line
   27996. `fs_pattern_generation[scene]` stays at 0 (line 27973 init).
2. Load or edit. Drain fires: gen 0+1=1. No old files to compete with.
3. Boot: gen 1 is the only candidate. **Correct.**

### Scenario E — Power off between load and first drain

1. Load Pattern. HCNAMES row is library slot (non-`@`). Gen stays at 5.
2. Power off before drain fires. No new hidden file written.
3. Boot: row is non-`@`. Boot reader loads from library directory. Gen
   seeded to winner_generation of old files (gen 5). Hidden files ignored.
4. User does nothing. Drain fires: gen 5+1=6. Row → `@`.
5. Power off. Boot: gen 6 > gen 5. **Loaded Pattern survives.**

---

## 7. Files changed

| File | Changes |
|------|---------|
| `Core/Hardware/SD/filesystem.c` | Remove gen reset at line 14835; rewrite `filesystem_resetPatternAutosaveGeneration` → `filesystem_patternAutosaveOnLoad` at line 23538; seed gen from winner at line 28002 |
| `Core/Hardware/SD/filesystem.h` | Rename declaration and contract block at line 370–376 |
| `Core/Bank/Scene/Preset/presetManager.c` | Rename two call sites at lines 495 and 549 |

No new RAM. No new public API signatures (one rename). No file format
change — the PAT4 header generation field (bytes 10..13) is unchanged. No
PatternData, SD layout, or HCNAMES format change.

---

## 8. Detailed change map

```
Core/Hardware/SD/filesystem.c
│
├── Change 1: MODIFY filesystem_loadPattern_tick() phase 14
│   Location: line 14835
│   Operation: remove fs_pattern_generation[si] = 0u
│   Dependencies: none
│
├── Change 2: MODIFY filesystem_resetPatternAutosaveGeneration()
│   Location: lines 23530–23542
│   Operation: rename to filesystem_patternAutosaveOnLoad(),
│              replace gen=0 with bank_invalidateSdCleanScene(),
│              rewrite comment block
│   Dependencies: filesystem.h rename, presetManager.c rename
│
└── Change 3: MODIFY filesystem_patternAutosaveBootReaderBlocking()
    Location: line 28002
    Operation: replace 0u with winner_generation
    Dependencies: none

Core/Hardware/SD/filesystem.h
│
└── Rename declaration + contract at line 370–376
    Dependencies: Change 2

Core/Bank/Scene/Preset/presetManager.c
│
├── Line 495: rename call
└── Line 549: rename call
    Dependencies: Change 2
```

---

## 9. Implementation order

1. **Change 2 + filesystem.h + presetManager.c** — rename the function and
   its declaration/call sites together. Compile-check: no unresolved symbol.

2. **Change 1** — remove the inline gen reset at library load completion.
   Compile-check: clean.

3. **Change 3** — seed from `winner_generation` in the boot reader.
   Compile-check: clean. `winner_generation` is already a local declared at
   line 27970 and populated by the candidate loop.

All three changes form one logical fix and should be committed together.

---

## 10. Test plan

After applying all changes, rebuild and flash.

### T2(a) retest

1. Boot with AutoSave ON. Let it converge (multiple drains across several
   Scenes — wait 2 min or check trace for quiet tail).
2. Load `002 blankPat.pat` into Scene 5 via Load:[Pattern].
3. Wait 15 s for the drain to fire.
4. Power off. Copy card → `SD_CARD_T2A_FIX`.
5. Reboot. Check Scene 5: step LEDs should be all off (blank Pattern).
6. Repeat with a power-off at ~3 s after load (before drain completes) to
   test Scenario E.

### Regression: normal AutoSave Pattern cycle

7. Boot. Edit a Pattern (toggle steps, change automation). Let AutoSave
   converge. Power off. Reboot. Verify edits survived.

### Regression: Bank Load

8. Load Bank 025 "NoBankMd" (or any Bank). Verify it loads. Wait for drain.
   Power off. Reboot. Verify the loaded Bank's Patterns are present.

### Pass criteria

- T2(a) retest: loaded Pattern persists after one drain cycle (step 5).
- Scenario E: loaded Pattern persists even with short wait (step 6).
- Steps 7–8: no regression in normal AutoSave or Bank Load paths.
- No `E` or `X` trace records.

## 11. Implementation work log

### 2026-09-25 — Source fix applied

- Removed the generation reset at root-library Pattern-load completion in
  `Core/Hardware/SD/filesystem.c`. The load still marks the Pattern dirty and
  invalidates Bank child clean authority, but the next drain now increments
  the existing monotonic generation baseline.
- Replaced `filesystem_resetPatternAutosaveGeneration()` with
  `filesystem_patternAutosaveOnLoad()` in `filesystem.c`/`filesystem.h` and
  both Scene/Bank completion callbacks in `presetManager.c`. The helper now
  invalidates sd-clean authority without changing the generation; the caller
  continues to issue the Pattern dirty mark.
- Changed the boot reader's non-`@` branch to seed
  `fs_pattern_generation[scene]` from the highest valid hidden candidate while
  continuing to ignore that candidate's payload. This preserves the directory
  or library source while preventing the first later AutoSave drain from
  losing to an older hidden file.
- Added adjacent rationale blocks at each changed implementation call/site
  and updated the public contracts in `filesystem.h`.
- Source-only checks completed: affected-file `git diff --check` is clean;
  the old helper symbol has no remaining references in source. Repository-wide
  `git diff --check` still reports trailing spaces in pre-existing modified
  SD-card fixture index files, which were not changed.
- Full ARM build and image generation completed with the project toolchain:
  `text=455,652`, `data=416`, `bss=291,804`,
  `build/LXRV2_lxr02.img` = 456,084 bytes. The build emitted only existing
  unused-function warnings in `filesystem.c` and standard bare-metal linker
  syscall warnings; no T2A-related errors or warnings appeared.
- Hardware retest remains pending: flash the new image and repeat the T2(a)
  one-drain and short-wait scenarios, then run the normal Pattern and Bank
  load regressions from §10.
