# Empty-Scene Overwrite Guard — Implementation Plan (Rev 1)

Status: **implemented.** Depends on P1
(`S057_AUTOSAVE_WRITER_WRAP.md` §2) — the guard proposed here reads
`bank_scenePresentMask()` as its "is this Scene real" signal, and P1 is
what makes that mask trustworthy again after a partial Bank Save.

**Mandate, as given:** an empty Scene must never be saved over an occupied
Scene in the Library. This must be enforced at the actual save/write layer,
not only by the SEQ LED selection UI. Root Scene Save must refuse (silently
fail) an attempt to save an empty resident Scene into any slot. The SEQ LED
toggle-selection mask needs to be checked for correctness as a secondary
question, since it's the current (UI-level) line of defense.

---

## 0. What changed since the initial plan

- **Q1 resolved (§3):** silently exclude empty Scenes from the save mask,
  don't refuse the whole Bank Save. Empty scenes are already shown by
  non-lit LEDs on the SEQ buttons during save, so the user can see which
  ones are excluded. The mask is filtered before any delete or write phase.
- **Q3 resolved (§3):** root Scene Save silently fails (`FS_STATUS_ERROR`)
  when the source Scene is empty. This is defense-in-depth — the UI should
  eventually make it impossible to even attempt this, but the filesystem
  guard is there regardless.
- **Q4 resolved (§3):** guard is limited to the Scene level only.
  KitMrp/InstrumentMrp-style partial/morph saves are out of scope — they
  don't create/replace a Scene identity.
- **Q2 resolved (§1.2):** boot-seeded Scene 0 is treated as empty. Both
  guards also check `bank_hasResidentBank()` — until a real Bank Load or
  Save completes, the boot-seeded present-bit on Scene 0 is not trusted.

---

## 1. What "empty" means today — there is no formal definition

Confirmed (also noted in `AUTOSAVE_READ_PLAN.md §10`): there is no
`scene_isEmpty()` or equivalently-named helper anywhere in `SceneData.h/.c`
or `filesystem.c`. The closest existing proxy is `bank_scenePresentMask()`
(`BankData.c:192`), and it turns out to be a reasonably principled
signal, not an arbitrary one — its only setters are:

- `filesystem.c:10837` — Bank Load, unioned in for every successfully
  loaded child (correct).
- `filesystem.c:10685` — Bank Load's empty-Bank branch, preserves the
  existing mask unchanged.
- `filesystem.c:14126` — Bank Save, **the P1 bug**: assigns the save
  subset directly instead of unioning.
- `presetManager.c:104` (`preset_markRequestedScenesPresentOnSuccessfulLoad()`)
  — Kit/Scene/Instrument loads mark their destination Scene present, gated
  on `filesystem_status() == FS_STATUS_DONE`. Explicitly documented as
  running only on success; failed loads don't touch BankData.
- `presetManager.c:1814` — a committed root Instrument write marks its
  destination Scene present at the same point it writes
  `scene->kit.instruments[slot] = *staged`.

So today, a Scene's present-bit is only ever set by a **real, completed**
Load or Instrument commit — not by mere navigation/selection. That's a good
foundation for "has this Scene received real content." Two things weaken
it, both must be resolved before treating it as authoritative:

1. **P1** (see wrap doc §2) — Bank Save can currently *clear* present-bits
   for Scenes that are still genuinely resident, corrupting the very signal
   this guard would depend on. Must be fixed first.
2. **Boot-time seed** — see §1.2 below.

### 1.2 Boot-seeded Scene 0 — concrete detail for decision

`bank_init()` (`BankData.c:99-117`) runs at every boot and unconditionally
sets `bank_scene_mask_present = 1u` — marking Scene 0 as present. At this
point:

- **Scene 0's SRAM content** is BSS-zero: every field in `scenes[0]`
  (SceneData.c:5, `scene_t scenes[SCENE_COUNT]`) holds its C static
  initializer (zeros). No `scene_init()` exists — the array is never
  explicitly initialized beyond what C gives it.
- **`bank_has_resident_bank`** is `0` — no Bank has been loaded yet.
- **The present-bit was not set by a real Load or Instrument commit** — it
  was set by `bank_init()` as part of the product default non-Bank state
  (the comment at `BankData.c:101-108` says this explicitly: "so a card
  with no valid Bank can fall back to root Scene, root Kit, or defaults
  without pretending a Bank is resident").

**When this state persists into user-reachable Save paths:** only when the
card has no valid Bank to load at boot (no `/Bank/` on card, or mount
failure, or the autosave reader's Case-3 invalidation leaves Scene 0 empty
in a future phase). In normal operation with a valid card, Bank Load
replaces the boot seed with real present-bits before the user ever reaches
Save.

**The question for you:** should the guard treat boot-seeded Scene 0 as
"present" (allowing it to be saved even though it contains only
zeros/defaults), or should it also check `bank_hasResidentBank()` as a
secondary gate — refusing any Scene as empty until at least one real
Load/commit has occurred this session?

The practical difference: without `bank_hasResidentBank()`, a user who
boots with no card content and immediately saves would write BSS zeros into
the Library. With it, that save is blocked until something real is loaded.
The present-bit alone is already the right answer for every case *after*
boot — this is only about the initial-boot window.

**Decision:** check `bank_hasResidentBank()` as a secondary gate. A Scene
is non-empty only when both (a) its present-bit is set AND (b)
`bank_hasResidentBank()` is true. Before a real Bank Load or Save
completes, every Scene is treated as empty regardless of the boot seed.
Both guards (Bank Save mask filter and root Scene Save case 0) implement
this. One extra byte-check, no new complexity.

---

## 2. Where the destructive step actually happens — confirmed, and it changes where the guard must go

Traced both save state machines. In each, the existing entry-validation
phase (`case 0`) is followed a few phases later by an **unconditional
whole-object delete** of whatever currently occupies the target slot,
which runs *before* any new content is written:

- **`filesystem_saveBankDirectory_tick()`** (`filesystem.c:13692`): `case 0`
  validates inputs and rejects early; `case 4` (`filesystem.c:13861`) calls
  `filesystem_deleteSlotDirectoryStart(op_slot, 0u)` — deletes the *entire*
  existing target Bank folder (all previously-saved children, confirmed by
  the code's own comment at `filesystem.c:13988-13989`, "exact replacement
  already removed the previous Bank tree"). Only *after* that does the
  per-child write loop (`case 11+`) run, writing only the Scenes selected
  in `op_bank_scene_save_mask`.
- **`filesystem_saveSceneDirectory_tick()`** (`filesystem.c:14255`): same
  shape — `case 0` validates, `case 4` (`filesystem.c:14320`) calls
  `filesystem_deleteSceneSlotDirectoryStart()` to remove the existing
  same-numbered `/Scene/NNN .../` tree, before any new file is written.

**Consequence for this plan:** a guard added anywhere at or after `case 4`
is too late — the old occupied data is already gone by the time the write
phases even start. The guard must complete during `case 0` (or between 0
and 4), before the delete is ever reached, for both functions.

**Confirmed out of scope / not a bug to fix:** a genuinely partial
`Save:[Bank]` (fewer than all resident Scenes selected) intentionally
deletes the unselected Scenes from the on-disk Bank — this is documented,
deliberate behavior (`filesystem_requestSaveBank()`'s own comment,
`filesystem.c:22043-22048`: "Save:[Bank] may intentionally save only a
subset of resident Scenes"), with `active_scene` relocation logic to match.
**This plan does not change that.** The mandate here is narrower and
different: even among the Scenes the user *did* select, none of them may
be empty when the destination they're about to overwrite is occupied.

---

## 3. Guard shape — resolved

### 3.1 Bank Save: silently exclude empty Scenes from the mask

**In `filesystem_requestSaveBank()`** (`filesystem.c:21987`), after
`op_bank_scene_save_mask` is captured and normalized (line 22007-22009),
filter the mask against `bank_scenePresentMask()`:

```c
op_bank_scene_save_mask =
    (uint16_t)(op_bank_scene_save_mask & bank_scenePresentMask());
```

This strips any empty (non-present) Scene from the save mask before the
state machine even starts. The existing per-child loop
(`case 11` at `filesystem.c:13969`) will simply never visit an excluded
Scene. If the resulting mask is `0u` (every selected Scene was empty), the
existing `case 11` already handles this: `op_bank_scene_save_mask == 0u`
skips the child loop and proceeds directly to finalization at `case 45`.

**Why this location, not `case 0`:** the mask is already being normalized
at the request-capture site (line 22007-22009 bounds it to
`STORAGE_BANK_SCENE_MAX_SLOTS` bits). Adding the present-mask filter here
keeps all mask conditioning in one place, before the state machine runs,
and before the `active_scene` relocation logic (line 22037-22058) that
also depends on the mask. Filtering *after* the active_scene relocation
would let an empty Scene become the active_scene, which is wrong.

**The user sees this:** empty Scenes are already shown by non-lit LEDs on
the SEQ buttons during save (via `menu_loadSaveSelectableSceneMask()` →
`bank_scenePresentMask()`). The filesystem-layer filter is defense-in-depth
— it ensures the same exclusion holds even if a future caller constructs a
mask that bypasses the Menu-side gate.

**Relationship to P1:** the present-mask filter is only correct once P1
lands. If P1 hasn't fixed the mask, a Scene that is genuinely resident
could be falsely excluded. **P1 must land first.**

### 3.2 Root Scene Save: silent fail

**In `filesystem_saveSceneDirectory_tick()` case 0** (`filesystem.c:14276`),
add one check after the existing `!scene || !kit` validation:

```c
if (!bank_scenePresent(op_kit_save_source_scene)) {
    filesystem_finish(FS_STATUS_ERROR);
    return;
}
```

This uses the existing single-Scene accessor `bank_scenePresent()`
(`BankData.c:197-201`) rather than the full mask, since root Scene Save
operates on exactly one source Scene.

**Silent-fail semantics:** `FS_STATUS_ERROR` is the existing, already-used
outcome for a case-0 rejection in both functions today (e.g., the
`!scene || !kit || op_slot >= ...` checks at `filesystem.c:14277-14281`
already return this). No new status code, no new error path. Menu receives
the same `FS_STATUS_ERROR` it would for any other case-0 rejection.

**Defense-in-depth:** this guard exists so that even if the UI is
eventually changed to prevent attempting the save, the filesystem layer
independently refuses. It cannot be bypassed by a different caller.

### 3.3 Not applied to: KitMrp/InstrumentMrp partial/morph saves

Per decision: the guard is limited to the Scene level. Morph-only saves
don't create or replace a Scene identity — they write into an existing
Scene's component slots. Out of scope.

---

## 4. SEQ LED toggle-selection mask — checked, mostly correct, one dependency

Traced the full mechanism (`menu.c`):

- `menu_loadSaveSelectableSceneMask()` (`menu.c:4745-4778`) returns
  `menu_residentPresentSceneMask()` (→ `bank_scenePresentMask()`, with a
  fallback to just the active Scene if the present mask is entirely zero)
  for both `Save:[Bank]` and every other Save type on the Save page.
- **LED lighting** (`menu_refreshLoadSceneLeds()`, `menu.c:4856`) is gated
  on `selected && (selectable_mask & bit)` — a Scene bit can only be lit if
  it's both toggled on *and* present.
- **Press-acceptance** (`menu_loadSceneButtonPressed()`, `menu.c:4907`) is
  gated identically: `if ((menu_loadSaveSelectableSceneMask() & bit) == 0u)
  return 1u;` — a press on a non-present Scene is consumed but produces no
  toggle.

So the mechanism is internally consistent: the same present-mask gates both
what can light and what can be toggled, for the same reason, in the same
place. **This is correctly wired for its own purpose.** Two caveats, both
already covered above, restated here because they directly answer "is the
mask working properly":

1. It's only as correct as `bank_scenePresentMask()` itself — currently
   corrupted by P1 after any partial Bank Save. Not a bug in the LED/toggle
   code; a bug in what it reads.
2. It's a **UI convenience gate**, not a data-integrity guarantee. Nothing
   stops a future caller of `filesystem_requestSaveBank()` /
   `preset_saveBank()` — a different UI surface, a test harness, a future
   automation feature — from constructing a mask that includes a
   non-present bit and calling the filesystem API directly, bypassing the
   Menu-side gate entirely. This is exactly why the mandate calls for the
   guard to live at the save layer (§3), not only in
   `menu_loadSceneButtonPressed()`/`menu_refreshLoadSceneLeds()`. Confirmed
   the modal is not, on its own, sufficient — matches the instruction.

**No correction needed in `menu.c` itself** beyond what P1 already fixes
upstream. The modal's logic is sound; it was never the actual guard, and
per the mandate it isn't supposed to be.

---

## 5. Relationship to `AUTOSAVE_READ_PLAN.md §10`

That document already flagged this exact gap ("Deferred: Bank Save
empty-Scene overwrite guard... Confirmed not implemented... explicitly out
of scope for this reader effort. Deferred to its own later cleanup/refactor
sub-phase, scheduled ahead of Pattern structure work.") This plan is that
deferred sub-phase, now being scoped for real. Once implemented, §10's
"deferred" note should be updated to point here rather than restating the
gap.

---

## 6. Open questions — all resolved

All four original open questions are resolved (see §0). No remaining open
questions.

---

## 7. Implementation — done

**Prerequisite:** P1 (`S057_AUTOSAVE_WRITER_WRAP.md` §2) must land first.
The present-mask this guard depends on is untrustworthy until P1 fixes the
Bank Save assignment bug at `filesystem.c:14126`.

### 7.1 `filesystem_requestSaveBank()` — mask filter (done)

At `filesystem.c:22052-22055`, after the existing mask bounds-clamp at
`filesystem.c:22027-22029`, added:

```c
bank_scene_save_mask =
    (uint16_t)(bank_scene_save_mask &
               (bank_hasResidentBank()
                    ? bank_scenePresentMask() : 0u));
```

This filters the mask before `filesystem_start()` and before the
active_scene relocation logic. When `bank_hasResidentBank()` is false
(boot state, no real Bank loaded), the entire mask is zeroed — the
boot-seeded present-bit on Scene 0 is not trusted.

### 7.2 `filesystem_saveSceneDirectory_tick()` case 0 — present check (done)

At `filesystem.c:14298-14302`, after the existing `!scene || !kit ||
op_slot >= ...` validation, added:

```c
if (!bank_hasResidentBank() ||
    !bank_scenePresent(op_kit_save_source_scene)) {
    filesystem_finish(FS_STATUS_ERROR);
    return;
}
```

Uses the same dual gate: `bank_hasResidentBank()` for boot-seed safety,
`bank_scenePresent()` for per-Scene emptiness.

### 7.3 Include for `bank_scenePresent()` / `bank_scenePresentMask()`

`filesystem.c` already includes `BankData.h` (confirmed by the existing
`bank_setScenePresentMask()` calls at lines 10685, 10837, 14126). No new
include needed.

### 7.4 No `menu.c` changes

The SEQ LED toggle mechanism is already correct (§4). The only thing that
fixes its behavior is P1 fixing the mask it reads — that's a Bank Save
change, not a Menu change.

### 7.5 Coverage of all entry paths — confirmed

- **Root Scene Save:** enters via `filesystem_requestSaveSceneDirectory()`
  → `filesystem_saveSceneDirectory_tick()` case 0. Guarded by §7.2.
- **Bank Save children:** enter via `filesystem_requestSaveBank()`. The
  mask is filtered by §7.1 before the state machine starts. The per-child
  writer enters `filesystem_saveSceneDirectory_tick()` at case 8 (not
  case 0), so §7.2 is not reached — but the mask filter at §7.1 already
  ensures no non-present Scene is visited.
- **No other entry points** for `FS_INTERNAL_OP_SAVE_SCENE` or
  `FS_INTERNAL_OP_SAVE_BANK` exist — confirmed by searching for both
  enum values in filesystem.c.

---

## 8. Test plan

1. **Root Scene Save from empty Scene → occupied slot:** boot with a valid
   Bank loaded, navigate away from Scene 0 to a Scene that was never loaded
   (its present-bit is 0). Attempt root Scene Save into an occupied library
   slot. Confirm: save returns `FS_STATUS_ERROR`, target slot is untouched.

2. **Root Scene Save from empty Scene → empty slot:** same setup, target an
   empty slot. Confirm: still refused — the mandate is about empty *source*,
   not empty *target*.

3. **Save:[Bank] with mixed present/empty Scenes:** load a Bank with
   Scenes 0, 1, 2 present. Toggle SEQ LEDs to select Scenes 0, 1, 2, 3
   (if Scene 3's LED can't be toggled because the Menu-side gate already
   prevents it, confirm that behavior, then bypass the Menu gate by calling
   `filesystem_requestSaveBank()` directly with bit 3 set in the mask).
   Confirm: the saved Bank contains only Scenes 0, 1, 2 — Scene 3 was
   silently excluded from the mask.

4. **Save:[Bank] with all-empty mask:** if possible, construct a save
   request where every selected Scene is empty. Confirm: Bank Save still
   completes (the `case 11` `op_bank_scene_save_mask == 0u` path runs),
   producing a Bank with bankset.bcg but no child Scene directories.

5. **Normal, fully-populated Save:[Bank]:** confirm no false-positive
   refusals — all present Scenes are saved as before.

6. **Normal root Scene Save:** confirm no false-positive refusal — a
   present Scene saves normally.

7. **SEQ LED behavior unchanged:** confirm toggle/light behavior matches
   pre-change behavior — this plan changes nothing in `menu.c`.

8. **Post-P1 regression:** after P1 lands, confirm a Scene dropped from a
   prior partial Bank Save's on-disk tree but still resident in SRAM is
   correctly toggleable again for the next save, and that the guard does
   not falsely exclude it.
