# S073: Post-flash menu bugfixes (plan)

- **Context:** the S073 flash-expansion image (`S073_FLASH_EXPANSION.md` §11)
  loads and boots on hardware. The first hardware pass on 2026-09-28 found
  two defects and one unwanted addition:
  - **A.** The Load menu has no Samples item, so samples cannot be installed.
  - **B.** Switching between Load menu types takes several seconds. The
    screen is often blank for many seconds while the `.hcindex` loads.
  - **C.** `FLASH_GROWTH_DRILL_KB` was added in S073 without being requested.
- **Status (2026-09-28):**
  - **A: fixed and hardware-verified.** Sample loading works again (§2.5).
  - **B: assessed and deferred by user decision** (§3). The trace logger is
    the main contributor that can be removed. It stays on for now; the
    assessment is kept in
    `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md` for when the
    logging is deactivated.
  - **C: done (2026-09-29).** The drill knob is removed (§4). D-C1 (keep
    or remove the boot image check) has not been decided. The check is
    left in place, which is the recommendation.
  - Line numbers are from the S073 working tree.
- **RAM policy:** A and C add no RAM. B's instrumentation (§3.5) needs your
  acknowledgement.

---

## 1. Order of work

1. **C: remove the drill knob** (small, no behaviour change). **Open.**
2. **A: restore Load:[Samples].** **Done;** hardware PASS on 2026-09-28.
3. **B: deferred.** Revisit when the trace/logging is deactivated, starting
   from `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md`. §3 below is
   kept as the analysis record.

---

## 2. Defect A — Load:[Samples] is missing

### 2.1 Cause (confirmed in code)

- The Load page cycles only through the types listed in
  `menu_loadSaveLoadTypes[]` (`menu.c:4411`): Kit, KitMrp, Scene, Bank,
  Pattern. `SAVE_TYPE_SAMPLES` is not listed.
- The whitelist was added in July (`a62221f`, "Kit save by slot-directory
  working again"). Samples has not appeared in it since. So this is **not an
  S073 regression**. It went unnoticed because installed samples survive
  firmware updates, so nobody needed to reinstall.
- Everything behind the menu row still exists and still compiles:
  - the OK dispatch (`menu.c:10497`, `case SAVE_TYPE_SAMPLES` →
    `menu_loadSamplesModal()`);
  - the modal (`menu.c:928`);
  - the row label (`menu.c:8367`, `"Samples "`);
  - the installer (`filesystem.c:22502`, `filesystem_installSampleFolderBlocking()`).
- **The installer was last hardware-verified in Sessions 018/023.** The
  filesystem facade has been heavily refactored since then.

### 2.2 Second problem found: the installer refuses a busy facade

- `filesystem_installSampleFolderBlocking()` returns 0 immediately if
  `status == FS_STATUS_BUSY`.
- `menu_loadSamplesModal()` suspends audio and calls the installer without
  first waiting for any background operation (AutoSave writer, trace flush,
  settings writer) to finish.
- The likely result once the row is restored: an intermittent
  `Sample upload / Failed` that depends on what happened to be running.

### 2.3 Fix

1. Append `SAVE_TYPE_SAMPLES` to `menu_loadSaveLoadTypes[]` only. Save has no
   Samples action.
   - The Pot-1 logical ring (`menu_loadSaveLogicalPosition()`) sizes itself
     from the array, so it follows automatically.
2. In `menu_loadSamplesModal()`, before `audioCodec_suspend()`:
   - wait for `filesystem_status()` to leave BUSY, pumping `filesystem_tick()`
     with a bounded timeout and showing `Waiting SD...`;
   - on timeout, show the existing failure text and change nothing.
   - **Confirmed:** once OK is accepted, no background writer can *start*.
     The trace flushes and the settings writer defer to the accepted command
     (`menu_beginLoadSaveCommand()` is called before the modal at
     `menu.c:10498`). The AutoSave writer and both pattern drains already
     stand down on the Load page.
   - So only an operation **already running** when OK is pressed can make
     the installer refuse. The modal blocks the main loop, so nothing else
     would ever finish that operation: the wait loop must pump
     `filesystem_tick()` itself.
3. Check the page states the restored row reaches:
   - **Row 2 (confirmed):** it shows `OK`, as for any unnumbered type.
     `preset_loadName(0, SAVE_TYPE_SAMPLES)` issues no filesystem request
     (`hasName = 0`).
   - **OK path (confirmed):** the blank-name gate applies only to numbered
     types, and OK is accepted for types at or above `SAVE_TYPE_GLO` once
     the cursor leaves the type row.
   - **Session boundary:** leaving Kit/KitMrp for Samples ends the
     seven-name session through `menu_endResidentNameScratchSession()`, as
     every other non-Kit type does.
   - **Fast drain:** `menu_loadSaveCommandInNoPlaybackScope()` must stay false
     for Samples. The modal owns its own codec suspend.
   - **Exit:** after the modal, the page returns to the bracketed type row,
     and the menu is usable without a reboot.
4. Remove the leftover one-second busy-wait "suspend/resume hardware test
   window" in the modal, unless you want to keep it.

### 2.4 Gate A (as planned)

- Build: `bss`/`data` unchanged.
- **Hardware:**
  - the Samples row appears on Load, and on Load only;
  - OK installs `/samples` and then `/loops`;
  - sectors 7–11 are erased and sector 6 is untouched (this closes S073
    Gate C4);
  - samples and loops play;
  - bytes free is about 1,308,320 B minus what was installed;
  - repeat the install once while the sequencer is playing and AutoSave is
    active, which proves the wait-for-idle.

### 2.5 Result (2026-09-28)

- **What was implemented** (`Core/Menu/menu.c`):
  - `SAVE_TYPE_SAMPLES` is appended to `menu_loadSaveLoadTypes[]`, on the
    Load page only;
  - `menu_loadSamplesModal()` waits up to 10 s (`Waiting SD...`) for a
    storage operation that was already running;
  - the one-second busy-wait is kept (D-A1).
- **Build:** `text` 483,936 B (+192), `data` 416 B, `bss` 426,336 B
  (unchanged). Image payload 484,352 B.
- **Hardware: PASS.** Load:[Samples] is present and sample loading works
  again (user report). This is the first sample install at the new sector-7
  floor, so it also closes S073 Gate C4 at the functional level.
- **Not reported individually:** the bytes-free figure, and an install with
  the sequencer playing.

---

## 3. Defect B — slow Load type switching

> **Status: DEFERRED (user decision, 2026-09-28).**
> - The trace logger (`DEV_MODE_LOGGING 1`) is the main contributor that can
>   be disposed of, and it stays on for now.
> - The trace-logger assessment, what deactivation will and will not fix,
>   and the measurement plan are in
>   `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md`.
> - The Load-page structural causes (§3.3) and fixes N1–N3 and N5–N7 (§3.6)
>   are **not scheduled**. Revisit them with that draft when the logging is
>   deactivated.
> - Nothing in this section has been implemented or measured.

### 3.0 Requirement (your decision, 2026-09-28)

- **A type switch is instantaneous.** The new type row appears in the same
  main-loop pass as the detent, and no detent is ever dropped.
- **List names fill in tens of milliseconds** after the switch.
- Save-page switching already feels fine. The Load-page behaviour below is
  what differs.

### 3.1 What a switch does (code path)

A type change on the Load page runs `menu_nextRestoredLoadSaveType()` and
then `menu_requestCurrentLoadSaveSelection()` (`menu.c:4770`). There is **one**
shared 1,000-row name cache, so every switch:

1. clears the cache (`filesystem_clearNameCache()`), leaving the row blank
   until new data arrives;
2. requests that type's data:

| Type | Card work per switch |
|---|---|
| Kit / KitMrp | First entry per Scene: read HCNAMES (161 rows, about 2 KB). Then chdir `/Kit` and read `/Kit/.hcindex`, unless the cache already holds it. |
| Scene | Read the resident Scene name from HCNAMES, then read `/Scene/.hcindex`. |
| Bank | Read `/Bank/.hcindex`, then a read-only child-Scene preview (a directory scan). |
| Pattern | Read `/Pattern/.hcindex`. |

3. If the single storage facade is busy, the request is refused and re-armed
   as `menu_deferSelectionRequest`. The retry fires from the Menu poll
   (`menu.c:11408`) only once the facade is idle.

The index files are small (1.0–1.5 KB, three sectors each). Several seconds
is therefore not a bandwidth problem: it is per-sector latency, time spent
waiting behind another operation, or input that is being discarded (§3.3).

### 3.2 Why each sector is slow while browsing

- `filesystem_tick()` runs once per main-loop pass. While browsing it calls
  `afatfs_poll()` **once per pass**. The four-pass fast drain applies only to
  accepted Scene/Bank/Pattern commands with the codec suspended
  (`menu.c:~1510`).
- The SD driver moves one 16-byte burst per poll (`sdcard_lxr02.c:96`). It
  reads the start token one byte per poll.
- So one 512-byte sector costs about **35+ main-loop passes**.

### 3.3 Load-only behaviour that holds up the menu (why Save feels fine)

Found by reading the code on 2026-09-28. Items 1, 2 and 3 apply **even when
nothing new is loaded**.

1. **Busy input is discarded, not queued.**
   - `menu_parseEncoder()` (`menu.c:10756`) returns early on every turn and
     click while `menu_storageBusy` is set or Preset is not idle. The only
     exception is number scrolling on Load Kit, KitMrp and Instrument.
   - A type-change detent made during that window is **lost**, and the user
     has to turn again after the work finishes.
   - Pot-1 entry into nested Instrument Load or Save also returns early when
     busy (`menu.c:7197`, `:7253`).
   - This is the direct cause of "several seconds just to switch": the switch
     cannot happen until the previous entry's card work has completed.
2. **Instrument Load and Morph Instrument Load write to the card on every
   entry.**
   - Before showing the typed list, `menu_prepareInstrumentLoadTemp()`
     (`menu.c:5419`) saves the current instrument to `.hctmp.<ext>` (or the
     Morph temp) through Preset. That is a create, write, close and sync.
   - The input gate is held throughout: first-entry HCNAMES read → temp write
     → `.hcindex` read.
   - Every voice, type or mode change calls
     `menu_invalidateInstrumentLoadTemp()`, so the next entry writes the temp
     again.
   - **Save mode skips this** (`if (menu_instrumentSaveMode) return 1u;`).
   - The comment in `menu_requestInstrumentEntryNames()` says Morph bypasses
     the temp, but the code writes a Morph temp. The comment is stale.
3. **Every Kit entry emits AutoSave trace records** (the branch A/B/C
   diagnostics in `menu_requestKitEntryNames()`), and Instrument entry emits
   `menu_traceInstrumentEntry()` records.
   - With `DEV_MODE_LOGGING 1`, pending records schedule an `/asavetrc.bin`
     append every 500 ms.
   - That trace flush and the settings writer are **not** held off on the
     Load/Save pages. The AutoSave record writer and both pattern drains are
     (`filesystem.c:~24463`, `:24199`, `:24550`).
   - The trace file has no size cap and is opened with `"a"`, so each append
     walks its FAT chain.
4. **On Load, Kit and KitMrp number scrolling is a real load.**
   - Each detent calls `preset_loadKitForScenes()` or
     `preset_loadKitMorphForScenes()` (`menu.c:10638` → `:4897`/`:4915`).
     That is a full Kit read plus runtime apply.
   - On Save, the same turn only shows the name.
   - While the apply drains, Preset is not idle, so type changes and clicks
     are discarded (item 1).
   - Instrument number scrolling is the same kind of real load (LSR-04).
5. **Leaving the Kit family after any load runs an HCNAMES checkpoint.**
   - `menu_endResidentNameScratchSession()` rewrites HCNAMES (temp file,
     sync, remove, rename) and raises `menu_storageBusy`.
   - The type-change detent is consumed by that flush (`menu.c:~10527`
     `break`), and the new type's list waits for it.
6. **Instrument `kit`-row restore.** After an audition load, resting on or
   returning to the `kit` row re-loads the `.hctmp` snapshot
   (`menu.c:11288` owed-restore retry). That is another payload load with
   Preset busy.

### 3.4 Hypotheses for the remaining time, ranked, with tests

| # | Hypothesis | Evidence so far | Test (no code first) |
|---|---|---|---|
| H0 | **§3.3 items 1–6 explain most of it.** | Confirmed in code. They are Load-only and match "Save is fine". | Time the switches (below) with **no number scrolling** on Load, versus after scrolling a few Kits. Enter and leave Instrument Load without scrolling. |
| H1 | **Waiting behind background writers.** An AutoSave record write already running when the page opened, or the trace flush / settings writer, which are not page-gated. | S054 documents the mechanism (`menu.c:11395`). The trace flush is fed by §3.3 item 3. | Copy `/asavetrc.bin` and `/bootlog.bin` after reproducing and decode them with `tools/decode_devlogs.py`. Look for trace flushes and AutoSave cycles that overlap the switches. |
| H2 | **The trace file is large.** Every append opens it and walks its FAT chain. | No cap exists. The ring is at the temporary 2,048-record size. | Check the size of `/asavetrc.bin`. A/B with `DEV_MODE_LOGGING 0`. |
| H3 | **S072 increased background dirtiness** (Effect edits, the FX sequencer, Effect LFOs marking AutoSave). | Unverified. Mostly page-gated anyway. | The same trace decode as H1. |
| H4 | **A slower main loop** (Phase 5 audio or service cost), at ~35 passes per sector. | The loop itself is unchanged in S072. | Time with the sequencer stopped versus playing; instrument (§3.5). |
| H5 | **An S073 regression.** | Unlikely: no menu or storage change in S073. | A/B with a `05bbd83` image on the same card. |
| H6 | **Too much work per switch** (HCNAMES re-reads, 4 KB cache thrash, Bank preview scan). | Structural. | The sector count per switch (§3.5). |

**How to time a switch:** use a stopwatch or a phone video of the LCD, 5
switches per case, Kit→KitMrp→Scene→Bank→Pattern→Kit. Record each case twice,
with the sequencer stopped and playing.

**A/B matrix (images only, no code change):**

| Image | Purpose |
|---|---|
| Current S073 image | Baseline |
| `05bbd83` (pre-S073) | H5 |
| S073 with `DEV_MODE_LOGGING 0` | H1/H2 (trace flush and bootlog off) |
| (Only if needed) an S071 image, e.g. `08b5f83` | Whether S072 introduced it. Images before `c80e1b3` reject the HCPR v3 / 161-row card state: use a **copy** of the card with `.hcnames`, `.hcnamtmp`, `.hcprms1` and `.hcprms2` deleted. |

### 3.5 Instrumentation (only if §3.4 leaves the cause open)

- Trace records in the **existing** AutoSave trace ring (logging builds only),
  for each Load-page browser request:
  - request tick;
  - accepted or refused, and the refusing owner (`current_op`);
  - completion tick;
  - sectors read;
  - the number of discarded encoder events.
- A logging-only pass counter for main-loop passes per second.
- **RAM:**
  - the records use the existing logging ring (approved while
    `DEV_MODE_LOGGING` is 1);
  - the counters are about **8 B of normal SRAM1 `.bss`**, logging builds
    only, owned by `filesystem.c`, lifetime the whole run. **This needs your
    acknowledgement.**
- Do not use `DEV_MODE_DIAGNOSTIC` screens for this: `lcd_waitForIdle()`
  changes timing.

### 3.6 Fix direction for the §3.0 requirement

The requirement cannot be met while navigation waits on storage. The core
change is to **separate UI navigation from storage completion**. The rest
make the storage work small and fast.

| Fix | What | Notes |
|---|---|---|
| **N1. Navigation never waits** | Type changes, Pot-1 moves and VOICE entries on Load/Save always apply at once: install the new type or voice, repaint the type row, and show a fixed placeholder (for example `........`) in the name field. The storage request becomes **latest-wins**: one pending request per page, posted when the facade is free. Completions with a stale `menu_selectionGeneration` are discarded, as today. `menu_parseEncoder()` stops discarding navigation input while busy; it still blocks OK/OW clicks and name editing during an accepted command. | This is the main fix for "instantaneous". Design check: which navigation must still wait. For example, leaving the page while an audition Kit apply drains already has a queued-exit path (`menu_pendingPageSwitch`). |
| **N2. No writes on entry** | Take the Instrument `.hctmp` / Morph temp snapshot **lazily**: immediately before the first audition load of the session, not on entry. Entering and leaving Instrument Load without scrolling then touches only reads. | This keeps the reversible `kit` row contract: a snapshot always exists before anything replaces the slot. Check against `048_SESSION_HANDOFF_LOG.md` and `AUTOSAVE.md` Instrument rules. |
| **N3. Checkpoints off the navigation path** | Do not run the HCNAMES checkpoint at type-change boundaries. Run it at page exit, or when idle, through the existing `menu_triggerDeferredHcnamesFlush()`. | **Hazard, must be redesigned rather than just moved:** the type-change flush exists so that Scene→KitMrp→Kit cannot publish a later Kit's identity for Scene-loaded destinations (the S051 identity-block rule, comment at `menu.c:~10530`). |
| **N4. Foreground priority** | While a Load/Save page is open, the trace flushes and the settings writer do not start, like the AutoSave writer and pattern drains already. Cap or rotate `/asavetrc.bin`; consider returning the ring to 64 records. | Small, low-risk. |
| **N5. Faster reads** | For Menu-owned browser reads, a bounded multi-poll: up to N `afatfs_poll()` calls per `filesystem_tick()`, capped by a DWT cycle budget (for example ≤ 50 µs per pass). Optionally let the token wait spin a few bytes per poll. | Needed to reach "tens of ms". Gate it on the underrun count under the CPU stress Scene. |
| **N6. Less work per switch** | Keep HCNAMES reads to one per Scene session (already so) and avoid re-reading an index the cache already holds. Clear the name cache only after the new request is accepted. | Keep the single-cache rule (`MEMORY.md`). |
| **N7. Audition loads stay, but do not block** | Kit/KitMrp/Instrument number scrolling keeps loading, as designed (LSR-04). A type change during an audition apply is shown at once (N1); the new type's index request queues behind the apply. | Design check: the audition load copies its name from the Kit index **before** payload I/O, so the cache can be replaced after acceptance. Verify there is no later read of the Kit index in the apply chain. |

**Suggested order:**
1. N4, the smallest change.
2. N1 with N7.
3. N2.
4. N5, if timing still misses tens of ms.
5. N3 last: it needs a redesign of the identity-block rule.

### 3.7 Gate B

- **Navigation:** every type, Pot-1 and VOICE detent on Load changes the
  displayed type/voice in the same pass (no dropped detents), with the
  sequencer playing and after audition loads.
- **Names:** filled within **tens of ms** (target ≤ 50 ms) when no write is in
  progress. When a write is unavoidable, the placeholder is shown instead of
  a blank row, and the names fill as soon as the write finishes.
- **No regressions:**
  - AutoSave still commits within its documented latency;
  - the reversible Instrument `kit` row still restores;
  - HCNAMES identity stays correct across Scene→KitMrp→Kit;
  - no new underruns under the CPU stress Scene;
  - the S054 livelock fix still holds;
  - Load/Save commands are unchanged.
- **Build:** `bss`/`data` unchanged except any approved logging-only counters.

---

## 4. Item C — remove the growth-drill knob

- Remove `FLASH_GROWTH_DRILL_KB` (`config.h`) and its table, the hex helper
  and the OK screen in `Core/Hardware/flashImage.c`. Update `MEMORY.md`,
  `SCOPING_TARGETS.md` §5.5 and `S073_FLASH_EXPANSION.md` §11.2/§11.4.
- **Decision D-C1: keep or remove the boot image check itself**
  (`flashImage.c`, the `.image_check` block, `tools/stamp_image_check.py`).
  - It was also added in S073 beyond the plan.
  - What it does: it is silent at boot and costs about 20 ms, no RAM, and
    ~0.7 KB of flash. It is the only thing that would report a sector the
    closed bootloader failed to erase or program once the image grows past
    `0x08080000`.
  - Recommendation: keep it. Remove it if you prefer the plan's original
    scope.
- Gate: normal image `bss`/`data` unchanged. With D-C1 = keep, the payload is
  unchanged except for the removed drill code, which is inert at 0 anyway.

**Done (2026-09-29):**

- `config.h`: the `FLASH_GROWTH_DRILL_KB` block is removed.
- `Core/Hardware/flashImage.c`: the drill table, `flashImage_hex8()` and the
  `Img OK` / `drill` screen are removed. A passing check returns silently,
  as it did with the knob at 0.
- `Core/Hardware/flashImage.h`: the contract no longer mentions the drill.
- `MEMORY.md`, `SCOPING_TARGETS.md` §5.5 and `S073_FLASH_EXPANSION.md`
  §11.2/§11.3/§11.4/§11.5 are updated.
- D-C1 has not been decided. The boot image check, the `.image_check` block
  and `tools/stamp_image_check.py` are left unchanged.
- Gate: `make clean` + `make all` links with the same 20 warnings as before,
  none from the edited files. `text=486,688`, `data=416`, `bss=426,336`.
  `lxr02.bin` is byte-identical to the pre-removal build (SHA-256
  `1bd8be52…5fc82`).

---

## 5. Decisions and questions for you

| ID | Question | Recommendation |
|---|---|---|
| D-A1 | Keep the modal's one-second suspend busy-wait? | **Kept** (not removed in the fix). |
| D-B1 | The type-switch latency target. | **Decided:** the switch is instantaneous, and names fill within tens of ms (§3.0). |
| D-B2 | Approve ~8 B of logging-only SRAM1 counters for §3.5 (only if needed). | **Deferred** with B. |
| D-B3 | Should everyday builds use `DEV_MODE_LOGGING 0`, and should the 2,048-record trace ring return to 64? | **Deferred:** the logger stays on for now (user). |
| D-C1 | Keep the boot image check? | Keep. |
| Q1 | When did Load switching last feel fast, and on which image? | Deferred with B. |
| Q2 | How large is `/asavetrc.bin` on the card you are using? | Deferred with B. |

---

## 6. Record

| Item | Date | Result | Notes |
|---|---|---|---|
| C: drill knob removed | | | |
| A: Samples row restored; install gate | 2026-09-28 | **PASS (hardware)** | `SAVE_TYPE_SAMPLES` added to `menu_loadSaveLoadTypes[]`. The modal waits (up to 10 s, `Waiting SD...`) for an operation already running. `text` +192 B, `data`/`bss` unchanged. The one-second busy-wait is kept. The user reports sample loading works again. |
| B: A/B timing — S073 image | 2026-09-28 | Deferred | Logger kept on; see the draft. |
| B: A/B timing — `05bbd83` | 2026-09-28 | Deferred | Logger kept on; see the draft. |
| B: A/B timing — logging off | 2026-09-28 | Deferred | Logger kept on; see the draft. |
| B: trace decode findings | 2026-09-28 | Deferred | Logger kept on; see the draft. |
| B: fix chosen and gate | 2026-09-28 | Deferred | Logger kept on; see the draft. |
| B: N2 lazy Instrument temp — design check | 2026-09-28 | Deferred | Logger kept on; see the draft. |
| B: N3 checkpoint redesign — design check | 2026-09-28 | Deferred | Logger kept on; see the draft. |
