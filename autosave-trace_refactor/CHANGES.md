# Changes applied (refactor plan items 5.1, 5.2, 5.3)

These five files replace their originals at the same paths. Everything else
in the tree is untouched. No new persistent RAM allocation — the 5.2 change
reuses the existing 512-byte / 64-record trace ring; 5.3 and 5.5 are pure
control-flow changes; 5.1 removes files that were never compiled.

## 5.1 — Removed `.failed` shadow files from `Core/`
Moved out of `Core/` into `knowledge_files/log_archive/failed_attempt_pre_session049/`
(not included in this delivery — they're inert reference material, not a
source change):
- `Core/Bank/BankData.c.failed`, `.h.failed`
- `Core/Bank/Scene/Preset/presetManager.c.failed`, `.h.failed`
- `Core/Bank/Scene/SceneData.c.failed`, `.h.failed`
- `Core/Hardware/SD/filesystem.c.failed`, `.h.failed`
- `Core/Menu/menu.c.failed`, `.h.failed`
- `Core/Bank/Scene/Autosave_failed.c`, `.h`

Confirmed via `grep` that nothing in `Makefile` or the remaining `Core/`
tree referenced any of these — safe removal.

## 5.3 — Narrowed the trace-flush guard (`filesystem.c`)
`filesystem_autosaveTraceFlushSchedule_tick()` previously refused to run
whenever `menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE`,
which blocked `asavetrc.bin` writes for as long as the user stayed on the
Load/Save page — including long after an accepted command had already
finished. Changed the guard to `menu_isLoadSaveCommandActive()`, which is
true only for the actual busy window (an accepted OK/OW command and its
post-apply root-index restore), matching the guard's own documented intent
more precisely.

`menu_loadSaveCommandActive` is `static` in `menu.c`, so a new one-line
read-only accessor was required:
- `Core/Menu/menu.c`: added `uint8_t menu_isLoadSaveCommandActive(void)`
- `Core/Menu/menu.h`: declared it

This is a direct instance of refactor-plan item 5.5 (named busy predicate)
done narrowly, just for this call site.

## 5.2 — New durable `L` (LOAD_MARK) summary trace stage
`AutosaveTrace.h`: added `AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L'` plus its
kind/flag/value-layout macros, following the existing `I`/`J` Instrument
pattern.

`Autosave.c`: `autosave_markKitDirty()` and
`autosave_markSceneWithoutPatternDirty()` each now emit one `L` record at
the end of the function, after all their nested per-parameter/per-Instrument
marking. This is the fix for the Scene Load trace "black hole" documented in
`LOAD_SCENE_TRACE_AUDIT.md`, generalized to Kit Load (and Bank Load, which
calls the same Scene marker) since they have the identical latent bug.

Record contents: kind (Kit=0 / Scene=1), Scene index, and whether mutation
tracking was enabled at the time — enough to prove the marker actually ran
for a given Scene regardless of how many `D` records it produced or whether
they wrapped the ring first.

## What was intentionally NOT done in this pass
- 5.4 (shared completion-marking helper in `presetManager.c`) and 5.6
  (shared directory-tick state machine) are not included here — per the
  plan's suggested order, those come after 5.2/5.3 are validated on
  hardware, using the new `L` records as a regression detector.
- No `presetManager.c` changes — the three completion callbacks
  (`on_kit_load_complete`, `on_scene_load_complete`, `on_bank_load_complete`)
  are untouched; they already call the markers that now emit `L`.

## Suggested verification on hardware
1. Build with `DEV_MODE_LOGGING=1`, confirm `arm-none-eabi-size -A` shows no
   unexpected `.bss` growth (expect none — no new storage was added).
2. Load a Scene from `Load:[Scene   ]`, stay on the page, then read
   `/asavetrc.bin` — an `L` record with kind=SCENE (and a nested kind=KIT
   record) for the loaded Scene index should now be present, whereas before
   this change `asavetrc.bin` would not even have been written to yet while
   still on the page.
3. Repeat for Kit Load and confirm a kind=KIT `L` record appears.
4. Build with `DEV_MODE_LOGGING=0` and confirm the image still contains no
   trace ring symbol and performs no diagnostic file write (existing
   `DEV_MODES.md` validation checklist item).
