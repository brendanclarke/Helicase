# Empty-Scene Overwrite Guard — Initial Plan

Status: **initial plan, not yet implemented.** No code yet. Depends on P1
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

## 1. What "empty" means today — there is no formal definition

Confirmed (also noted in `AUTOSAVE_READ_PLAN.md §10`): there is no
`scene_isEmpty()` or equivalently-named helper anywhere in `SceneData.h/.c`
or `filesystem.c`. The closest existing proxy is `bank_scenePresentMask()`
(`BankData.c:165-192`), and it turns out to be a reasonably principled
signal, not an arbitrary one — its only setters are:

- `filesystem.c:10753` — Bank Load, unioned in for every successfully
  loaded child (correct).
- `filesystem.c:10601` — Bank Load's empty-Bank branch, preserves the
  existing mask unchanged.
- `filesystem.c:14028` — Bank Save, **the P1 bug**: assigns the save
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
2. **Boot-time seed** — per `SCOPING_TARGETS.md`'s P1 discussion,
   `bank_init()` seeds the present mask to `1` (Scene 0 only) at boot,
   before any real load has necessarily happened. Whether a freshly-booted,
   never-loaded Scene 0 with factory/default content should count as
   "empty" or "not empty" for this guard's purposes is a product decision,
   not something inferable from the code. **Open question for the
   implementer/product owner**, flagged, not resolved here.

---

## 2. Where the destructive step actually happens — confirmed, and it changes where the guard must go

Traced both save state machines. In each, the existing entry-validation
phase (`case 0`) is followed a few phases later by an **unconditional
whole-object delete** of whatever currently occupies the target slot,
which runs *before* any new content is written:

- **`filesystem_saveBankDirectory_tick()`** (`filesystem.c:13594`): `case 0`
  validates inputs and rejects early; `case 4` calls
  `filesystem_deleteSlotDirectoryStart(op_slot, 0u)` — deletes the *entire*
  existing target Bank folder (all previously-saved children, confirmed by
  the code's own comment at `filesystem.c:13890-13891`, "exact replacement
  already removed the previous Bank tree"). Only *after* that does the
  per-child write loop (`case 11+`) run, writing only the Scenes selected
  in `op_bank_scene_save_mask`.
- **`filesystem_saveSceneDirectory_tick()`** (`filesystem.c:14157`): same
  shape — `case 0` validates, `case 4` calls
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
`filesystem.c:21620-21624`: "Save:[Bank] may intentionally save only a
subset of resident Scenes"), with `active_scene` relocation logic to match.
**This plan does not change that.** The mandate here is narrower and
different: even among the Scenes the user *did* select, none of them may
be empty when the destination they're about to overwrite is occupied.

---

## 3. Proposed guard shape

**For `filesystem_saveBankDirectory_tick()` case 0:** before accepting the
request, for every bit set in `op_bank_scene_save_mask`, confirm the
corresponding resident Scene is non-empty (once §1's open questions are
resolved, this reads as "is present in `bank_scenePresentMask()`").
Open design decision, not resolved here: if one selected Scene is empty
while others are fine, does the *whole* Bank Save refuse (safest — avoids
partially applying a whole-tree delete based on a partially-valid
selection), or does that one Scene get silently excluded from the mask
before the delete proceeds (keeps the rest of the save working, matches
"toggle it off" semantics more closely, but is a quieter and easier-to-miss
correction)? Recommend the former (whole-refuse) as the safer default given
this is explicitly called an "absolute mandate," but flagging for
confirmation rather than deciding unilaterally.

**For `filesystem_saveSceneDirectory_tick()` case 0:** simpler — this saves
exactly one resident Scene (`op_kit_save_source_scene`) into exactly one
target slot. Per the mandate's explicit instruction, if that source Scene
is empty, the save request should fail at case 0, before `afatfs_chdir`,
`mkdir`, or the delete step ever run — a true no-op refusal, not a
partially-completed operation.

**Silent-fail semantics:** "silently fail" needs a concrete definition at
implementation time. Proposed: the filesystem operation completes with
`FS_STATUS_ERROR` (the existing, already-used outcome for a case-0
rejection in both functions today, e.g. `filesystem.c:14177-14181`'s
existing `scene`/`kit`/slot/name checks) rather than any new status. What
Menu does with that — a passive return to the bracketed row versus a
visible message — is a UI decision out of scope for the filesystem-layer
guard itself; noted here so it isn't lost, not decided.

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

## 6. Open questions to resolve before implementation

1. Whole-Bank-refuse vs. silently-exclude-one-Scene when only some selected
   Bank Scenes are empty (§3).
2. Whether a never-loaded, boot-seeded Scene 0 counts as "empty" for this
   guard (§1).
3. What Menu should show the user on a silent-fail root Scene Save refusal
   (§3) — nothing, or a brief message.
4. Whether the guard should also apply to KitMrp/InstrumentMrp-style
   partial/morph saves, or is scoped to whole-Scene/whole-Bank saves only
   (this plan assumes the latter; morph-only saves don't create/replace a
   Scene identity the way full Scene/Bank saves do, so they're likely out
   of scope, but worth confirming explicitly).

---

## 7. Test plan (for the implementation session)

- Attempt a root Scene Save from a genuinely empty resident Scene (never
  loaded since boot) into both an empty and an occupied target slot;
  confirm refusal in both cases, confirm the occupied target is untouched.
- Attempt `Save:[Bank]` with one selected Scene empty and others real;
  confirm whichever policy is chosen in §6.1 behaves as decided, and that
  no partial whole-tree delete happens when the policy calls for refusal.
- Confirm a normal, fully-populated `Save:[Bank]` and root Scene Save are
  unaffected (no false-positive refusals).
- Confirm SEQ LED toggle/light behavior is unaffected — this plan changes
  nothing in `menu.c` beyond what P1 already corrects upstream.
- Re-run once P1 lands: confirm a Scene dropped from a prior partial Bank
  Save's on-disk tree, but still resident in SRAM, is correctly toggleable
  again for the next save (this is the direct, testable proof P1 fixed the
  mask this guard depends on).
