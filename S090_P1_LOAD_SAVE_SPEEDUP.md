# S090 P1 — Load/Save Name Fetch Speed-Up While Playing

**Status:** Plan and decision record (2026-10-31; revised the same day after
the second and third reviews, U11–U21). Not implemented; schedule in
`S090_P1_IMPLEMENTATION.md`. Remaining
decisions: §10. Implementation scope: §11.
**This file is the single authority** for S090 P1 decisions and findings.

**Symptom (user):** boot, press PLAY, open Load/Save, cycle a few pages: the
file-name rows stay blank for many seconds.

**Goal (user):** faster names on screen after entering Load/Save, and faster
changes between Load/Save items, **while playing**.

**Evidence:** `SD_CARD_TEST/asavetrc.bin` (176,790 records, many boots) and
`SD_CARD_TEST/pattrace.bin`, decoded with
`tools/diagnostic_tools/decode_devlogs.py`. Phase timings come from pairing
the `N` (Instrument entry) request/complete records.

---

## 0. Decision record

### 0.1 User decisions (2026-10-31), in order

| # | Question | User decision | Effect on the plan |
|---|---|---|---|
| U1 | First-round Q3: lazy `.hctmp` at first preview? | Accepted | **Superseded by U4.** Lazy `.hctmp` is no longer planned. |
| U2 | First-round Q8: CPU headroom? | "There is basically no constant headroom for CPU use at all." | Items that **remove SD work** come first. Items that use idle time (item 1) are measured and kept only if they help. |
| U3 | Toggle all dev logging off and test | Done on hardware: no improvement in Load/Save speed. Logging restored. | Logging is **not** the cause (§1.3). Keep the gating fixes made during the test. |
| U4 | May the +129 B be used for an Instrument snapshot? | Yes, if it helps | It does: item 2 replaces `.hctmp` with a RAM snapshot taken at entry. |
| U5 | 131 B: the 129 B snapshot plus 2 B validity state? | Approved | Validity state was redesigned to 1 B (item 5); 130 B used. |
| U6 | Q5: risk of stale names from reuse? | "No wrong names ever." | Item 5: a default-deny validity rule; reuse only when correctness is guaranteed by construction. |
| U7 | Q7: keep more than one index in RAM? | First "use the RAM", then corrected: caching every index is impossible and the 4,176 B pool is **not approved** | No index pool, no multi-index store (§0.3). One browser cache only. |
| U8 | Q8: boot seeds the active Scene's resident names | Approved ("make that addition now") | Item 6c. Index loading at boot is **not** part of it (§0.3). |
| U9 | RAM read-back | Approved items OK; withdrawn items "incorrect and should not be considered" | §0.2 ledger is final. |
| U10 | Keep the 1 B names-stale flag and the 4 B logging-only counters? | "That's fine." | Both kept (§0.2). **Revisited:** the flag is proposed for removal (§10 Q2); the counters are withdrawn (U20). |
| U11 | Check the `.hcnames` RAM mirror against its AutoSave and Pattern uses before relying on it (F1) | Asked for the check | Done: §3.1. Names are safe to reuse while the mirror is VALID; reusing it skips a source-register reload (§10 Q1). |
| U12 | Standing rule for this session | "Not to unnecessarily change the multi-use cache usages unless there's a clear, valid reason directly related to the speedup, for either the .hcindex or .hcnames users." | §4 constraint. Each change to a shared-cache user names the speedup it buys. |
| U13 | §0.4 item 3b (settings/MIDI Learn writers yield) | "Not an issue worth addressing now … This isn't the problem that I'm having, leave it the way it is." | Withdrawn (§0.3). |
| U14 | §0.4 item 5e (shadow check) | "We checked by disabling and restored the dev logger. This isn't the issue." | Withdrawn (§0.3). |
| U15 | §0.4 item 4a (trace rotation) | "The dev trace doesn't seem to be the issue here. Don't … mess with stuff that won't solve the problem." | Withdrawn (§0.3). Item 3a (trace flush page gate) is withdrawn on the same grounds (confirmed out of scope by U20). |
| U16 | §0.4 item 6a (browser cache across exit) | "Does it make sense? When load/save is exited, caches are needed for other operations such as autosave or pattern editing." | Open. Answer and recommendation: §3.1, §10 Q3. |
| U17 | Second-round Q8: one index read (≈2.4 s) per type change | "This is slow. If this is necessary, we should consider not locking the facade when changing load/save type and at least letting the user get to the type they want before taking 2.4 seconds to populate the list." | One read per type is necessary with one cache (U7). New item 7: type changes are accepted while a read runs; the list loads for the type the user settles on (§5 item 7, §10 Q4). |
| U18 | Second-round Q9: idle pump scope | "Make sure you leave the facade the way it is for now. Blank means busy, don't … mess with how the pages display." | Binding (§4): page display unchanged. Names stay blank while their read runs. |
| U19 | Second-round Q11: underrun risk | "It should be absolutely impossible to underrun the audio buffer. Do not introduce any risk for this happening because of background load/save operations." | Binding (§4). Item 1 and §7a withdrawn (§0.3): each adds filesystem work to a main-loop pass. Every remaining change removes or cancels card work. |
| U20 | Second-round Q12: how to run it | "This isn't your scope. Scope the fixes that will address the issue in the plan (slow/unbrowsable list names) in a single implementation (don't write it now) and do not sidequest." | §11: one implementation, only fixes for slow or unbrowsable names. Measurement extras, trace changes and other items are out (§0.3). |
| U21 | Third-round §10 Q1–Q6 | "Remaining recommendations accepted." Deep-dive and full schedule requested as `S090_P1_IMPLEMENTATION.md` | Q1 mirror-served reads, Q2 existing gates instead of item 5, Q3 6a-i only, Q4 cancel, Q5 no AutoSave mark, Q6 Morph helper. Schedule written and pre-verified 2026-10-31; its §1 records the deep-dive findings (S3/S6 need no code). |

### 0.2 Approved RAM (final)

| Item | Bytes | Region | Owner | Build | Status |
|---|---:|---|---|---|---|
| Instrument snapshot (item 2) | 129 | SRAM1 `.bss` | `presetManager.c` | all | approved, needed |
| Resident names-stale flag (item 5) | 1 | SRAM1 `.bss` | `filesystem.c` | all | approved; proposed for removal (§10 Q2) |
| SD throughput counters (item 4c) | 4 | SRAM1 `.bss` | `filesystem.c` / `main.c` | logging only | **withdrawn** (U20: measurement extra) |
| **Static total** | **130 all builds** (129 B if Q2 is accepted) | | | | |
| Restore staging slot (item 2) | 196 | stack, foreground, only during a restore | `presetManager.c` | all | approved, needed |

- Default `.bss` is SRAM1, earmarked for future Pattern data. About 94 KB is
  free there today with logging on (`_ebss` 0x2006508C, SRAM1 ends
  0x2007C000).
- Nothing else may be allocated without a new user sign-off (bytes, region,
  owner).

### 0.3 Withdrawn — do not reintroduce

- **Library index pool** (4,096 B data + 80 B directory, "hold every recent
  index"). Rejected by the user.
  - **Why:** each slot-numbered type (Kit, Scene, Bank, Pattern) allows 1,000
    entries (up to ≈9 KB each at 9 B per name), and new Instrument types will
    keep adding indexes. A pool sized to one test card is not a design.
- **Loading indexes at boot.** Withdrawn with the pool.
  - **Why:** without the pool there is one browser cache holding one index
    at a time. Each index loaded at boot overwrites the previous one, and any
    Load/Save type change overwrites it again, so boot loading saves nothing.
    At best it would cover the first page, and only if nothing else used the
    shared buffer first; `.hcnames` reads and copy/clear also use it.
- **Lazy `.hctmp` at first preview** (former item 2 and U1). Superseded by
  the RAM snapshot (U4).
- **Earlier estimate "a second index costs about 1 KB".** That was wrong for
  slot-numbered indexes (up to ≈9 KB). Do not quote it.
- **Item 3b, settings/MIDI Learn writers yield (U13).**
  - **Why:** Load/Save, Global edits and MIDI Learn edits are not done
    together or in quick succession; this is not the reported problem.
- **Item 5e, shadow check (U14).**
  - **Why:** the logging off/on test (§1.3) already covered the logger.
- **Item 4a, trace rotation at boot (U15), and its open question (old Q3).**
  - **Why:** the dev trace is not the cause (U3), so trace hygiene does not
    solve the problem.
- **Item 3a, trace flushes wait for page exit, and trace ring overflow (old
  Q5).** Withdrawn (U15; confirmed out of scope by U20).
  - **Why:** both only change the dev trace, and the logging-off build was
    no faster.
- **Item 1, idle pump for Load/Save I/O, and its scope question (U19).**
  - **Why:** it runs extra filesystem steps inside a main-loop pass. A slot
    that frees during a step waits for that step to finish, which adds up to
    one filesystem step of delay before the next render. At the CPU ceiling
    that cannot be proven safe, and U19 allows no added risk.
- **§7a, larger SD burst (U19).**
  - **Why:** it lengthens every filesystem step before a render can start.
- **Item 4b extensions and item 4c counters (U20).**
  - **Why:** measurement only; they do not shorten a name fetch.
- **Trace record changes for item 2 (U20).**
  - **Why:** the retired temp phases (6/7) simply stop appearing. No new
    "snapshot captured" record, so `AutosaveTrace.h` and the decoder are
    unchanged.
- **6c boot trace field (U20).** Whether the mirror is VALID at audio start
  is settled by reading the boot code during implementation (§11).

### 0.4 Formerly "in the plan but not individually approved"

All four are resolved: 3b, 5e and 4a are withdrawn (U13–U15, §0.3); 6a is
open (U16, §10 Q3). The implementation scope is §11 (U20).

---

## 1. What the trace shows

### 1.1 The last boot (records #176741–#176789)

Nested Instrument entry runs three serialized facade operations, and the
names appear only after the last one (`menu.c`
`menu_requestInstrumentEntryNames()`):

1. `/.hcnames` read (resident Kit + six Instrument names);
2. `.hctmp.<ext>` write (hidden snapshot of the current voice, backs the
   `kit` row);
3. `Instrument/<type>/.hcindex` read (the name list).

| Phase (ticks = ms) | Start → end | This boot | Earlier median on this card |
|---|---|---:|---:|
| HCNAMES read | 8979 → 11654 | **2.7 s** | 0.20 s (n=37) |
| `.hctmp` write | 11654 → 20843 | **9.2 s** | 0.52 s (n=91) |
| `.hcindex` read | 20843 → 23848 | **3.0 s** | 0.17 s (n=113) |
| re-entry: two `temp request FAILED=1` (facade busy) | 24743, 26327 | ~5 s gap | — |
| second `.hctmp` write | 29684 → 39750 | **10.1 s** | — |
| second `.hcindex` read | 39750 → 42166 | **2.4 s** | — |

Entry to visible names: **15.3 s**. The three most recent boots are all slow
(HCNAMES 1.2–2.7 s, temp 2.9–10.1 s); the latest is about twice as slow as the
two before it.

### 1.2 What it is not

- **AutoSave:** never admitted in the window. A `W` record shows the writer
  held by the Load/Save page guard, and every `H` budget report shows
  `repair`, `scalar` and `pattern` at `charged_ms=0`.
- **Pattern work:** `pattrace.bin`'s last session holds only boot-time
  records. `decode_devlogs.py` has no `pattrace.bin` support and labels its
  records with AutoSave stage names; by `PatternTrace.h` the `V`/`L` records
  are `REPAIR_RESERVE`/`REPAIR_RELOC` (item 4b).
- **The dev trace, mostly:**
  - Both trace flush schedulers are gated by `menu_isLoadSaveCommandActive()`.
    That is set only by an accepted OK-press command
    (`menu_beginLoadSaveCommand()`), **not** by browse fetches.
  - So a flush cannot interrupt a phase, but it can take the idle facade
    between phases on a Load/Save page.
  - That is the most likely owner of the ~5 s busy gap above. The AutoSave
    writer is page-suppressed, and the deferred HCNAMES flush does not run on
    Load/Save pages.
  - Inferred, not witnessed: the trace has no record of its own flush
    start/end.
  - **Not pursued (U3, U15):** the logging-off build was no faster, so the
    gap is not treated as a target.

### 1.3 Logging off/on test (U3, 2026-10-31)

| Build | Flash (text) | data | bss |
|---|---:|---:|---:|
| `DEV_MODE_LOGGING 1` (before and after) | 683,512 | 424 | 412,884 |
| `DEV_MODE_LOGGING 0` | 667,072 | 420 | 396,064 |

- **Hardware result:** no improvement in names-on-screen or Load/Save item
  changes with logging off. Logging is not the cause; remaining slowness is
  the SD path.
- **Gating audit:** AutosaveTrace and PatternTrace have empty `#else` stubs;
  LTO removes every call. The trace rings and cursors, boot logger, HCPRMS
  boot capsule, trace flush ticks, and the drop counters in buttonHandler,
  LiveRecord, sequencer, menu and Autosave are all gated.
- **Fixed during the test (kept):** five logging-only helpers in
  `filesystem.c` were defined outside `#if DEV_MODE_LOGGING` while every
  caller was gated. Whole definitions (and two forward declarations) are now
  gated:
  - `filesystem_autosaveTraceSerialize()`
  - `filesystem_patternTraceSerialize()`
  - `filesystem_autosaveTraceFlushCompleted()`
  - `filesystem_patternTraceFlushCompleted()`
  - `filesystem_resetFacadeForBootLogRecovery()`

  The logging-on image is byte-for-byte the same size after the fix.
- **Pre-existing, untouched:** seven `-Wunused-function` warnings in
  `filesystem.c` in both builds:
  - four dead meta/globals helpers: `filesystem_metaHasStoredGlobalsLen`,
    `filesystem_applyLegacy22Globals`, `filesystem_staleMetaPrefixLen`,
    `filesystem_applyStaleGlobalsFallback`. They are candidates for removal
    under the no-backward-compatibility rule;
  - three helpers whose callers sit in `#if 0` blocks (lines ≈24096–24344,
    ≈25002–25857): `filesystem_reportBootSubstep`, `filesystem_blockRename`,
    `filesystem_bootLoggingSetKitDetail`.
- **`DEV_STALL_DETECTION` stays 1.** It is an operation-abort safety net,
  not a logger. `DEV_LOGGING_IWDG` and `DEV_MODE_DIAGNOSTIC` stay 0.
- **Current state:** `DEV_MODE_LOGGING 1`;
  `build/LXRV2_lxr02.img` = 683,936 B.

## 2. Root cause

While audio plays, foreground Load/Save I/O gets **one `afatfs_poll()` per
main-loop pass**, and one poll clocks one 16-byte bit-bang burst
(`SDCARD_BURST_SIZE`, `sdcard_lxr02.c:96`).

- **Fast drain is out of reach:** the four-pass fast drain
  (`FS_FAST_DRAIN_POLL_PASSES`, `filesystem_tick()`) runs only when
  `menu_beginNoPlaybackStorage()` has suspended audio, i.e. Scene/Bank/Pattern
  commands (`menu_loadSaveCommandInNoPlaybackScope()`). Instrument browsing
  and Kit-page name fetches are outside it by design.
- **The existing idle pump is gated elsewhere:** the S082 P2 pump
  (`main_serviceBackgroundLoadIdle()`, `main.c:249`, budget
  `BG_LOAD_IDLE_FS_BUDGET_US` = 200 µs, `config.h:868`) only serves
  background Bank loads.
- **Loop rate sets throughput:** SD throughput is set by how often the main
  loop comes round. With no constant CPU headroom (U2), that is roughly once
  per render. The worsening over the last three boots fits the current
  Oscillator/DrumVoice work raising render cost (inferred; the trace records
  neither transport state nor CPU load).
- **The overhead dominates, not the data:**

  | File | Size on `SD_CARD_TEST` |
  |---|---:|
  | `.hcnames` | 2,047 B |
  | `Kit/.hcindex` | 1,336 B |
  | `Scene/.hcindex` | 1,488 B |
  | `Bank/.hcindex` | 1,232 B |
  | `Pattern/.hcindex` | 1,032 B |
  | `Instrument/Drum/.hcindex` | 1,080 B |
  | other Instrument `.hcindex` | 360 B each |
  | `.hctmp.*` (stale files) | 676–1,389 B |

  - Each read also scans directories: the root (51 entries), `Instrument/`,
    then the type folder (`Instrument/Drum` has 124 entries).
  - Each read also does cluster and FAT lookups.
  - asyncfatfs caches only 8 sectors (`AFATFS_NUM_CACHE_SECTORS`, 4 KB),
    so every scan goes back to the card.
  - A `.hctmp` write adds create, allocate, write, close and sync, with card
    busy-waits. It is the largest single phase.
- **Consequence:** with no CPU headroom, the effective fix is **fewer card
  operations**, not faster ones.

## 3. Facts the implementation depends on

**Shared name cache.**
- `fs_list_cache_name[1000][9]` (≈9 KB, `filesystem.c` ≈1058), tagged by
  `fs_list_cache_kind` / `fs_list_cache_type` / `fs_list_cache_count`.
- It holds **one** index at a time.
- **Correction (second review):** `.hcnames` no longer uses it. Since S058
  Option 1C, `.hcnames` has its own mirror (§3.1);
  `FS_NAME_CACHE_HCNAMES` is a legacy tag only.
- Copy/clear borrows it for the 161-row HCNAMES remap
  (`FS_NAME_CACHE_COPYCLEAR`). Other users are listed in §3.1.

**Index layouts.**
- Slot-numbered (Kit, Scene, Bank, Pattern): 1,000 rows each, mostly empty.
  The cache is indexed by slot.
- Instrument: a dense sorted list per type (Drum 120, Snare/Cymbal/HiHat 40
  each on this card). New Instrument types will add more indexes.

**Reuse that exists, and where it is missing.**
- **Kit already skips a reload** when the cache holds the Kit index
  ("Branch B", `menu_requestKitEntryNames()` →
  `filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)`).
- **Instrument does not:** `menu_requestInstrumentIndexLoad()` always calls
  `filesystem_clearNameCache()` and reloads.
- **Load/Save exit discards everything:** `menu_switchPage()` (≈18814)
  clears `menu_residentNameScratchValid` and calls
  `filesystem_clearNameCache()`. Every new session therefore re-reads
  `.hcnames` + one index.
- **Within a session the seven-name scratch survives:** the trace's second
  entry had no HCNAMES read. A voice change re-does `.hctmp` + index.

**Instrument slot and restore path.**
- `kit_instrument_slot_t` (`SceneData.h:54`) = `instrument_type_t` (4 B
  enum) + three 64-byte images: Normal, Morph, `morph_interpolation`. That
  is ≈196 B; `INSTRUMENT_PARAM_COUNT` 64, `instrument_param_value_t` =
  `uint8_t`.
- The file loader resets the staged slot with `instrumentManager_resetSlot()`,
  then parses only Normal and Morph (`filesystem.c` ≈17083–17155). Nothing on
  the file path writes `morph_interpolation`. So a 129 B snapshot (type +
  Normal + Morph), expanded through `instrumentManager_resetSlot()`,
  reproduces today's file restore.
- **An in-RAM apply path already exists:**
  `preset_startInstrumentApplyImage(staged, mask, slot, source_slot, type,
  mark_autosave)` (`presetManager.c` ≈2664). It is used by the file load
  (`preset_startInstrumentApply()`) and copy/clear Instrument copy
  (`preset_startInstrumentCopy()`). It copies `*staged` into the Scene before
  returning, so a stack staging slot is safe.
- **Temp-path consumers to replace:**
  - `preset_saveInstrumentTemp`, `preset_saveInstrumentMorphTemp`,
    `preset_loadInstrumentTemp`, `preset_loadInstrumentMorphTemp`;
  - `filesystem_requestSaveInstrumentTemp`, `…SaveInstrumentMorphTemp`,
    `…LoadInstrumentTemp`, `…LoadInstrumentMorphTemp`;
  - `filesystem_loadedInstrumentWasTemporary()` (read in `menu.c` ≈17541,
    17898, 17923);
  - `filesystem_loadedInstrumentWasMorphTemporary()`, read by
    `preset_startInstrumentMorphApply()` (`presetManager.c` ≈2595) to choose
    `preset_commitStagedInstrumentMorphToMorph()`. That commit reads the
    loaded file image (`filesystem_loadedInstrumentSlot()`), so the Morph
    restore needs a commit from the snapshot instead (F3, §10 Q6);
  - `PRESET_OP_INSTRUMENT_TEMP_SAVE` / `…MORPH_TEMP_SAVE` /
    `…MORPH_TEMP_LOAD` handling in `menu.c` ≈17513–17541 and ≈17844–17998;
  - `menu_prepareInstrumentLoadTemp()` (≈8296) and
    `menu_restoreInstrumentLoadTemp()`.
- **`menu_init()` clears identity rows** (≈19944), which matters for the
  boot seed (item 6c).
- **Today's restore does not mark AutoSave (F4).** Menu passes
  `!filesystem_loadedInstrumentWasTemporary()` (0 for a restore,
  `menu.c` ≈17898). The `preset_startInstrumentApplyImage()` comment says a
  temporary restore "deliberately does not manufacture a new retained-data
  mutation" (§10 Q5).

### 3.1 Shared-cache review (second review, U11/U16)

**`.hcnames` mirror: `hcnames_name_mirror[161][9]`** (1,449 B) with
`hcnames_mirror_valid` (INVALID / PUBLISH_PENDING / VALID), `filesystem.c`
≈1075–1097. It is paired with `fs_resident_source[161]` (sources and the `R`
refreshed mark). Its users:

| User | Use |
|---|---|
| AutoSave ensure (boot) | reads the card into it; VALID on close (≈7804) |
| AutoSave convergence rewrite | requires VALID (≈8319); streams `.hcnames` from the mirror (≈7256) |
| AutoSave recovery from the winner | writes payload name bytes straight into it |
| Pattern Save/Load and Pattern AutoSave drain | `UPDATE_HCNAMES_PATTERN`: reset, read, overlay Pattern rows, write (≈6986) |
| Bank Save | reuses it without a read when VALID (≈19740) |
| Bank Load | working copy for child rows, held INVALID until it publishes |
| Scene 17 capture, copy/clear remap | read it |
| Menu Kit/Instrument entry | **always** resets and re-reads it (`filesystem_requestLoadResident*Name()`, ≈33014) |

Findings:
- **Names are safe to reuse while VALID (F1).** Every row overlay
  (transaction helpers, copy/clear remap, Bank and empty-Scene rows, recovery
  bytes) happens after the mirror is marked INVALID. VALID is set only after
  a complete read or a write's final sync, and every writer streams the file
  from the mirror. So VALID means the mirror equals the card.
- **The Menu writes its dirty rows before reading.**
  `menu_endResidentNameScratchSession()` (`menu.c` ≈7885) writes dirty rows
  before the entry read. A VALID mirror at entry therefore also equals the
  live names.
- **The code's own contract already asks for this.** The
  `filesystem_prepareResidentNamesCache()` comment says callers that can use
  a valid image "must test hcnames_mirror_valid before calling this reload
  primitive". The Menu entry requests are the callers that do not.
- **Side effect of skipping the read.** The entry read also reloads the
  source register: the reset sets every non-dirty source to UNKNOWN, and the
  read refills sources and `R` from the card. Dirty (staged) rows survive.
  `filesystem_setResidentRefreshed()` sets `R` without `DIRTY`, so today each
  entry read replaces a clean row's RAM `R` with the card's. Skipping the read
  keeps the RAM register until the next write. No path was found that relies
  on that reload, but it changes what the convergence writer later
  serializes (§10 Q1).
- **AutoSave and Pattern rewrites do not break reuse.** They hold the mirror
  INVALID while they run and end at VALID. If one fails, the mirror stays
  INVALID and the next entry falls back to a card read.
- **Doc error (not fixed, out of scope):** `MODULE_INTERCHANGE_SPEC.md` ≈242
  says the mirror is 1,305 B (the old 145-row size); the code asserts 1,449.

**Browser cache: `fs_list_cache_name`.** Users outside Load/Save browsing:

| User | Use |
|---|---|
| Copy/clear | borrows it (`filesystem_borrowNameCacheScratch()`); clears it on return |
| AutoSave recovery writer | clears it on finish and on error (`filesystem_autosaveWriterFinishErrorNow()` ≈8252, phase 38 ≈9977). This is left over from before Option 1C; it now only throws the index away |
| Refused `.hcnames` requests | clear it (`filesystem_clearNameCacheStorage()` on refusal, ≈33018 and siblings) |
| Boot library-index creation, index rebuilds, Pattern Save `.hcindex` rebuild | fill it with the rebuilt index |

Findings for 6a (U16):
- **No one outside Load/Save needs the cache's contents.** AutoSave and
  Pattern editing never read an index from it. Each borrower takes the
  storage and clears the tag, so keeping an index across exit starves no
  one; the next borrower just discards it.
- **Trusting it after exit is the costly part.** Every user outside the
  page would have to be proven to clear the tag or keep the rows correct
  (the item 5 library table). The gain is one index read on exit-and-return
  to the same page, which is not the reported symptom.
- **Within a session the cache survives previews.** Previews parse into
  `fs_stage_workspace` (`op_staged_instrument`), not the cache. Instrument
  Save updates the cache in place (`filesystem_updateInstrumentCacheAfterSave()`)
  and rebuilds the index. Copy/clear and AutoSave recovery clear the tag. So a
  same-type voice change can reuse the loaded index through the existing tag,
  as Kit already does (Branch B).
- **The tag is set when a request starts** (`filesystem_prepareLibraryNameCache()`),
  not when it finishes. Instrument reuse must also require that the load
  finished DONE (Menu already tracks completion).

**Trace flush gates.**
- `filesystem_autosaveTraceFlushSchedule_tick()` (≈27260) and
  `filesystem_patternTraceFlushSchedule_tick()` (≈5980) check only
  `menu_isLoadSaveCommandActive()`.
- Flush intervals: `AUTOSAVE_TRACE_FLUSH_INTERVAL_MS` 500, and
  `PAT_TRACE_FLUSH_INTERVAL_MS` 1000.
- `AUTOSAVE_TRACE_RECORD_COUNT` is 2,048 (the temporary expansion).

## 4. Constraints that apply

- **Load/Save scrolling mandate** (`FILESYSTEM_SPEC.md` §Binding Mandates):
  number first, name next, LEDs last; no stage may hold `menu_storageBusy`
  while the user scrolls. Today the Instrument entry chain holds it across
  all three phases. Nothing may lengthen a hold. RAM restores and cache hits
  take none.
- **SD work stays asynchronous; no 1 ms block** in the main loop or ISRs at
  priority ≤ 4. Boot-only synchronous polling before audio is allowed (item
  6c).
- **Render keeps priority;** no DSP work is skipped (`CPU_USE_DSP_AUDIT.md`).
- **No added underrun risk, ever (U19, binding):** no change may add
  filesystem or other work to a main-loop pass, or lengthen a filesystem
  step. Every change in §11 removes card work, cancels it, or replaces it
  with a bounded RAM copy no larger than the work it replaces.
- **Page display unchanged (U18, binding):** blank names mean busy. A name
  row stays blank while its read runs and fills when the read completes. No
  new placeholder, label, progress mark or layout change.
- **No wrong names, ever (U6).** Names in RAM are reused only through a
  proven validity gate. Proposed (§10 Q1–Q3): `hcnames_mirror_valid` VALID
  for resident names; the browser-cache tag plus a DONE load for an index.
  Item 5 was the earlier design.
- **Shared caches (U12):** do not change how the `.hcindex` cache or the
  `.hcnames` mirror is used by AutoSave, Pattern, copy/clear or any other
  user unless the change is directly needed for the speedup. Each such
  change states the time it saves.
- **RAM:** §0.2 only.
- **Logging allocations** exist only with `DEV_MODE_LOGGING 1`.
- **No backward compatibility:** no reader or migration for `.hctmp` files or
  old trace layouts.
- **Failed approaches (MEMORY.md):**
  - no blind 1 ms pacing (S046);
  - no bundled devlog consolidation, and no logger recovery that hides the
    original failure (S046);
  - never treat a failed open as proof of absence;
  - do not borrow `fs_list_cache_name` for unrelated data;
  - keep boot-reader type retention untouched;
  - durable name identity is corrected at the Load/Save commit boundary.

## 5. The items

### Item 1 — Idle pump for Load/Save I/O — WITHDRAWN (U19, §0.3)

Kept for the record. It adds up to one filesystem step of delay before a
render, which U19 does not allow.

**Change:** widen `main_serviceBackgroundLoadIdle()` from "background Bank
load running" to also cover:
- a Load/Save page open with the facade BUSY;
- the deferred HCNAMES flush running after exit.

The loop body is unchanged:

```c
while (filesystem_status() == FS_STATUS_BUSY &&
       audioCodec_queueFreeSlots() == 0u &&
       elapsed < BG_LOAD_IDLE_FS_BUDGET_US)
    filesystem_tickUncounted();
```

Rename the helper (e.g. `main_serviceStorageIdle()`) and add a Menu accessor
so `main.c` does not read Menu state. `!audioCodec_isSuspended()` stays; fast
drain owns the suspended case.

**Gate (U2):** it only runs while both render slots are full, which is rare
at the CPU ceiling. Keep it only if it measurably shortens name fetches with
no extra underruns (evidence: item 4c counters). It costs no RAM.

**Logging on/off:** identical.

### Item 2 — Instrument snapshot in RAM (removes `.hctmp`) — U4

- **What:** the hidden `.hctmp.<ext>` write at entry and the file read on
  `kit` restore are replaced by a RAM image of the entry voice. Entry writes
  nothing, and the restore touches no file.
- **Snapshot moment:** entry, as today.

**Data (129 B, SRAM1 `.bss`, owner Preset, `presetManager.c`). One voice,
never a Kit:**

```c
static struct {
    uint8_t type;                                            /* instrument_type_t */
    instrument_param_value_t normal[INSTRUMENT_PARAM_COUNT]; /* 64 */
    instrument_param_value_t morph[INSTRUMENT_PARAM_COUNT];  /* 64 */
} pm_instrument_snapshot;
```

`morph_interpolation` is not stored (§3). Menu keeps its session flags and
the existing 9-byte original name (`menu_instrumentTempName`).

**API replacing the four temp functions:**

| New | Replaces | Behaviour |
|---|---|---|
| `preset_captureInstrumentSnapshot(scene, slot)` | `preset_saveInstrumentTemp()` | copy `type` + both images; synchronous |
| `preset_captureInstrumentMorphSnapshot(scene, slot)` | `preset_saveInstrumentMorphTemp()` | copy `type` + Morph image |
| `preset_restoreInstrumentSnapshot(scene, slot, type)` | `preset_loadInstrumentTemp()` | below |
| `preset_restoreInstrumentMorphSnapshot(scene, slot, type)` | `preset_loadInstrumentMorphTemp()` | Morph-only commit of Morphable cells through the existing Morph-only apply (`instrument_apply_morph_only`) |

**Restore.** Use a stack `kit_instrument_slot_t` (196 B):
1. `instrumentManager_resetSlot(&staged, type)`;
2. copy Normal/Morph for the type's `descriptor_count` entries;
3. `preset_startInstrumentApplyImage(&staged, 1u << scene, slot, slot,
   type, mark)`. `mark` is **0** to match today's restore (F4, §3), pending
   §10 Q5; the earlier draft said `1u`.

**Morph restore (F3, pending §10 Q6).** `preset_startInstrumentMorphApply()`
reads the loaded file image, so the Morph restore gets a small helper that
copies the snapshot's Morphable Morph cells into the resident slot when the
types match (the same rule as
`preset_copyInstrumentMorphToMorphIfSameType()`). It then runs today's
remaining steps unchanged: `autosave_markInstrumentMorphDirty()`,
`bank_invalidateSdCleanScene()`, and, for the active Scene,
`presetMorph_requestVoice()` and the Morph-only worker.
`filesystem_loadedInstrumentWasMorphTemporary()` is removed with the other
temp functions.

**Completion contract:**
- Menu's completion switches keep their structure; Preset publishes the
  same completed-op codes.
- `filesystem_loadedInstrumentWasTemporary()` becomes a Preset-owned "last
  apply came from the snapshot" flag.
- Trace phases 6/7 (temp request/complete) simply stop being emitted. No
  new record is added and no enum is renumbered, so `AutosaveTrace.h` and
  the decoder are unchanged (U20). `COPYCLEAR_UTILITIES.md` §15 changes only
  where it describes the `.hctmp` entry phases.

**Remove** the four `filesystem_request*InstrumentTemp*` functions, their
operation phases, `filesystem_loadedInstrumentWasTemporary()` and the
`.hctmp` filename builders.

**Keep** `filesystem_instrumentStemIsReservedTemporary()`, so stale
`.hctmp.*` files on cards stay invisible. Document them as safe to delete;
no cleanup code.

**Live changes during browsing:** step automation, MIDI or live record
writes made while browsing are discarded by a `kit` restore. That is
today's behaviour with the file.

**Verification V1:** before deleting the file path, a temporary logging-only
check runs both restores on the same voice. The resulting `type`, Normal and
Morph must match byte for byte for all four types, in Normal and Morph
modes.

**Expected:** removes the 9–10 s phase at entry and on every voice change,
plus the restore file read.

**Logging on/off:** identical.

### Item 3 — Menu requests first on Load/Save pages

**Status:** 3a and 3b withdrawn (U13, U15, U20; §0.3).
Only 3c remains. Their text is kept below for the record.

**3a. Trace flushes wait for exit. — WITHDRAWN (inferred from U3/U15)**
- Both trace flush schedulers also return while `menu_activePage` is
  `LOAD_PAGE`/`SAVE_PAGE`, matching the AutoSave writer's page guard.
- Keep the one-shot `TRACE_SUPPRESSED` witness, with a new flag bit for
  "page".
- A flush already running when the page opens finishes normally.
- **Spec change:** `DEV_MODES.md` §Scheduling ("after that command has
  finalized it may append on a Load/Save page") becomes "deferred while a
  Load/Save page is open".
- **Logging off:** compiled out.

**3b. Settings and MIDI Learn writers yield. — WITHDRAWN (U13)**
- `settings.cfg` and `midilrn.cfg` writers do not **start** while Menu has
  a pending or deferred storage request (new
  `menu_hasPendingStorageRequest()`).
- **Ceiling:** after 5 s of waiting they start at the next idle facade, so
  a setting is never postponed indefinitely.
- **Logging off:** this is product code, so it stays active.

**3c. Prompt retry (kept).** Confirm a deferred Menu request
(`menu_deferSelectionRequest`) is retried on the first idle tick after the
facade frees; fix it if it waits for a later cadence. Menu-only; it touches
no shared cache.

**RAM:** none.

**Risk (3a only, now moot):** trace ring overflow on long page sessions.
Each Instrument preview emits about 84 records, so about 24 previews fill the
2,048-record ring. Overflow is counted by the `G` record.

### Item 4 — Measurement, decoder, trace hygiene (logging only)

**Status:** item 4 is withdrawn in full (U15, U20; §0.3). Item 2 needs no
decoder change because no trace record is added or renumbered.

**4a. Boot rotation — WITHDRAWN (U15)** (pre-audio):
- **When:** if `asavetrc.bin` exceeds `DEV_TRACE_ROTATE_BYTES` (proposed
  256 KiB), rotate one generation:
  1. delete `asavetr1.bin` if present;
  2. rename `asavetrc.bin` → `asavetr1.bin`;
  3. the next flush recreates `asavetrc.bin`.

  Same for `pattrace.bin` → `pattrac1.bin` (proposed 64 KiB).
- **Presence checks:** use a completed, closed, case-insensitive scan
  classifying zero / one / multiple matches. Multiple matches → skip and
  record an anomaly. Never use a failed open.
- **Failure handling:** a rotation failure never fails boot or hides
  another failure (S046). Leave the files, record one trace record,
  continue.
- **Cost:** directory-entry work only; measure the boot cost.
- **Speed effect:** small. This is hygiene and decode time, not the cause.

**4b. Decoder** (`decode_devlogs.py`, read-only):
- **Withdrawn (U20):** decode item 2's change to the Instrument-entry records
  (phases 6/7 retired, one "snapshot captured" record), so the existing `N`
  timings stay readable. This changes together with `AutosaveTrace.h`.
- **Withdrawn (U20):**
  - split per boot at the boot-reader summary (`Q` flags 0x80);
  - per-boot phase timing for each name source (HCNAMES, snapshot, index,
    payload, cache hits): min / median / max and start ticks;
  - `pattrace.bin` support from `PatternTrace.h`;
  - `*1.bin` generations (moot: 4a withdrawn);
  - records from items 3 and 5 (moot if those are withdrawn).

**4c. Throughput counters — WITHDRAWN (U20)** (4 B, logging only, §0.2): count main-loop
passes and `afatfs_poll()` calls. Publish both once per 5 s budget report as
one extra `H`-family record. This is the evidence for item 1 and for the
optional items in §7.

### Item 5 — Name validity rule ("no wrong names ever", U6)

**Status: proposed for replacement (§10 Q2).** The §3.1 review found two
gates that already exist:
- `hcnames_mirror_valid` for resident names;
- the browser-cache tag for an index, plus a DONE load.

Using them would remove the new op table, the build check, the 5c hooks and
the 1 B flag, and leave every shared-cache user unchanged (U12). The design
below is kept for the record. 5e is withdrawn (U14). One problem with it
(F2): previews are `LOAD_INSTRUMENT` operations and call
`preset_startInstrumentApplyImage()`, so the resident rule below would
invalidate the scratch on every preview. The Load/Save page keeps that
scratch correct by itself (identity row on load completion, dirty mask,
deferred flush).

Every reuse of names already in RAM (items 6a–6c) goes through this rule
and nothing else. It is **default-deny**.

**5a. Two domains.**

| Domain | Covers | Invalidated by |
|---|---|---|
| Library | the browser-cache tag (item 6a) | anything that can change any `.hcindex`, any library folder or any library row |
| Resident | the seven-name scratch (6b) and the boot seed (6c) | anything that can change any Scene's resident Kit or Instrument name or content |

**5b. Every facade operation is classified at compile time.**
- **One table:** indexed by `FS_INTERNAL_OP_*` in `filesystem.c`, with two
  bits, `LIBRARY_NEUTRAL` and `RESIDENT_NEUTRAL`, and a one-line reason per
  row.
- **Zero means not neutral:** an entry left at zero invalidates both
  domains.
- **Build check:** the table is sized by the op count, so an unclassified
  new op fails the build.
- **Trigger point:** `filesystem_start()` invalidates at operation start, so
  a failed or cancelled op has still invalidated.

Proposed neutral set:

| Operation | Library neutral | Resident neutral | Reason |
|---|:-:|:-:|---|
| Index load / rebuild of type T | yes (re-validates T only on DONE) | yes | publishes T itself |
| HCNAMES read | yes | yes (re-validates the Scene read) | it is the resident source |
| Bank preview scan | yes | yes | read only |
| Trace flushes | yes | yes | root `*.bin` only |
| `settings.cfg`, `midilrn.cfg` | yes | yes | no names |
| Scalar AutoSave writer (`.hcprms*`) | yes | yes | parameters only |
| Pattern AutoSave drains (`.patNNx`) | yes | yes | Pattern data only |
| Instrument / Kit / Scene / Bank loads | yes | **no** | resident names change |
| Every Save, delete, rename, repair, deferred HCNAMES flush, sample install, copy/clear op | **no** | **no** | may change either |

**5c. Non-facade name changes** invalidate through
`filesystem_invalidateLibraryNames()` / `filesystem_invalidateResidentNames()`:
- the copy/clear raw-block holder, on acquire **and** release
  (`copyClear_backgroundSuspended()` edges);
- Preset RAM image applies that change Scene Instrument or Kit content.
  This includes `preset_startInstrumentApplyImage()`, and therefore the item
  2 `kit` restore. That costs one extra read next entry, which is accepted:
  no special cases;
- any card remount, error recovery or `afatfs` re-init.

**5d. No counters, no aliasing.**
- **Library:** invalidation sets the cache tag to `FS_NAME_CACHE_NONE`.
- **Resident:** invalidation sets `fs_resident_names_stale` (the approved
  1 B). Menu clears it when it **requests** a fresh HCNAMES read, not when
  the read completes, so a mutation during the read leaves it set.
- **Reuse condition:** the flag is clear **and** the Scene matches.
- There is no generation number that could wrap.

**5e. Shadow check — WITHDRAWN (U14)** (logging only).
- **What:** on every reuse, queue one low-priority read of the same source
  for the next idle facade. Compare it line by line while streaming, with no
  buffer beyond the existing line buffer.
- **On mismatch:** emit a `NAME_MISMATCH` trace record (domain, kind, type,
  row).
- **Pass criterion:** every hardware test passes only with **zero
  mismatches**.
- **Scheduling:** the read is classified neutral, follows item 3's page
  rules, and compiles out with logging off.

**Scope limit:** the rule guarantees RAM names match what the firmware last
read or wrote. A card edited on a computer is seen after a power cycle; the
card is not hot-swappable and nothing remounts. State this in
`FILESYSTEM_SPEC.md`.

**Standing rule:** propose a binding MEMORY.md rule, "names in RAM are
trusted only through the classified-op validity rule; every new facade op
must be classified".

### Item 6 — Reuse names already in RAM (within the one cache)

**6a. Browser cache: same type, and return to the same page (U16, §10 Q3).**

Proposed split (§3.1):
- **6a-i, within a session (recommended):** a same-type voice change reuses
  the loaded Instrument index. It saves one index read (≈2.4–3 s while
  playing) per same-type voice change.
  - Add `filesystem_instrumentNameCacheLoaded(type)`: the kind is
    INSTRUMENT, the type matches, and the cache was published by a DONE
    load.
  - `menu_requestInstrumentIndexLoad()` takes the Kit-style Branch B with it.
  - It uses the existing tag. No other cache user changes.
- **6a-ii, across exit (recommended: drop):** keep today's clear on Load/Save
  exit (`menu_switchPage()` ≈18814). Trusting the tag after exit needs every
  outside user proven, and it saves only exit-and-return to the same page.
  - Superseded text: stop clearing on exit; trust the cache only through its
    tag; audit the Scene/Bank/Pattern request helpers for the same
    unconditional clear.
- **Limit:** the cache holds one index. **A → B → A costs one card read per
  switch** (U7).

**6b. Seven-name scratch across exit.**

**Proposed replacement (§10 Q1):** leave the scratch's exit behaviour as
today. Instead, the Menu's `.hcnames` entry requests skip the reset and the
card read when `hcnames_mirror_valid` is VALID. The request still goes through
the facade and finishes DONE on its first step, so the completion order is
unchanged and the card is not touched. That removes the `.hcnames` phase
(2.7 s in §1.1) from every entry after the first valid read, with no new
state. It changes only the Menu's own requests; AutoSave, Pattern, Bank and
copy/clear uses stay as they are.

Original text (superseded if Q1 is accepted):
- Keep `menu_residentNameScratchValid` and its Scene on exit.
- On entry, reuse the scratch iff its Scene matches the source Scene and
  `fs_resident_names_stale` is clear; otherwise read `.hcnames` as today.
- A non-empty `menu_residentNameDirtySceneMask` at exit still runs the
  deferred flush. That op is non-neutral, so the next entry re-reads.

**6c. Boot seeds the active Scene's seven resident names (U8).**

**Proposed simplification (§10 Q1):** the boot AutoSave ensure already
leaves the mirror VALID (≈7804). If every later boot step also ends with it
VALID, the first entry after boot is served by Q1 with no extra boot read.
6c then becomes:
- check, by reading the boot code during implementation, that the mirror
  is VALID when audio starts;
- add the boot read below only if the mirror is found INVALID there.

Original text:
- **Read last:** as the last pre-audio storage step (after the boot reader,
  its Case 2/3 reloads and any HCNAMES provenance refresh), do one HCNAMES
  read of the active Scene with boot-only synchronous polling.
- **Seed through the normal copy:** pass it through the same copy as
  `menu_residentNameScratchLoaded()`, so the identity rows, scratch Scene,
  scratch valid and the stale flag (cleared at request) are set.
- **Do not reuse the boot reader's buffer:** it is the shared cache, and
  later boot steps may rewrite rows.
- **Init order:** run after `menu_init()` (which clears identity rows), or
  make `menu_init()` leave a seed alone. Update `MODULE_INTERCHANGE_SPEC.md`
  "Boot / Init Order".
- **No index is loaded at boot (§0.3).**
- **Result:** the first Load/Save entry after boot skips the 2.7 s
  `.hcnames` read. It still pays one index read for the page it opens on.

### Item 7 — Type changes never wait (U17)

**What "type change" means:** a step of the Load/Save type cell (Kit,
KitMrp, Scene, Bank, Pattern, Instrument …, `SAVE_STATE_EDIT_TYPE`,
`menu.c` ≈15840), or a nested Instrument type step
(`menu_instrumentLoadStepType()`, ≈9305). Each new type needs that type's
`.hcindex` in the one browser cache.

**Problem (code):** while an index read runs, `menu_storageBusy` is set and
the encoder gate (`menu_parseEncoder()`, ≈16083–16099) refuses every turn
except Load-number scrolling on Kit/Instrument. So each type step waits for
the previous type's whole read (≈2.4–3 s while playing) before the next step
is accepted. Reaching a type three steps away costs about three reads.

**Is one read per type necessary?** Yes, for the type the user settles on.
There is one browser cache (U7) and no CPU headroom (U2). The reads for the
types passed over on the way are not necessary.

**Change (same rule as slot scrolling, `FILESYSTEM_SPEC.md` §Binding
Mandates):**
1. **The type label steps at once.** The gate also admits type-cell turns on
   the Load and Save pages, and nested Instrument type steps, while a read
   runs. No other input is newly admitted.
2. **Names blank while their read runs** (U18). Display is otherwise
   unchanged.
3. **A running index read for an older type is cancelled** at a phase where
   no callback is pending (the Bank preview cancel pattern,
   `filesystem_cancelScanBankScenes()`). It finishes ERROR and leaves the
   cache tag `FS_NAME_CACHE_NONE`; a cancelled or stale read never publishes
   a tag. An index **rebuild** (a write) and an `.hcnames` flush are never
   cancelled: the turn still updates the label, and the read follows when
   they finish (§10 Q4).
4. **Only the type on screen is loaded.** `menu_selectionGeneration` already
   increments on every type step. The deferred retry
   (`menu_deferSelectionRequest`, item 3c) requests the type on screen once
   the facade is idle; stale completions are discarded by generation.

**Speedup:** reaching a type N steps away costs one read instead of N.
**Underrun risk (U19):** none added. It cancels card work and adds none to
a pass.
**Shared caches (U12):** only the Menu-requested index read gains a cancel.
Boot index creation, rebuilds, copy/clear, AutoSave and Pattern users are
unchanged.
**RAM:** none expected (a flag bit in existing operation state, as Bank
uses `op_bank_flags`); confirm at implementation.

### Expected result (revised: items 2, 3c, 6a-i, 7, and Q1 in place of 5/6b/6c)

| User action | SD work after the plan |
|---|---|
| First entry after boot | one index read (no `.hctmp`; no `.hcnames` if the mirror is VALID at audio start) |
| Voice change, same Instrument type | none (6a-i) |
| Voice change, different type | one index read, for the type settled on (item 7) |
| `kit` restore | none |
| Load/Save type change (Kit ↔ Scene ↔ Instrument …) | the label steps at once; one index read for the type settled on (item 7) |
| Exit and return to the same page, nothing changed | one index read; no `.hcnames` (6a-ii dropped) |
| After a name-changing operation | `.hcnames` read only if its write left the mirror INVALID (failure); index per its tag |
| Any read | no `.hctmp` in front of it |

## 6. Order of work

Superseded by §11 (U20): one implementation. Earlier orders (4b/4c +
baseline; item 5; item 2; 6a; 6b/6c; item 3; item 1; item 4a; docs, and the
second-round revision) are kept only as history.

### 6.1 Verification

**Build:** `make all && make img`; no new warnings; `link_budget.py`
before/after. The RAM delta must equal §0.2.

**Name correctness (U6).** For each action below:
1. enter Load/Save so the names are warm;
2. do the action;
3. exit and return, and also switch type and voice.

The names must equal a fresh card read (shadow check withdrawn, U14: compare
against the card by a forced read or a computer listing).

| # | Action |
|---|---|
| N1 | Kit Save as a new name |
| N2 | Kit Save overwriting a slot |
| N3 | Instrument Save (new typed row) |
| N4 | Scene / Bank / Pattern Save |
| N5 | Kit / Instrument / Scene / Bank loads |
| N6 | MIDI program-change load |
| N7 | copy/clear paste Kit / Instrument / Scene; clear a slot |
| N8 | delete a slot |
| N9 | `kit` restore, then exit |
| N10 | AutoSave write, Pattern drain, settings change (reuse must **survive**: neutral) |
| N11 | Load:[Samples] |
| N12 | power-cycle after editing names on a computer |

**Hardware (logging ON, PLAY running unless noted):**

| # | Test | Pass |
|---|---|---|
| H1 | Instrument entry, each type | no `.hctmp` activity |
| H2 | Preview rows, `000 → kit` | entry voice restored exactly |
| H3 | Morph entry, preview, `kit` | only Morphable Morph cells restored |
| H4 | `kit` before any preview | no-op |
| H5 | Fast scroll in every phase | mandate order; number never lags |
| H6 | Instrument Save | unchanged |
| H7 | Voice change, two Drum voices | no SD access |
| H8 | Drum → Snare → Drum | one index read per type change |
| H9 | Instrument Save adding a row, then browse | fresh read; new name shown |
| H10 | Boot → PLAY → Load/Save | no `.hcnames` read (mirror VALID at audio start); one index read |
| H11 | Exit, return, nothing changed | no `.hcnames` read; one index read |
| H12 | Exit, MIDI PC Kit load, return | correct names; no `.hcnames` read when the load's publish succeeded |
| H13 | Exit with dirty names (deferred flush), return | correct names after the flush |
| H14 | Change active Scene, enter | correct names for the new Scene |
| H15 | Copy/clear during and after Load/Save | loan works; no stale cache |
| H16 | ~~Long browse: no trace flush on the page~~ | withdrawn with 3a |
| H17 | ~~Change a Global, browse at once~~ | withdrawn with 3b |
| H18 | Everything above, listening | no glitch; no underrun (U19) |
| H19 | ~~Boot with an oversized trace~~ | withdrawn with 4a |
| H20 | Scene/Bank/Pattern Load (audio-suspended path) | unchanged; fast drain still owns it |
| H21 | Stopped transport: H1, H7, H10, H11 | no regression vs historical medians |
| H22 | AutoSave convergence rewrite and Pattern drain between entries, then enter | correct names; served from the mirror (N10) |
| H23 | Step the type cell several types quickly, Load and Save pages | label follows every step; names blank, then the settled type's list; one index read |
| H24 | Drum → Snare → Cymbal → HiHat quickly (nested type step) | same as H23 |
| H25 | Step type while an index rebuild or `.hcnames` flush runs | label follows; the write finishes; then one read for the settled type; correct names |
| H26 | Step type back to the type whose read was cancelled | fresh read; correct, complete list (no partial cache reused) |

### 6.2 Logging toggle check

Builds L1 → L0 (`DEV_MODE_LOGGING 0`) → L1′; `DEV_LOGGING_IWDG` and
`DEV_MODE_DIAGNOSTIC` stay 0.

| Item | Logging ON | Logging OFF |
|---|---|---|
| 2 snapshot | active | identical |
| 3c prompt retry | active | identical |
| Q1 mirror-served entry (6c) | active | identical |
| 6a-i Instrument index reuse | active | identical |
| 7 type steps never wait | active | identical |

Withdrawn rows: 1, 3a, 3b, 4a, 4c, 5e; item 5 if §10 Q2 is accepted.

**Checks:**
- **L0:** no unused-function warnings that L1 lacks (§1.3 lists the seven
  pre-existing ones).
- **L0 image:** `nm` shows no trace rings, cursors or flush ticks.
- **L1′:** matches L1 in size and symbols.
- **L0 hardware:** repeat H1, H7, H10, H11, H18 and H23 by stopwatch.

## 7. Optional — not approved, not in §11

Not part of the implementation (U20). Each needs a user decision after
§11 is measured; 7b needs RAM sign-off. Neither 7b nor 7c adds work to a
pass, so both are compatible with U19.
- **7a. Larger SD burst — WITHDRAWN (U19):** `SDCARD_BURST_SIZE` 16 →
  32/64 B would lengthen every filesystem step before a render can start.
- **7b. Larger FAT cache:** `AFATFS_NUM_CACHE_SECTORS` 8 → 12/16 (+2/+4 KB).
  Directory sectors would stay resident between operations.
- **7c. Remembered index positions:** store each index file's directory
  position so reopening skips the directory scan. Needs a new asyncfatfs
  entry point.

## 8. Open questions (first round — superseded by §10)

Kept for the record. Q3 and Q5 are withdrawn (U15). Q2 and Q7 are withdrawn
with item 1 (U19). Q1 is answered by U17 (item 7). Q4 is moot if §10 Q2
replaces item 5. Q6 is folded into item 7.

**Q1. Target time.** With no CPU headroom the remaining cost is one index
read per type change (≈2.4–3 s while playing on this card).
*Recommendation:* set the acceptance target after items 2, 5 and 6 are
measured. If one read is still too slow, decide on §7.

**Q2. Idle pump scope (item 1).**
*Recommendation:* Load/Save pages plus the deferred HCNAMES flush only; not
every background writer.

**Q3. Rotation thresholds and names (item 4a).**
*Recommendation:* 256 KiB / 64 KiB, `asavetr1.bin` / `pattrac1.bin`.

**Q4. Coarse invalidation (item 5).** Any non-neutral op drops all reuse.
*Recommendation:* keep it coarse for P1, because it is easy to prove.
Per-type invalidation is a later refinement with its own shadow-check run.

**Q5. Trace ring overflow** during long page sessions (item 3a).
*Recommendation:* separate P2. Mirror S082 P2 so a whole-Instrument mark is
one `I` record with no per-byte `D`. `D` is 114,129 of 176,790 records (65%)
on this card.

**Q6. `menu_storageBusy` during cold reads.** A cold read still holds the
input gate.
*Recommendation:* measure after item 6; redesign the gate only if cold reads
remain noticeable.

**Q7. Underrun risk (item 1, 7a).** Load/Save writes include card busy-wait
polls.
*Recommendation:* H18 includes an Instrument Save and a Kit Save while
playing. If any underrun appears, lower `BG_LOAD_IDLE_FS_BUDGET_US` for the
Load/Save case first.

## 9. Documents to update (during implementation)

- **`FILESYSTEM_SPEC.md`:**
  - Instrument entry without `.hctmp`, and the RAM `kit` restore (≈145, 228,
    307, 425, 912, 2066, 2780);
  - Menu `.hcnames` entry requests served from a VALID mirror (≈2474), the
    reuse gates, and the scope limit (a card edited on a computer is seen
    after a power cycle);
  - same-type Instrument index reuse within a session;
  - type steps never wait: cancel of a stale index read (§Binding Mandates
    gains the type rule beside the slot rule);
  - stale `.hctmp` files are safe to delete.
- **`STORAGE_SRAM_MANIFEST.md`:** the §0.2 rows that remain.
- **`MODULE_INTERCHANGE_SPEC.md`:** boot order only if the 6c seed read is
  needed (§11).
- **`COPYCLEAR_UTILITIES.md` §15:** only where it describes the `.hctmp`
  entry phases. `AutosaveTrace.h` and the decoder are unchanged (§0.3).
- **`MEMORY.md`:**
  - replace the S088 status line;
  - add the reuse rule (names in RAM only through `hcnames_mirror_valid` /
    the browser-cache tag after a DONE load; shared-cache users unchanged
    unless the speedup needs it, U12);
  - add "the snapshot owns `kit` restore";
  - add to Failed Approaches: "caching every library index in RAM / loading
    indexes at boot (S090, rejected: 1,000 entries per type, open-ended
    Instrument types, one browser cache)".
- **Session log:** `knowledge_files/log_archive/090_SESSION_HANDOFF_LOG.md`.

## 10. Remaining open questions (third review, 2026-10-31)

**All accepted as recommended (U21, 2026-10-31).** Implementation schedule:
`S090_P1_IMPLEMENTATION.md`. Kept below for the record.

Each needed a user answer before code. Answered or withdrawn since the second
review:
- old Q4 (measurement extras) and Q5 (item 3a): withdrawn (U20);
- old Q8 (type change): item 7 (U17); its one design choice is Q4 below;
- old Q9 (idle pump scope) and Q11 (underrun): item 1 and §7a withdrawn
  (U19);
- old Q10 (`menu_storageBusy` during cold reads): folded into item 7;
- old Q12 (how to run it): §11, one implementation (U20).

**Q1. Serve the Menu's `.hcnames` entry requests from the mirror when it is
VALID (replaces 6b; simplifies 6c).**
- **What changes:** only the Menu's own `filesystem_requestLoadResident*Name()`
  requests. When VALID, they skip `filesystem_prepareResidentNamesCache()`
  and the card read, and finish DONE on their first facade step. AutoSave,
  Pattern, Bank and copy/clear uses of the mirror are unchanged (U12).
- **Speedup:** removes the `.hcnames` phase (2.7 s in §1.1) from every entry
  where the mirror is VALID, including the first after boot if boot leaves it
  VALID.
- **Side effect (§3.1):** entry no longer reloads `fs_resident_source[]` from
  the card. A clean row keeps its RAM `R` mark instead of taking the card's.
  No path was found that depends on that reload.
- *Recommendation:* yes, accepting the side effect. H22 and N10 cover the
  AutoSave and Pattern rewrites between entries.

**Q2. Replace item 5 with the existing gates.**
- **What changes:** names in RAM are reused only through
  `hcnames_mirror_valid` (resident) and the browser-cache tag after a DONE
  load (index).
- **Drops:** the op table, the build check, the 5c invalidation hooks and
  the 1 B stale flag (§0.2).
- **Why:** with Q1 and Q3 as recommended, nothing is reused that those two
  gates don't already govern. Item 5 as written also invalidates on every
  preview (F2).
- *Recommendation:* yes.

**Q3. 6a, the browser cache (U16).**
- **Answer to your question:** after exit, AutoSave and Pattern editing
  never need the cache's contents. Copy/clear borrows the storage and
  AutoSave recovery clears it; both reset the tag. Keeping an index across
  exit starves no one, but trusting it later needs every outside user proven
  (§3.1), and it saves only exit-and-return to the same page.
- **6a-i, within a session:** same-type voice changes reuse the loaded
  Instrument index through the existing tag, as Kit does. Saves one index
  read (≈2.4–3 s) per same-type voice change. *Recommendation:* keep.
- **6a-ii, across exit:** *Recommendation:* drop; keep today's clear on exit.

**Q4. Item 7: cancel the stale index read, or let it finish?**
- **Cancel (recommended):** a type step during a read sets a cancel flag;
  the read unwinds at its next callback-free phase, closes the file and
  finishes ERROR with the cache tag NONE. Then the read for the type on
  screen starts. Cost: one cancel path in the Menu-requested index read
  (filesystem.c), modelled on the Bank preview cancel.
- **Let it finish:** no filesystem change. The stale read runs to the end,
  is discarded by generation, then the read for the type on screen starts.
  Settling on a type can then cost up to one extra read (≈2.4–3 s).
- Either way: the label steps at once, names stay blank until the settled
  type's read completes (U18), and index rebuilds and `.hcnames` flushes
  are never cancelled.
- *Recommendation:* cancel. It matches the slot-scrolling mandate ("the next
  turn disposes whatever is still running") and removes, rather than
  delays, the wasted read.

**Q5. `kit` restore AutoSave mark (F4).** Today's restore does not mark the
whole Instrument for AutoSave; the first draft passed `1u`.
*Recommendation:* pass `0u` (unchanged behaviour).

**Q6. Morph `kit` restore helper (F3).** Commit the snapshot's Morphable
Morph cells, then run today's remaining steps unchanged (item 2, "Morph
restore"). *Recommendation:* yes.

## 11. Implementation scope (U20: one implementation, no side quests)

**Goal:** the list names (Load/Save) appear sooner and the lists can be
browsed while playing. Only the changes below are in the implementation.
Each one removes or cancels card work (U19), leaves the page display as it
is (U18), and changes a shared-cache user only where the speedup needs it
(U12).

| # | Change | Removes | Files | Depends on |
|---|---|---|---|---|
| S1 | Item 2: RAM snapshot replaces `.hctmp` (normal and Morph capture and restore); remove the temp file path; keep the reserved-name filter | the `.hctmp` write at every entry and voice change (9–10 s in §1.1) and the restore file read | `presetManager.c/h`, `menu.c`, `filesystem.c/h` | Q5, Q6 |
| S2 | Q1: Menu `.hcnames` entry requests finish from a VALID mirror | the `.hcnames` read at entry (2.7 s in §1.1) | `filesystem.c` (the `filesystem_requestLoadResident*Name()` requests and the load mode of `filesystem_residentNames_tick()`) | Q1 |
| S3 | 6c: confirm, by reading the boot code, that the mirror is VALID when audio starts; only if it is not, add the approved boot read (U8, boot-only polling before audio) | the `.hcnames` read at the first entry after boot | `main.c` / `filesystem.c` (only if needed) | Q1 |
| S4 | 6a-i: same-type Instrument voice change reuses the loaded index (Kit-style Branch B, DONE load required) | one index read per same-type voice change | `menu.c`, `filesystem.c/h` (accessor) | Q3 |
| S5 | Item 7: type steps accepted while a read runs; stale read cancelled; only the settled type loads | one index read per type passed over | `menu.c` (encoder gate, type-step handlers), `filesystem.c` (index-read cancel) | Q4 |
| S6 | Item 3c: a deferred Menu request is retried on the first idle tick after the facade frees (check; fix only if it waits for a later cadence) | idle wait between a refused and a retried request | `menu.c` | — |
| S7 | Item 5 only if Q2 is rejected (without 5e) | — | `filesystem.c`, `menu.c` | Q2 |

**RAM:** 129 B SRAM1 `.bss` (snapshot) + 196 B stack during a restore
(§0.2). +1 B only if Q2 is rejected. Nothing else.

**Flash:** expected to fall (the four temp request paths, their operation
phases and the `.hctmp` name builders go; the snapshot helpers are small).
Measured with `link_budget.py` before and after.

**Verification:** §6.1 (build, N1–N12, H1–H15, H18, H20–H26), §6.2 logging
toggle. Docs per §9 at close-out.

**Out of scope (do not touch):**
- item 1 idle pump, §7a burst size (U19); §7b/§7c (not approved);
- items 3a, 3b, 4a, 4b, 4c, 5e; trace record, decoder or `AutosaveTrace.h`
  changes (U13–U15, U20);
- 6a-ii (cache across exit) and the original 6b scratch retention;
- AutoSave, Pattern, Bank, copy/clear and boot uses of the `.hcnames` mirror
  and the browser cache, beyond S2–S5 (U12);
- page display: names, labels, LEDs, layout (U18);
- unrelated findings, reported only: the seven pre-existing
  `-Wunused-function` warnings (§1.3), the `MODULE_INTERCHANGE_SPEC.md`
  1,305 B mirror size (§3.1), the vestigial browser-cache clear in AutoSave
  recovery (§3.1);
- the uncommitted Oscillator/DrumVoice/S089 files in the tree.
