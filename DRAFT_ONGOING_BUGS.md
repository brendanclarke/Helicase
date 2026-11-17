# DRAFT — Ongoing Bugs (future work)

Started 2026-11-17 (S097), at the user's request. **Low-depth notes only:**
reported symptoms, the mechanisms most likely involved, and evidence already at
hand. Nothing here was researched in depth or fixed. Line numbers are as of
S097 P2.

---

## B1. Instant Scene change between identical Scenes is not seamless

**Report (user, 2026-11-17).** An instant Scene change between Scene slot 1
and slot 9, which hold identical copies, produces steps out of place and notes
truncated or re-appearing. Not all of that is necessarily a bug. **Intent:**
switching instantly between two identical Scenes must be indistinguishable
from one Scene playing on.

**What a Scene select does today** (`seq_selectActivePattern()`,
`sequencer.c` ≈ l. 2410–2430, and the S096 P3 change marks ≈ l. 658–745).
Each item below is a way two identical Scenes can sound different across the
switch:

| Mechanism | Possible symptom | Likely nature |
|-----------|------------------|---------------|
| `seq_trackRepeatCount[]`, `seq_trackRndStepCount[]` and `seq_trackLastConditionFired[]` are cleared | trigger conditions that count passes (`1:2`, `2:4`, …), `fil`/`!fl`, `lst`, `lop` and random play mode evaluate as on a first pass: conditional steps appear or vanish | design question: should an instant change to an identical Scene keep the counters? |
| running step rolls end ("a Scene change ends every running step roll") | a roll in progress is cut | by design (S079 P2 fix 4 / S084 P1); may need an exception |
| Colloid change decision (S096 P3 C1): a sustained Colloid note continues only if the new Scene's first step triggers it or carries sus-on | Colloid notes truncated at the switch | by design (decided Q4) for *different* Scenes; arguably wrong for identical ones |
| step-length windows `seq_lenWindow[]` for steps with fill < 100 % (same block) | a note held under the old step may be ended or re-timed | unknown |
| per-track cursor and DDA realignment: `seq_trackAccumulator[t] = master_q8 % interval` (l. 5013, 5090) and `seq_alignActivePatternToScene()` | steps out of place if the new Scene's cursor or phase is derived differently from the running one (per-track length/scale/shuffle, `seq_effectiveTrack*[]`) | possible bug; check that identical data gives an identical cursor |
| instrument/Kit apply on Scene change (voice reset, Morph images, run mode `rst`/`fre`/`itp`) | envelope or phase restarts, clicks, truncation | check what is re-applied when the images are equal |
| step reset rule (S081 §5.8: reset once per step at first trigger) and pending triggers in `voiceControl_processPending()` | a step re-fired, or a reset at the switch point | unknown |

**First things to establish:**

1. Reproduce with the simplest content: one track, plain steps, no conditions,
   no rolls, not a Colloid. If that is seamless, add the features above one at
   a time.
2. Where the change happens: exactly on a step boundary (`seq_handleMasterBoundary()`
   for queued changes; instant changes from PERF/MIDI PC via
   `seq_selectActivePattern()`), or mid-step.
3. A trace of per-track cursor positions around the switch would settle
   "steps out of place". `pattrace.bin` holds Pattern trace records published
   from TIM3, but `decode_devlogs.py` currently decodes it with the AutoSave
   stage table ("decoder kind: asavetrc.bin"), so its decoded text is not
   meaningful yet (tooling note below).

---

## B2. AutoSave is slow and does not catch all edits in reasonable time

**Report (user, 2026-11-17).** AutoSave, especially the parameter AutoSave,
needs to run more often and finish faster. Edits are not all caught in a
reasonable time. Seen mostly with the **Colloid**, so some Colloid
parameters may not register as dirty after an edit.

**Evidence already on the card** (`SD_CARD_COL/asavetrc.bin`, 1.1 M records,
82 boots; trace ticks are milliseconds):

- **Drain time.** Across 3,075 parameter drains, admission → terminal is a
  median of **7.8 s**, 90th percentile **24.9 s**, max ≈ 65 s (or more: the
  16-bit tick wraps).
- **Debounce.** That is on top of the writer's debounce:
  `AUTOSAVE_WRITER_INTERVAL_MS` = 5,000 ms for new work (config.h ≈ l. 560).
  Then 250 ms continuations while a backlog remains
  (`AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS`).
- **CPU budget.** Background AutoSave/repair work gets a time share:
  `BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING` = 25 µs/ms (2.5 %) while playing,
  50 while stopped (config.h l. 711–712). This is the likely main reason
  drains take seconds while playing.
- **Load/Save page.** The writer is held while the Load/Save page is open
  (`AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED`, "intentional Load/Save page guard",
  S090 P2).
- **Last boot of the S097 P2 test.** One parameter
  (`Scene0 instrument[4] normal[54]`) was marked dirty ~40 times between ticks
  52,359 and 58,962. The writer was armed only at tick 63,463, then held
  because the Load/Save page was opened. Nothing was written for those edits
  while the page was open.

**Hypotheses to check:**

1. **Speed.** The 2.5 % playing budget and the per-drain work (full-record
   validation, mask merge, CRC at 128 B per tick) dominate. Options: a larger
   playing budget, smaller or incremental drains, a shorter debounce for
   parameter-only dirt. Each must respect the CPU ceiling (S089) and the
   "no SD polls per pass while playing" rule.
2. **Missed Colloid edits.** Some Colloid writes may not reach
   `autosave_markPayloadOffsetDirty()`. Candidates: rows written outside the
   ordinary `menu_cellCommitValue()` → Scene store path, such as the bit
   cells (S096 P1 SD1), the MIDI-mode writes (S096 P3), special writers, and
   Morph-endpoint edits. Test: edit each Colloid row once and check for a
   `stage=D` record naming that row (`… instrument[slot] normal[row]` /
   `morph[row]`).
3. **Debounce restart.** Check whether continuous editing keeps pushing the
   5 s deadline out ("writer armed once": it may be armed only once).

**Relevant rules:** `AUTOSAVE.md` (format, dirty ownership, writer
lifecycle); MEMORY.md "Load/Save drops AutoSave (S090 P2)" and "Start-up
AutoSave settle"; the background budget in `filesystem.h`
(`filesystem_backgroundBudget*`).

---

## B3. Bank Save over an occupied slot keeps the old Bank's other Scenes

**Report (user, 2026-11-17, S097 P2 test).** After a Bank Save and a Bank
Load, Scene slots 2–6 were populated, though no merge option was used.

**Root cause (found in the logs and on the card; not fixed).**

- Bank slot 024 held the 16-Scene Bank "NoBank" (`SD_CARD_COL` copy). The
  Bank Save into slot 024 ("CollidSt") wrote only the **9 Scenes present** in
  the resident Bank (00, 07–11, 13–15; 9 `CREATE_RESULT` records) and renamed
  the folder.
- Folders 01–06 and 12 of the old Bank stayed on the card unchanged: the same
  names and Kits (SeaWaked, Eris, Flux, Beatmstr, FilMod, Beatmstr, Beatmstr).
- The following Bank Load found 16 children (`BANK_PRESENT … effective load
  mask 0xffff`) and loaded them all. The stale Scenes appear at 1-based slots
  2–7 and 13.
- This is the design of `filesystem_saveBankDirectory_tick()`: "for each bit in
  op_bank_scene_save_mask … deletes any existing child at that slot …
  Non-masked children are never touched and survive from previous saves.
  This replaces the former total-tree-delete … which destroyed ALL children and
  failed when the tree contained quarantined directories (ErrS05)."

**Intent (user):** a non-merge Bank Save must leave exactly the saved Scenes in
the Bank. **Direction:** in a replace (non-merge) Bank Save, also delete the
existing child folders for slots *not* in the save mask, one child at a time
(the same per-child delete as phases 20–21). Quarantined directories should
not fail the whole save (the ErrS05 reason the tree delete was dropped).
Check that the Bank preview and Bank Load use the same present rule.
Pre-existing: unrelated to the S097 `-Os` work.

---

## B4. Sequencer stumbles during a Scene Load into non-playing slots (`FS_OS=1` only)

**Report (user, 2026-11-17, `FS_OS=1` image).** The sequencer "stumbled" or
dropped a step while Scenes were loading, though the load did not target the
playing Scene. The logged load was one Scene into destination Scenes 9, 10, 11;
the facade was held for 10.9 s.

**Known so far** (S097 P2 implementation log §10):

- No `-Os` slowdown shows in operation durations or mixer cost. The background
  AutoSave slices are somewhat longer but sub-millisecond.
- The load's completion only updates the present mask; the Pattern-service gate
  covers only the destination Scenes.
- Nothing in the trace records foreground pass length or underruns, so a long
  main-loop pass (which delays the render and with it the trigger drain in
  `voiceControl_processPending()`) can be neither shown nor excluded.

**A/B result (user, 2026-11-17):** the same action on the `FS_OS=0` image
(the S097 P2 default) did **not** stumble (boot 85: one Scene into 13–15,
13.1 s). So the stumble is tied to the opt-in `FS_OS=1` build, which is now
off, and is not a defect in the default build. It stays here because `FS_OS=1`
cannot return until the long pass is found (S097 P2 implementation log §11).
If it is ever seen on the default build, it predates S097. Then candidates: the longest
fixed-work Scene-load phases (parse/apply of a Kit or Pattern in one pass),
per-track Scene playback (could a track be playing Scene 9–11? `seq_perTrackPattern`),
and any re-apply of instrument images or settings during the load. A DEV-only
trace record of the longest main-loop pass and of `audioCodec_underrunCount`
changes (with `current_op`/phase) would locate it. It would need sign-off: it
adds DEV RAM and touches `main.c`, which is in the LTO unit.

---

## B5. Scene Save needs several presses of OK

**Report (user, 2026-11-17).** It took a few encoder presses on OK before a
Scene saved.

**Known so far:** the trace has one Scene Save request (slot 49), and the
filesystem facade had been idle for 28.9 s before it. So the earlier presses
were not refused by filesystem work. They were consumed or ignored in the
menu (`menu_handleLoadSaveMenu()` and the Save confirm and overwrite flow), or
the encoder press was missed. **Next:** find out what the LCD showed between
presses (a confirm step, an overwrite question, nothing). Then trace the press
handling on the Save page. Unrelated to `-Os` (`menu.c` is `-O2`).

---

## Notes

- **Tooling:** `tools/diagnostic_tools/decode_devlogs.py` decodes
  `pattrace.bin` with the AutoSave stage table. Its records (type, flags,
  tick16, value32; `PatternTrace.h`) need their own decoder before they can be
  used.
- **Card state (2026-11-17):** a `.hcnamtmp` (2,181 B) sat beside `.hcnames`
  (2,179 B), a names-register write not yet promoted. The next boot (85)
  recovered it: its boot settle had `pending=scalar,repair` and took 2.1 s
  instead of ≈ 0.23 s, and the file is gone. Note it if it recurs after a clean
  shutdown.
