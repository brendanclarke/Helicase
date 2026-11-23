# DRAFT — Load menu speed and smoothness: the trace logger's share

**Status: draft for later. Nothing here is implemented.** Written in Session
073 (2026-09-28), after the first hardware pass of the S073 flash-expansion
image. Use it when the trace/logging is deactivated (`DEV_MODE_LOGGING 0`), to
know what that will and will not fix.

- **Decision (user, S073):** the trace logger is the main thing that can be
  disposed of. It stays on for now.
- **Line numbers** are from the S073 working tree (branch `dev-ph5-effects`,
  after `05bbd83`). Re-verify before editing.

---

## 1. Symptom and requirement

- **Symptom (hardware, 2026-09-28):**
  - switching between Load menu types takes several seconds;
  - the name row is often blank for many seconds while the `.hcindex` loads;
  - Save-page switching feels fine;
  - the worst types are Kit, KitMrp, Instrument and Morph Instrument.
- **Requirement (user):**
  - the type switch is instantaneous;
  - list names fill in some tens of milliseconds after it.

---

## 2. How the trace logger slows the Load menu

### 2.1 What is running

With `DEV_MODE_LOGGING 1`, two trace rings drain to the card in the
background.

| Trace | Ring | Flush | File |
|---|---|---|---|
| AutoSave trace (`AutosaveTrace.c`) | **2,048 records** × 8 B = 16 KB SRAM1. This is a *temporary* expansion; the default is 64 (`config.h` `AUTOSAVE_TRACE_RECORD_COUNT`). | An append of at most **64 records** (the 512 B `staging_buf`) whenever records are pending, no more often than every **500 ms** (`AUTOSAVE_TRACE_FLUSH_INTERVAL_MS`, `config.h:528`). Scheduler: `filesystem_autosaveTraceFlushSchedule_tick()` (`filesystem.c:24721`). | `/asavetrc.bin`, opened `"a"` |
| Pattern trace (`PatternTrace.c`) | 32 records (`PAT_TRACE_RECORD_COUNT`) | Every **1,000 ms** when pending (`PAT_TRACE_FLUSH_INTERVAL_MS`). Scheduler: `filesystem_patternTraceFlushSchedule_tick()` (`filesystem.c:5571`). | `/pattrace.bin` |

### 2.2 Why that hurts the Load menu

1. **There is one storage facade.** A trace append is a full operation:
   return to root, open, write, close, and sync. While it runs, a Load-page
   request for a name list is refused and re-armed as a deferred retry
   (`menu_deferSelectionRequest`, dispatched at `menu.c:11408` once the facade
   is idle). The name row stays blank until then.
2. **The trace flushes are not held off on the Load/Save pages.**
   - They defer only to an *accepted* Load/Save command
     (`menu_isLoadSaveCommandActive()`), not to browsing.
   - By contrast, the AutoSave record writer and both Pattern drains already
     stand down while a Load/Save page is open (`filesystem.c:~24463`,
     `:24199`, `:24550`).
3. **Browsing produces trace records, which schedule more appends.**
   - Every Kit/KitMrp entry records its cache branch (A/B/C) in
     `menu_requestKitEntryNames()` (`menu.c:5629`).
   - Nested Instrument entry records its phases through
     `menu_traceInstrumentEntry()`.
   - So the act of browsing keeps a flush pending every 500 ms.
4. **A load leaves a long tail of appends.**
   - Load-page Kit/KitMrp number scrolling performs real loads (see §4
     item 3). A load marks AutoSave dirty, and the dirty marker emits a `D`
     record per accepted byte offset.
   - The S056 trace draft measured about 1,900 `D` records per Scene for a
     whole-Scene mark (`DRAFT_TRACE_SPLIT_BY_MODULE_UNPLANNED.md`).
   - That fills the 2,048-record ring. Draining it at 64 records per append,
     500 ms apart, takes **32 appends over about 16 s**. Any Load-page switch
     in that window can land behind one.
5. **`/asavetrc.bin` has no size cap.**
   - It is appended across sessions and never truncated or rotated.
   - Opening it `"a"` seeks to the end, which walks its FAT cluster chain, so
     each append takes longer as the file grows.
   - On this hardware the SD card is bit-banged: while browsing, each
     `filesystem_tick()` makes one `afatfs_poll()`, which moves one 16-byte
     burst. One 512-byte sector therefore costs about 35+ main-loop passes
     (`sdcard_lxr02.c:96`).

### 2.3 What `DEV_MODE_LOGGING 0` removes

Per `AutosaveTrace.h` and `DEV_MODES.md`, the trace APIs become no-op stubs
with logging off. Switching it off removes:

- both trace rings: 16 KB + the Pattern ring of SRAM1. **Under the RAM policy,
  released RAM is not free for reuse without approval**;
- every trace flush, and all `/asavetrc.bin` and `/pattrace.bin` I/O;
- the CPU cost of recording (the `D` records during dirty-marking, and the
  branch and phase records while browsing);
- the boot-logging deadlines and `/bootlog.bin` (the pre-audio window only);
- `DEV_LOGGING_IWDG`, which is already 0.

**Expected effect on the Load menu:**

- no background append can take the facade while you browse;
- no 16-second trace tail after a load;
- less blank time and fewer refused requests.

Measure it (§5) rather than assume it.

---

## 3. Options if logging has to stay on longer

These keep the logger but remove most of its effect on browsing. Each is
small.

1. **Page-gate both trace flushes**, as the AutoSave writer already is:
   - no new append starts while `menu_activePage` is `LOAD_PAGE` or
     `SAVE_PAGE`;
   - records stay in the ring and drain after the page is left;
   - the cost is a later flush and possible ring drops (already counted by
     the `F`/`G` self-report records).
2. **Cap or rotate `/asavetrc.bin`.** For example, start a fresh file at boot,
   or restart it past N KB. This bounds the per-append FAT walk.
3. **Return the AutoSave trace ring to 64 records** once the extension
   experiment is finished. This shortens any post-load drain from about 16 s
   to under 1 s.
4. **Stop recording per-byte `D` records** (the per-module split proposed in
   `DRAFT_TRACE_SPLIT_BY_MODULE_UNPLANNED.md`). This removes the largest
   producer.

---

## 4. What deactivating logging will NOT fix

These causes are in the Load-page code itself, and remain with logging off.
They are why Save feels fine and Load does not. Items 1–2 apply even when
nothing new is loaded.

1. **Busy input is thrown away, not queued.**
   - `menu_parseEncoder()` (`menu.c:10756`) returns early on every turn and
     click while `menu_storageBusy` is set or Preset is not idle. The only
     exception is number scrolling on Load Kit, KitMrp and Instrument.
   - A type-change detent in that window is lost.
   - Pot-1 entry into nested Instrument Load or Save also returns early when
     busy (`menu.c:7197`, `:7253`).
2. **Instrument and Morph Instrument Load write to the card on every entry.**
   - `menu_prepareInstrumentLoadTemp()` (`menu.c:5419`) saves the current
     instrument to `.hctmp.<ext>` (or the Morph temp) before the list loads.
   - Any voice, type or mode change invalidates it, so it is written again.
   - Save mode skips this.
   - The comment in `menu_requestInstrumentEntryNames()` saying Morph bypasses
     it is stale.
3. **On Load, Kit/KitMrp number scrolling is a real load.** Each detent calls
   `preset_loadKitForScenes()` / `preset_loadKitMorphForScenes()`
   (`menu.c:10638` → `:4897`, `:4915`). Instrument number scrolling is the
   same (LSR-04). While the apply drains, type changes are thrown away
   (item 1).
4. **Leaving the Kit family after a load rewrites HCNAMES first.**
   `menu_endResidentNameScratchSession()` rewrites it (temp file, sync,
   remove, rename). The type-change detent is used up by that flush.
5. **Instrument `kit`-row restore.** After an audition load, landing on the
   `kit` row reloads the `.hctmp` snapshot (`menu.c:11288`).
6. **The shared name cache is cleared before each request**, so any wait shows
   as a blank row.
7. **Browsing reads are slow.** One 16-byte burst per main-loop pass;
   fast drain applies only to accepted commands with the codec suspended.

Fix direction for these (details in `S073_POST_FLASH_MENU_BUGFIXES.md` §3.6,
which may be deleted after S073; the summary is repeated here):

- **N1:** navigation never waits. Type, voice and Pot-1 changes apply
  immediately with a name placeholder, storage requests become
  latest-selection-wins, and stale completions are dropped by
  `menu_selectionGeneration`.
- **N2:** take the Instrument temp snapshot lazily, before the first
  audition load, not on entry.
- **N3:** move the HCNAMES checkpoint off type changes. This needs a
  redesign: the flush exists for the S051 identity-block rule.
- **N5:** a bounded multi-poll for browser reads, capped by a DWT cycle
  budget.
- **N6:** clear the cache only once the new request has been accepted.
- **N7:** audition loads stay, but never block navigation.

---

## 5. How to measure before and after deactivation

- **Timing cases:** use a stopwatch or a phone video of the LCD, 5 runs each,
  sequencer stopped and playing.
  1. Kit→KitMrp→Scene→Bank→Pattern→Kit with **no** number scrolling.
  2. The same sequence immediately after scrolling through 3–4 Kits on Load.
  3. Enter and leave Instrument Load without scrolling.
- **Card evidence (logging on):**
  - the size of `/asavetrc.bin`;
  - decode `/asavetrc.bin` with `tools/decode_devlogs.py` and count appends
    and `D` records around a load, and any `F`/`G` drop reports.
- **A/B:** the same image with `DEV_MODE_LOGGING 1` and `0`, on the same
  card.
- **Record for each case:** the time until the type row changes, the time
  until names appear, and the number of dropped detents (turns needed per
  switch).
