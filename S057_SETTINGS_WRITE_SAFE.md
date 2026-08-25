# settings.cfg Safe-Write — Implementation Plan

Status: **agreed design, not yet implemented.** Companion to
`S057_AUTOSAVE_WRITER_WRAP.md` §3b, which first flagged that every boot
re-serializes `settings.cfg` via a direct in-place truncate with no backup —
unlike AutoSave's `.hcprms1/2`, a power loss mid-write can leave it empty or
torn, silently reverting `active_bank` (and everything else) to firmware
defaults with no error surfaced. This document is the agreed fix design.
No code yet.

---

## 1. Why this is one fix, not two

`filesystem_markSettingsDirty()` is called from both Bank Load/Save
(`filesystem.c:10645, 10800, 14042`) and ordinary Global Menu edits, but
both feed the same debounced scheduler
(`filesystem_settingsWriterSchedule_tick()`), which starts the same single
operation, `FS_INTERNAL_OP_SAVE_GLOBALS`, which runs the same
`filesystem_saveGlobals_tick()` state machine. There is no separate
"autowriter" — fixing `filesystem_saveGlobals_tick()`'s write mechanism once
makes both triggers safe. **Bank Load's own code does not change at all.**

---

## 2. Design: temp file + validated promote

Replace the current single `afatfs_fopen(STORAGE_SETTINGS_FILENAME, "w", ...)`
truncate-in-place with:

1. Write the complete new content to a **temp file** (new-file create, not a
   truncate of anything meaningful — see §4).
2. Close it, **then explicitly sync** (see §5 — this is the load-bearing
   step).
3. Only after sync confirms the temp file is durably on the card: remove
   the existing `settings.cfg` (if present), then rename the temp file to
   `settings.cfg`.
4. At boot, before the existing settings load runs at all: check for a
   leftover temp file. If one exists, it proves a promotion was
   interrupted; validate and promote it, or discard it, before proceeding
   to the existing (unmodified) load path.

No new low-level AsyncFATFS code is needed — every primitive this reuses is
already implemented and already hardware-exercised elsewhere:

- `afatfs_fopen(name, "w", cb)` / `filesystem_writeTextLine()` — the same
  open-and-stream mechanism the current writer already uses, just pointed
  at a different filename.
- `afatfs_sync()` (`asyncfatfs.c:1144-1156`) — same call AutoSave's writer
  already uses three times per transaction.
- `afatfs_removeObjects_lfn(name, AFATFS_REMOVE_FILES_ONLY, cb)` — the same
  "remove every case-folded match of this name, tolerate zero matches"
  primitive AutoSave already uses to clear its inactive `.hcprms` target,
  and Instrument/Kit saves already use before rewriting member files.
- `afatfs_renameObject_lfn(old, new, matchMode, aliasOut, cb)` — the same
  primitive `filesystem_blockRename()` uses for Kit quarantine. Confirmed
  constraint: it fails with `AFATFS_RESULT_ALREADY_EXISTS` if the target
  name is already occupied (`asyncfatfs.c:4790`) — this is *why* step 3
  removes the old file before renaming, not instead of it.

---

## 3. Naming

**Plainly visible, no dot-prefix.** `.hcnames`/`.hcprms1/2` are opaque
binary wire-format files nobody's meant to read directly; `settings.cfg` is
plain text specifically meant to be human-inspectable, and a leftover temp
file after an interrupted write is exactly the evidence someone debugging a
bad boot should be able to find sitting in plain sight, not hidden.
Proposed name: `settings.tmp` — clearly paired with `settings.cfg` by name
alone. (Exact string TBD at implementation time; any plainly-visible,
obviously-paired name works.)

No separate cleanup step is needed on the success path: rename doesn't
copy, it relabels the existing object in place
(`ASYNCFATFS_REFERENCE.md:348-349` — "preserves the object's first cluster,
file size, attributes, timestamps"). Once the rename to `settings.cfg`
completes, `settings.tmp` simply no longer exists as a name. Only a
*rejected* (invalid or orphaned) temp file needs an explicit remove, during
boot recovery.

---

## 4. Why opening the temp file with "w" is safe (unlike today's scheme)

The current bug is dangerous specifically because `settings.cfg`'s *live,
trusted* content is what gets truncated in place. The temp file carries no
such risk: nothing depends on preserving whatever (if anything) was
previously sitting at that name. If a stale temp file exists from an
earlier interrupted attempt in the same session, truncating and
overwriting it is exactly the right behavior — it's about to be fully
rewritten from scratch either way.

---

## 5. Sync placement — the load-bearing correction to the original proposal

Initial design review raised: could a still-incomplete temp file ever get
promoted? Answer worked out in discussion: **no, provided sync is placed
correctly**, and that placement is the one piece that must not be skipped.

`afatfs_fclose()` completing means the write was accepted into the cache
and the close-time directory entry save was *queued* — not that it's
physically on the card. `afatfs_sync()`'s own doc comment calls this out
directly: "public persistence boundary for callers that have finished a
write flow... waits for dirty sectors and in-flight SD writes." AutoSave's
writer already treats close and sync as two separate required steps at
every one of its own irreversible junctures (`AUTOSAVE.md`: "closes **and
syncs** the invalid copy," "writes **and syncs** the CRC," "...closes,
**and syncs**"). The current `filesystem_saveGlobals_tick()` has *zero*
sync calls anywhere — a separate, smaller pre-existing gap this fix also
closes.

**Binding rule for implementation:** `afatfs_sync()` must be called after
closing the temp file and its completion must be awaited **before** the
old-file removal step starts. With that ordering:

- Crash before sync completes → removal of the old `settings.cfg` is never
  reached; it's untouched. Any temp file left behind was never confirmed
  durable and boot recovery must not trust its mere existence as proof of
  completeness (see §7).
- Crash after sync completes → the temp file is guaranteed fully durable
  (all data clusters and its final directory entry) before removal/rename
  ever starts. There is no window where a genuinely partial temp file is
  sitting there available to be promoted.

This is why the terminator line (§6) is not load-bearing for correctness —
sync is what actually closes the gap. The terminator is being kept anyway,
per instruction, for its own separate reason.

---

## 6. Terminator line — kept for future format-expansion, not for this fix

Requested explicitly: keep a terminator/count line even though sync alone
already makes promotion-of-a-partial-file impossible. Rationale: it gives
any future settings-format expansion a cheap, self-checking completeness
signal without needing a format-version bump or touching the sync-based
safety guarantee this fix already provides.

Current `filesystem_nextSettingsLine()` (`filesystem.c:12689-12768`) emits
17 lines (`case 0u`..`case 16u`: `format`, `version`, then 15 keyed
fields ending in `autosave`), `default:` returns 0 to signal end-of-content.

Add one new final line, **`case 17u`**, emitted last:

```
lines=17
```

— where `17` is the count of preceding key lines (not counting itself).
Whoever adds a new settings key in the future increments both the new
key's own case and this declared count; no other format change needed.

Validation rule for any consumer that wants a completeness check (used by
boot recovery in §7, not by the normal load path in §8): the file is
complete only if every line parses without error **and** the last line
encountered before EOF is a `lines=N` line whose `N` equals the number of
successfully parsed lines that preceded it. A clean-but-early EOF (crash
mid-content, but happens to land right after a syntactically valid line)
is now distinguishable from genuine completion, because the required
terminator would simply be absent.

---

## 7. Boot-time recovery — new prelude before the existing load

Add a new pass **before** `filesystem_loadGlobals_tick()`'s existing phase
0, which is otherwise completely unmodified:

**Pass A (new) — temp-file discovery and resolution:**

1. Attempt to open the temp file for read.
2. **Not found** → nothing to do; proceed directly to Pass B (existing load
   path, untouched).
3. **Found** → read and validate it using the existing
   `filesystem_parseSettingsLine()` (same function, no duplicated parser),
   plus the terminator check from §6:
   - **Valid** (every line parses, terminator present and count matches) →
     promote: remove the existing `settings.cfg` if present (tolerate "not
     found" as success, per the same singleton-removal discipline already
     established for `.hcprms`/`.hcnames` in `AUTOSAVE.md`; treat any real
     scan/open/type error as a hard error, not a silent pass), then rename
     the temp file to `settings.cfg`.
   - **Invalid** (any parse error, or terminator missing/mismatched, or a
     read/open error partway through) → discard: remove the temp file only.
     The existing `settings.cfg`, if any, was never touched and remains
     authoritative.
4. Either way, fall through to **Pass B**.

**Pass B (unchanged) — the existing `filesystem_loadGlobals_tick()`**, now
guaranteed to see a single, consistent `settings.cfg` (or none, on genuine
first boot) regardless of what happened on the previous boot's write.

This reuses one parser for both the recovery-validation pass and the real
load pass — no second implementation of "what does a valid settings.cfg
look like."

---

## 8. What does not change

- `filesystem_markSettingsDirty()`, the debounce scheduler
  (`filesystem_settingsWriterSchedule_tick()`), and every caller (Bank
  Load, Bank Save, Global Menu edits) — untouched. They only ever mark
  dirty or start `FS_INTERNAL_OP_SAVE_GLOBALS`; none of them know or care
  how the write itself is implemented.
- `filesystem_nextSettingsLine()`'s existing 17 field cases — untouched,
  only gains one new terminator case.
- The existing revision-check discipline at close
  (`op_settings_change_revision` captured at open, compared at finish to
  decide whether `fs_settings_dirty` clears or a newer mutation arrived
  mid-write and must re-arm) — must be preserved unchanged across the new
  phases; it doesn't interact with the promote mechanics at all.
- P1/P2 from `S057_AUTOSAVE_WRITER_WRAP.md` §3a/§3b (Bank Save present-mask
  union, redundant-boot-write accept/gate decision) — independent items,
  not entangled with this fix.

---

## 9. New phases (informal — exact enum values are an implementation detail)

**`filesystem_saveGlobals_tick()`**, appended after the existing
open/write/close sequence (now targeting the temp filename instead of
`settings.cfg` directly):

```
... existing OPEN / WAIT_OPEN / WRITE_LINES / CLOSE / WAIT_CLOSE (temp file) ...
SYNC        -> afatfs_sync()
WAIT_SYNC   -> block until sync reports complete
REMOVE_OLD  -> afatfs_removeObjects_lfn(settings.cfg, FILES_ONLY, cb)
WAIT_REMOVE
RENAME      -> afatfs_renameObject_lfn(temp -> settings.cfg, ...)
WAIT_RENAME
FINISH      -> filesystem_finish(...), same revision-check as today
```

**`filesystem_loadGlobals_tick()`**, new prelude before existing phase 0:

```
CHECK_TEMP        -> afatfs_fopen(temp, "r", cb)
WAIT_CHECK_TEMP
  not found  -> fall through to existing phase 0 (unchanged)
  found      -> READ_AND_VALIDATE_TEMP (reuses filesystem_parseSettingsLine()
                 + §6 terminator check)
    valid    -> REMOVE_OLD_SETTINGS -> RENAME_TEMP -> existing phase 0
    invalid  -> REMOVE_TEMP -> existing phase 0
```

---

## 10. Test plan (for the implementation session)

- Normal case: setting changed, debounce fires, `settings.cfg` updates,
  temp file does not persist afterward.
- Power-cut simulation (or actual hardware pull) at each new phase
  boundary: mid temp-write, right after temp close (before sync),
  right after sync (before remove-old), mid remove-old, mid rename. Confirm
  boot recovery reaches the correct outcome in every case (old file intact,
  or new file promoted — never neither, never a torn file surviving as
  `settings.cfg`).
- Confirm a temp file deliberately truncated short (simulating a crash
  mid-write that *did* reach sync somehow, or a hand-edited fixture) is
  rejected by the terminator check and discarded, leaving the existing
  `settings.cfg` in place.
- Confirm first-boot (`settings.cfg` doesn't exist, no temp file) is
  unaffected — falls straight through Pass A to the existing defaults path.
- Confirm the Bank Load fallback case (boot requests a Bank that doesn't
  exist, falls back, `settings.cfg` must reflect the actual loaded Bank)
  still works unchanged, since `filesystem_markSettingsDirty()` itself is
  untouched.
