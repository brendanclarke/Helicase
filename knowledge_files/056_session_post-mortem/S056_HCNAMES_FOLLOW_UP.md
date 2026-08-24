# Session 056 — HCNAMES Follow-Up

Investigation into `/.hcnames` — the 129-row Bank/Scene/Kit/Instrument
identity and provenance register — across two hardware test sessions and two
card captures (`SD_CARD/`, `SD_CARD2/`). Findings and plan only; no code was
changed by this document.

> ### How to read this document
>
> **To implement, go straight to §11 — "Full remediation plan".** §11.0 lists
> every correction §11 makes to the sections above it.
>
> This document was written incrementally as the investigation ran. It
> contains **three diagnoses that turned out to be wrong** and **two earlier
> plan sketches that are superseded**. Each is banner-marked in place. They
> are kept because the reasoning that eliminated them is the evidence base
> §11 rests on — not because any of them should be built.

| Section | Status |
|---|---|
| §1, §2 | Correct. `settings.cfg` and the AutoSave Bank section verified good. |
| §3 | **RETRACTED.** Mask-selection theory, disproven in §8. |
| §4 | **SUPERSEDED.** Observation correct; both hypotheses wrong; diagnostic replaced by §11.3. |
| §5 | Partly superseded — see §10.1 on the trace file's real structure. |
| §6 | Correct. |
| §7 | **SUPERSEDED** by §11.8. |
| §8 | Payload verification correct; its "five-row subset" framing superseded by §9.2. |
| §9.1–§9.4 | Correct and load-bearing. |
| §9.5 | **WITHDRAWN.** The read-hold-write differential is not the cause. |
| §9.6 | Correct. Retracts the premise of `S056_NAMES_CORRUPTION.md`. |
| §9.7 | **SUPERSEDED** by §11. |
| §9.8 | **CONTAINS A FALSE TABLE.** Use §10.2 instead. |
| §9.9 | **SUPERSEDED** by §11.8. |
| §10.1–§10.3 | Correct and load-bearing: the publication map and the two-defect split. |
| §10.4–§10.5 | **SUPERSEDED** by §11.2–§11.6. |
| **§11** | **The plan. Implement from here.** |
| **§12** | Implementation review of the landed §11 work, plus one required correction to §11.3's verify policy. |

## What is actually wrong — final position

- **`/.hcnames` has not been written by any Load or Save since Session 055.**
  It is byte-identical across both card captures and the `11482fd` commit,
  through five Bank operations, two root Scene Loads, a Kit Load and several
  Instrument Loads. (§9.1, §10)
- **Defect A — publication coverage.** Kit Load, Kit Save and Instrument Load
  publish no identity rows at all; Scene Load/Save publish only one of the
  three row classes they change; the missing remainder was delegated to a
  Menu UI-boundary flush that has **never executed once** in 69,012 recorded
  trace events. `FS_INTERNAL_OP_UPDATE_HCNAMES_KIT` has no producer anywhere
  in `filesystem.c`. (§10.1, §10.2)
- **Defect B — the write does not land.** A root Scene Load's register
  transaction and both Bank writers complete `FS_STATUS_DONE`, with zero
  error and zero stall records in the whole trace, and the card does not
  change by one byte. Root cause not identified; §11.3's read-back gate is
  what will settle it. (§9.3, §9.4, §10.3)
- **Source staging is already correct at all eight operations.** The defect
  is publication only — nothing needs to add or change
  `filesystem_setResidentSource()` calls. (§11.0)
- **`settings.cfg` and the AutoSave records are correct and durable.** The
  Session 056 Part B settings fix and the `K` completion witness both work,
  and are what made this diagnosis possible. (§1, §9.3)
- **The premise of `S056_NAMES_CORRUPTION.md` is retracted.** Row 9's
  `Moch to` is a completed Scene-level operation from Session 055, not a
  Bank-Load name leak. The snapshot fix that landed is harmless hardening,
  but it was never validated by card evidence. (§9.6)

---

## 1. Confirmed working

```
$ cat SD_CARD/settings.cfg | grep active_bank
active_bank=15
```

Matches the last Bank Loaded (15), not the Bank Saved (16) or the boot Bank
(11) — exactly right, and durable (present after power-off), which is
specifically what `S056_BANK_SETTINGS_CORRECTION.md` set out to fix.

```
$ python3 -c "..." # direct byte read, both records
.hcprms1 (gen 11): slot=15 name='LoadTst!' mask=0xffff active_scene=10 voice_mask=0x0400
.hcprms2 (gen 10): slot=15 name='LoadTst!' mask=0xffff active_scene=10 voice_mask=0x0400
```

Both AutoSave generations agree on Bank 15's identity — not just the
winning record but the previous one too, which is a stronger signal than a
single lucky write: the Bank-identity fields have been stable and correct
across at least two consecutive AutoSave publish cycles.

## 2. Not a bug: `active_scene`/`voice_mask` vs. Bank 15's own file

`tools/verify_bank_autosave.py` flags:

```
.hcprms1 active_scene: expected 12, got 10
.hcprms1 voice edit mask: expected 0x1000, got 0x0400
```

"Expected" here comes from `Bank/015 LoadTst!/bankset.bcg`
(`active_scene=12`, `scene_mask_voice_edit=1000`) — Bank 15's own *saved
default*. The AutoSave record instead shows scene 10 (bit 10 = `0x0400`).
This is exactly what should happen if the active/edited Scene was switched
to 10 sometime after the Load — which the trace supports directly: a long
run of `D` (dirty-mark) records for `Scene10 instrument[5] normal[3]` and
`normal[4]`, with steadily increasing tick values consistent with a knob
being turned over several seconds, followed by a full AutoSave write cycle
(`A`→`V`→`M`→`C`→`P`→`T`) that published exactly this state as the new
winning generation. The validator's "expected" value is a reasonable
default assumption (nothing changed after Load), but it isn't what actually
happened in this test, and the record is correctly tracking live state, not
the Bank's static file default. Not a defect.

## 3. Explained (pending one confirmation from you): scenes 06/08/10/12 stale

> **RETRACTED — do not implement or reason from this section.** All 16
> Scenes were loaded; §8 proves it against the complete AutoSave payload,
> and §9.2 then showed the *entire* register was stale rather than four
> blocks of it. Kept only as a record of a wrong turn.


```
HCNAMES row 7 Scene 06 / row 23 Kit 06 / rows 69-74 Instruments: stale
HCNAMES row 9 Scene 08: stale ('Moch to' again — see below)
HCNAMES row 11 Scene 10 / row 27 Kit 10 / rows 93-98 Instruments: stale
HCNAMES row 13 Scene 12 / row 29 Kit 12 / rows 105-110 Instruments: stale
```

All 12 *other* Scene/Kit/Instrument blocks (00-05, 07, 09, 11, 13-15)
correctly match `Bank/015 LoadTst!/`'s actual on-disk content byte-for-byte.
Only these four are wrong, and consistently as a whole block each (Scene
name, Kit name, and all six Instrument names for that slot together) — not
a scattered, partial corruption.

`filesystem_cacheCurrentBankSceneNameBlock()`'s own doc comment (confirmed
by direct code reading) states this is by design: *"unmasked resident
Scenes are deliberately never touched, preserving both their payload/
name/source pairing during every mask-selective Bank Load."* Load:Bank lets
you deselect individual child scenes via the LED row before confirming; any
deselected scene's HCNAMES rows (and its resident payload) are supposed to
be left exactly as they were before the Load. If your Load:Bank selection
for slot 15 excluded scenes 6, 8, 10, and 12, this is precisely the
documented, intended outcome — not a bug.

**I can't independently confirm this from the card alone**: the direct
evidence that would prove it (the `B` present-mask-at-commit record, or the
missing `K` record's own value) never reached `asavetrc.bin` this session
(§4). The four affected numbers themselves don't hint at anything else —
they're just four of sixteen slots, no more suspicious a subset than any
other.

**Please confirm**: when you loaded Bank 15, did you select all 16 scenes,
or deselect any? If you selected all 16, this explanation is wrong and the
per-child overlay mechanism has a real, reproducible defect affecting
specifically slots 6/8/10/12 (an oddly regular pattern — every other slot
starting at 6 — that would be worth its own dedicated investigation). If
you deselected some, this is fully explained and not a concern.

One more data point either way: HCNAMES row 9 (Scene 08) again reads `Moch
to` — the exact same unrelated root-library value from the *first* Session
056 test (`S056_NAMES_CORRUPTION.md`), which the snapshot fix was written
to prevent. Since Bank 15's own Scene 08 happens to also be named
`KitWool` (coincidentally matching Bank 12's), and row 25 (Kit 08) is *not*
flagged as wrong, the simplest reading is that slot 8's overlay simply
never ran this time either (mask-excluded, per the theory above) rather
than that the fix failed — if it had run and been corrupted again, I'd
expect the Kit row to disagree from the Scene row the same way it did the
first time, and it doesn't. Worth keeping in mind for the retest in §5
regardless.

## 4. Unresolved: HCNAMES row 0 (Bank identity) is stale, and the code doesn't explain it

> **SUPERSEDED.** The observation is correct — row 0 is stale — but both
> hypotheses below are wrong, and the "recommended next diagnostic" is
> replaced by §11.3 Group B. §9.1 shows the file had not been written at
> all, which is why nothing in this section adds up.


```
HCNAMES row 0 Bank name: expected 'LoadTst!', got 'LoadTst'
HCNAMES row 0 Bank source: expected '015', got '012'
```

Row 0 currently reads exactly `LoadTst\t012` — the Bank identity from the
*first* Session 056 test (Bank 012), unchanged. It doesn't match any of the
three Bank operations that ran this session: not the boot Load of Bank 11
("Full"), not the Save to Bank 16 ("FullZ"), not the interactive Load of
Bank 15 ("LoadTst!"). Unlike §3, this can't be explained by mask selection:
row 0's update has no dependency on which child scenes were selected.

I traced the exact code path a non-empty Bank Load takes
(`filesystem_loadBankDirectory_tick()`'s child-loop-exhausted branch, the
same one confirmed working for §1/§2) and found something that doesn't add
up. In that one straight-line block — no branches, no early returns, no
async gap — in this order:

```c
bank_setRestoreBankSlot(op_slot);          // feeds .hcprms1's Bank slot (confirmed correct)
filesystem_markSettingsDirty();            // feeds settings.cfg (confirmed correct)
...
filesystem_cacheResidentName(0u, op_bank_display_name);   // feeds HCNAMES row 0
op_phase = 83u;   // -> opens .hcnames for write, streams all 129 rows, closes+flushes, then FS_STATUS_DONE
```

Since `settings.cfg` and `.hcprms1`'s Bank section are both confirmed
correct, the lines that produce them *must* have run — and
`filesystem_cacheResidentName(0u, op_bank_display_name)` is in the exact
same block, a few lines later, with nothing between them that could skip
it. The subsequent write-to-disk phases (open `.hcnames` for `"w"`, stream
all 129 cached rows including row 0, close, `filesystem_finish(FS_STATUS_DONE)`
— which itself waits for a final FAT sync before publishing DONE) are
equally unconditional. By this reading, row 0 *should* have been written
correctly to disk as part of this Load, the same as the fields sitting
right next to it in the same block.

I don't have a confirmed explanation for the contradiction. Two honest
possibilities:

1. **Something after the Load overwrote row 0 with stale data.** HCNAMES
   uses a general "read all 129 rows, overlay only the rows this operation
   owns, rewrite the whole file" pattern for several other operation types
   (Kit/Instrument/Scene-level updates), not just Bank Load. If any of
   those ran later in the session — triggered by something during "change
   some parameters" — and its own read of the 129-row register used a
   stale in-RAM copy instead of freshly reading the just-updated file, it
   would rewrite the *entire* file including row 0, silently reverting it
   to whatever that stale copy held. I did not find or rule out a specific
   site that does this; it's a plausible mechanism, not a confirmed one.
2. **The Load's own write to row 0 didn't actually happen the way the code
   reads**, for a reason I didn't find in this pass (e.g. a `fs_list_cache_name[0]`
   write happening earlier than I think, then getting clobbered by
   something else in the *same* borrowed-cache array before phase 85
   streams it out — the exact class of hazard `S056_NAMES_CORRUPTION.md`
   already found once for the Scene-name row specifically, but that fix
   was scoped to the Scene-name field only, not row 0).

Either way, this looks like a real, separate defect from what
`S056_NAMES_CORRUPTION.md` already fixed, not evidence that fix failed —
that fix specifically targeted the Scene-name row via a Bank-Load-owned
snapshot; row 0 is populated through a completely different code path
(`filesystem_cacheResidentName(0u, op_bank_display_name)` inside
`filesystem_loadBankDirectory_tick()`, not the per-child block the
Names-Corruption fix touched).

### Recommended next diagnostic (not implemented)

Mirroring the bracket-and-compare pattern already used for the `H` stage:
add one trace record right after phase 86 closes `.hcnames`, recording the
first few bytes of `fs_list_cache_name[0]` (or a cheap fingerprint of it) —
i.e., prove exactly what the Bank Load itself wrote for row 0, independent
of whatever the file shows after everything else that session does. If
that record shows the *correct* value, the defect is downstream (theory 1
above, and the next place to look is whatever else touches the 129-row
register later in the session). If it shows the *wrong* value already,
the defect is upstream inside this same block despite how the code reads,
and the next step is instrumenting `op_bank_display_name`/
`fs_list_cache_name[0]` directly around that assignment.

## 5. Missing `K` record for the interactive Bank Load

> **PARTLY SUPERSEDED.** The missing-`K` observation held for that card,
> but §10.1 established that `asavetrc.bin` is a cumulative append log of
> ring flushes spanning every session — which also explains the
> "not chronological" ordering noted at the end of this section. Later
> captures contain `K` records for every Bank operation.


`asavetrc.bin` for this session contains exactly two `K` records:

```
t=1854  K kind=Load DONE=1 slot=11   (boot, using the still-stale settings.cfg=11 left from test 1)
t=31285 K kind=Save DONE=1 slot=16
```

There is no `K` record anywhere for a Load of slot 15, even though its
downstream effects (settings.cfg, both AutoSave generations) are
confirmed durable. `on_bank_load_complete()` emits `K` unconditionally as
its first action, so this means either the record never got flushed from
the SRAM ring to `asavetrc.bin` before power-off, or — much less likely,
given the durable evidence that the Load succeeded — something skipped the
callback entirely on a path that still somehow reached the settings/AutoSave
writes (no such path was found in this session's reading). The far more
likely explanation is the first one: this is the same known trace-ring/
flush-timing limitation flagged in `S056_BANK_TESTS.md` §3.3 and
`DRAFT_TRACE_SPLIT_BY_MODULE.md`, not a new problem, and not something
`S056_BANK_SETTINGS_CORRECTION.md`'s fix was meant to address (that fix
guarantees settings.cfg's *durability*, not the trace ring's).

Separately worth flagging: this file's records are **not stored in simple
chronological file order**. Large bursts of `D` records with low tick
values (e.g. ~2261-7279, an early parameter-edit session) appear *after*
records with much higher tick values (e.g. the t=31285 Bank Save, and two
full AutoSave publish cycles around t=46000-47778) later in the file. Tick
values are still internally consistent and trustworthy for ordering events
*within* this boot session, but file position is not a safe proxy for
"what happened when" — I used tick values throughout this document, not
line position, for that reason. This reordering itself might be worth
understanding at some point (it suggests the ring/flush mechanism doesn't
drain in strict FIFO order under contention), but it's outside this
session's scope.

## 6. Other files checked

- `Bank/016 FullZ/` — 16 children, valid `bankset.bcg`, structurally sound.
  Consistent with a Save of whatever was resident (Bank 11-derived) before
  loading Bank 15.
- `Bank/015 LoadTst!/` — all 16 children present on disk (ruling out "Bank
  15 just doesn't have those slots" as an explanation for §3).
- `.hcprms1`/`.hcprms2` headers both valid (`HCPR`, version 1, commit
  `0xA5`); generations 11 and 10 respectively — a normal, healthy
  alternating A/B sequence, not itself a sign of trouble.
- No root-library Kit/Scene slot is named `LoadTst` (only `LoadTst!` exists,
  as the Bank 15 directory name) — ruling out a root-library
  cross-contamination (the "Moch to" mechanism) as the source of row 0's
  stale value; that value is specifically Bank 12's old identity, not
  anything currently in the root Kit/Scene library.
- Did not find anything unusual in a spot check of `Instrument/`, `Kit/`,
  `Scene/`, `samples/`, `loops/`, or `.fseventsd`/`.Spotlight-V100`
  (host/OS artifacts, unrelated to firmware behavior).

## 7. Recommended retest

> **SUPERSEDED by §11.8.** Item 1 tests the retracted mask theory; item 2's
> diagnostic is replaced by §11.3 Group B.


1. Confirm (or rule out) §3's mask-selection theory: repeat a Bank Load
   with **all 16 scenes explicitly selected**, and check whether all 16
   HCNAMES blocks come out correct. If any of 6/8/10/12 (or any other slot)
   is still wrong with a full selection, that upgrades §3 from "likely
   explained" to "a real bug," worth its own investigation.
2. Regardless of §3's outcome, row 0 (§4) needs its own attention — it
   isn't explained by anything this session found. If you're willing to
   land the diagnostic sketched in §4 before the next test, it would
   directly resolve which of the two hypotheses is correct instead of
   requiring another round of forensic reconstruction from `.hcnames`
   alone.
3. If practical, wait a beat after "change some parameters" before power-off
   (a few extra seconds) to see if the missing `K` record (§5) is purely a
   flush-timing artifact or something more persistent — same reasoning as
   the retest recommendation in the original `S056_BANK_TESTS.md`.

---

## 8. Correction — §3's mask-selection theory was wrong; verified against full AutoSave payload

> **Payload verification correct; framing superseded.** The proof that all
> 16 Scenes loaded correctly stands and is still load-bearing. The
> "stable five-row subset" conclusion does not: §9.2 shows every row was
> stale, and the twelve "correct" rows matched only because Banks 012 and
> 015 happen to share twelve child names.


You were right to push on this, and right about the outcome: **all 16
scenes were loaded**, and the full AutoSave payload proves it. I should
have checked this before writing §3 instead of stopping at
`tools/verify_bank_autosave.py`'s own limited sampling (its own docstring
says it "samples the active Scene and the first discovered child... to keep
the check bounded" — it never checked scenes 6/8/10/12 at the payload level
at all, only at the HCNAMES level, so it could never have caught this).

I went back and decoded `.hcprms1`'s full Scene section (`SCENE_OFFSET
+ scene*SCENE_BYTES`, all 16 scenes) and compared every field against
`Bank/015 LoadTst!/`'s actual on-disk tree — not the sampled subset the
validator checks, all of it:

- **Scene name** (all 16): exact match, including Scene 06 (`FilModZ`),
  Scene 08 (`KitWool`), Scene 10 (`FilModZ2`), Scene 12 (`FilModZ2`).
- **Embedded Kit name** (all 16): exact match, including Kit 06
  (`RedSnapz`), Kit 08 (`KitWool`), Kit 10 (`808ceebe`), Kit 12 (`808ceebe`).
- **Scene settings block** (all 16 × 40 values — morph amount, per-voice
  morph, decimation, audio-out routing, FX send, fader, MIDI channel/note):
  byte-for-byte match against each scene's `sceneset.scg`, zero
  differences anywhere, including 6/8/10/12.
- **Instrument type + name** (all 16 scenes × 6 instrument slots = 96
  entries): byte-for-byte match against each Kit's `kitset.kcg` and member
  files, zero differences anywhere, including the four scenes in question.

Every single checkable field in the AutoSave record agrees with Bank 015's
actual saved content, for every scene, with no exceptions. There is no
evidence anywhere in the payload of a partial or mask-excluded load. §3's
theory is **wrong** — withdrawn.

**What this actually proves**: the Bank Load correctly read, validated, and
committed all 16 children into resident memory (which is what AutoSave's
Scene section reflects — live resident state, not the HCNAMES file). The
defect is entirely confined to the **HCNAMES identity register specifically**
for a *reproducible, specific set of rows* — not a masking artifact, not a
scattered data-corruption issue, and not something that also affected the
actual musical data.

### "A few parameters were changed, but not many" — also checked, also consistent

The 40-value settings block matching exactly for every scene means whatever
you changed wasn't a Scene-level setting (morph/routing/fader/MIDI). The
trace's dirty-mark evidence (`Scene10 instrument[5] normal[3]` and
`normal[4]`, repeated with climbing tick values, i.e. a knob being turned)
points at instrument *parameter* values specifically, which this check
doesn't cover byte-for-byte (they're variable-shape per instrument type and
expected to diverge from the stock `.drm`/`.snr`/`.cym`/.`hat` file the
moment anyone touches a knob — that's the point of editing). Nothing here
contradicts "a few parameters changed" — it's consistent with exactly that,
localized to Scene 10 as §2 already found, and not a data-integrity concern.

### The real pattern, now confirmed reproducible across two sessions

The HCNAMES rows that are wrong are **the exact same rows, with the exact
same stale values, that were wrong in the first Session 056 test**:

| Row | Currently shows | Matches |
|---|---|---|
| 0 (Bank) | `LoadTst` / source `012` | Bank 012's identity — Session 056 **test 1's** loaded Bank, byte-for-byte |
| 9 (Scene 08) | `Moch to` | The exact same root-library leak value found and "fixed" in test 1 |
| 7, 23, 69-74 (Scene/Kit/Instr 06) | stale | Not Bank 15, not Bank 11 (boot), not Bank 16 (save) |
| 11, 27, 93-98 (Scene/Kit/Instr 10) | stale | Not Bank 15, not Bank 11, not Bank 16 |
| 13, 29, 105-110 (Scene/Kit/Instr 12) | stale | Not Bank 15, not Bank 11, not Bank 16 |

Root Kit/Scene slot 004 is still literally named `Moch to` on disk right
now (re-checked this pass) — so row 9's value is at minimum still
*available* to leak from the same root-library source as test 1, though I
can't tell from the card alone whether it's frozen at test 1's value
untouched, or being independently re-written to the same wrong value on
every subsequent Bank operation. Either way, this session ran **three**
Bank operations that each reach the row-0-writing code path — the boot Load
of Bank 11, the Save to Bank 16, and the Load of Bank 15 — and *none* of
them changed rows 0, 7, 9, 11, or 13, even though §4 already established
that the code responsible for row 0 sits in the same unconditional,
unbranched block as the code that *did* correctly update `settings.cfg`
and `.hcprms1` every single time. Combined with §4's finding, this is no
longer "one anomaly, unexplained" — it's a **specific, stable, five-row
subset of the HCNAMES register that has not been successfully written by
any Bank Load or Save since test 1**, while both the actual resident/
AutoSave payload (this section) and every other HCNAMES row keep updating
correctly on every operation.

That stability is itself informative: a per-attempt race (e.g. an
occasional flush-timing miss) would be expected to produce *different*
wrong values on different runs, or affect a different subset each time.
Getting the identical wrong values on the identical five rows across a
boot Load, a Save, and an interactive Load — three separate operations,
one full session apart — points toward something structural about those
five row *positions* (or whatever feeds them) rather than a timing
coincidence repeating itself exactly three times in a row.

### Revised assessment

I don't have a confirmed root cause, and I'd rather say that plainly than
guess again without checking. What's now solid:

- Not a masking artifact (§8, this section — disproven with full-payload evidence).
- Not a resident-data or AutoSave defect (§8 — everything checkable matches Bank 015 exactly).
- Row 0 is written by code sitting in the same unbranched block as
  known-correct `settings.cfg`/`.hcprms1` writes (§4), so it isn't simply
  "that whole commit block didn't run."
- The same specific rows fail the same specific way across independent
  operations and across sessions — this is reproducible, not flaky.

The most targeted next step is still the diagnostic sketched in §4,
extended to cover all five rows rather than just row 0: a trace record
(or five) capturing what `fs_list_cache_name[0]`, `[7]`, `[9]`, `[11]`, and
`[13]` actually hold at the moment phase 86 closes `.hcnames` — i.e., prove
directly whether the *write* has the wrong data already staged (a
cache-population bug specific to these five positions) or the *right* data
staged and something downstream still reverts it before the read you're
seeing on the card. Given the reproducibility just established, this
should be very cheap to catch on the next test with that instrumentation
in place — no special reproduction steps needed, it appears to fail the
same way every time.

---

## 9. Confirmed: `/.hcnames` has not been written since Session 055

§4 and §8 both assumed the register was being rewritten and asked *why
five rows came out wrong*. That premise was wrong. The correct question
is why **no** row ever changes. Everything below is measured, not inferred.

### 9.1 The decisive measurement

`SD_CARD/.hcnames` is tracked in git, and the card has been re-copied into
the repo after every test. The file is **byte-identical to the version
committed in `11482fd` ("documentation for session 054-055")**:

```
$ git show HEAD:SD_CARD/.hcnames | cmp - SD_CARD/.hcnames
IDENTICAL to HEAD
$ git log --oneline -- SD_CARD/.hcnames | head -3
11482fd documentation for session 054-055
0827b40 patch up loading stub pattern along with scene; session 054 testing ongoing
b9bdb92 session 054 complete ...
```

It did **not** change in `b0c3c87` ("056 pre-implementation documentation",
the card snapshot from Session 056 test 1), and it has not changed now.
`.hcprms1`, `.hcprms2` and `settings.cfg` — same root directory, same
card, same copy operation — did change in both snapshots. So the card was
copied correctly and the firmware's other root writers work; only
`/.hcnames` is frozen.

That covers **five Bank operations across two test sessions**: test 1's
interactive Bank Load of 012, and test 2's boot Load of 011, Save to 016,
and Load of 015 — plus whatever ran at the end of Session 055.

### 9.2 The whole file is one old image, and the "correct" rows were a coincidence

Comparing every Scene row against both Banks on the card:

| HCNAMES row | on card | Bank 012 child | Bank 015 child |
|---|---|---|---|
| 1..6 (Scenes 00-05) | Barf, Slak, RedSnap, Pop, Brezel, Rollin | same | same |
| 7 (Scene 06) | `FilMod` | **FilMod** | FilModZ |
| 8 (Scene 07) | SoyEared | same | same |
| 9 (Scene 08) | `Moch to` | KitWool | KitWool |
| 10 (Scene 09) | Goa | same | same |
| 11 (Scene 10) | `Electro` | **Electro** | FilModZ2 |
| 12 (Scene 11) | Eris | same | same |
| 13 (Scene 12) | `CasioPop` | **CasioPop** | FilModZ2 |
| 14..16 (Scenes 13-15) | 808ceebe, 808cb, Pop | same | same |

Every row is Bank **012**'s content. Kit rows 17..32 and Instrument rows
33..128 likewise. The twelve rows §3/§8 called "correct" are correct only
because Banks 012 and 015 happen to share twelve of sixteen child names.
There was never a five-row subset — the register is 100% stale.

### 9.3 The Bank writer provably runs to completion

From `asavetrc.bin` (60,396 records), the Bank Save to slot 16:

```
58181 O t=31033 type=BANK cp=SOURCE_STAGED slot=16   <- filesystem.c:14122, immediately after
                                                        filesystem_cacheResidentName(0u, ...) and
                                                        filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, 16)
58182 O t=31105 type=BANK cp=FINISH        slot=16   <- filesystem.c:14200, phase 86, *after*
                                                        afatfs_fclose() + afatfs_chdir(NULL) completed
58183 K t=31285 flags=0x03 (kind=Save, DONE) slot=16
```

Phases 83 (open `"w"`) → 84 (wait) → 85 (stream 129 rows) → 86 (close +
flush) all executed, in 72 ms, with no error. The whole trace contains
**zero `E` records** (the universal `filesystem_complete()` error witness,
which no operation can bypass) and **zero `X` stall records**. Nothing
failed and nothing hung.

### 9.4 What that forces

`filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot)` cannot fail
for a valid slot (`filesystem_residentSourceValid(0, 16)` → row 0 < 129,
source 16 < `FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT`), it sets
`fs_resident_source[0] = 16 | FS_RESIDENT_SOURCE_DIRTY_FLAG`, and the
dirty flag exists precisely so a later re-read of the old file cannot
overwrite it. Phase 85 then formats row 0 from
`fs_resident_source[0] & FS_RESIDENT_SOURCE_VALUE_MASK`, so the file
**must** have received `\t016`. It contains `\t012`.

Row 0's *name* is equally stale: `filesystem_cacheResidentName(0u,
op_bank_display_name)` would have written `FullZ`.

So both halves of row 0 were streamed with the values that were read off
the card at phase 0, not the values staged at the commit 70 ms earlier.
**The register the writer streams is not the register the commit
updates** — for the full duration of a Bank operation.

### 9.5 The structural difference: read-hold-write vs. read-write

> **WITHDRAWN — see §10.3 and §11.0.** The read-hold-write differential
> below is not the cause: a root Scene Load reads and writes the register
> adjacently, with no traversal in between, and fails identically. The
> claim that the generic writer "works (proved twice in 054/055)" is also
> unsupported — source token `004` is produced identically by a Scene Load
> *from* slot 004 and a Scene Save *to* slot 004, and both
> `/Scene/004 Moch to` and `/Kit/004 Moch to` exist on the card.


This is the part that identifies the defect, because one HCNAMES writer on
this card demonstrably still works. Between `b9bdb92` and `11482fd` the
register *was* rewritten twice — both times by the **generic** writer
(`filesystem_residentNames_tick()`, filesystem.c:4920), publishing a root
Scene Load. Its signature is unmistakable: row 9 reads `Moch to<TAB>004`,
a **direct root-Scene slot** source token. Bank Load never writes that —
`filesystem_cacheCurrentBankSceneNameBlock()` always writes
`FS_RESIDENT_SOURCE_INHERIT` (`-`) for Scene rows.

Compare the two shapes:

| Writer | HCNAMES read | work in between | HCNAMES write | status |
|---|---|---|---|---|
| `filesystem_residentNames_tick()` phases 0→6 (root Scene/Kit/Instrument publish) | phase 0, `"r"` (4949) | **none** — overlay applied in phase 3 (5030-5035), write opened in the same phase | phase 3, `"w"` (5037) | **works** (proved twice in 054/055) |
| `filesystem_loadBankDirectory_tick()` | phase 0, `"r"` (10200) | the **entire Bank traversal**: 16 × (opendir, chdir, rescan, full Scene payload load incl. embedded Kit + 6 Instruments + pattern + effects) | phase 83, `"w"` (11134) | **never lands** (5/5 ops) |
| `filesystem_saveBankDirectory_tick()` | phase 0, `"r"` (13720) | the **entire Bank tree delete + recreate + 16 child Scene saves** | phase 83, `"w"` (14135) | **never lands** |

Bank Load and Bank Save are the **only two operations in the file** that
borrow the 129-row register at the start of the operation, hold that borrow
across hundreds of intervening phases and dozens of file/directory
operations, and write it at the end. Every other HCNAMES publisher reads
and writes adjacently.

The borrow itself is fragile by construction, and the code says so:
`fs_list_cache_name[1000][9]` is the shared browser cache
(filesystem.c:869), tagged by `fs_list_cache_kind`. Two documented
behaviours turn any interference into a **silent** failure:

- `filesystem_cacheResidentName()` (4711) begins
  `if (row >= FS_RESIDENT_NAMES_ROW_COUNT || fs_list_cache_kind != FS_NAME_CACHE_HCNAMES) return;`
  — every overlay call becomes a no-op, with no return value, no trace,
  and no error, the moment the cache tag changes.
- `filesystem_clearNameCacheStorage()` (1398) `memset`s all 9,000 bytes and
  sets `fs_list_cache_kind = FS_NAME_CACHE_NONE`. Callers such as
  `filesystem_recordInstrumentFile()` (7819) and
  `filesystem_updateInstrumentCacheAfterSave()` (8192) do this
  *conditionally*, mid-traversal, as a side effect of ordinary Instrument
  work — exactly the kind of work a Bank child load performs.

The value evidence tells us which variant actually occurred: the streamed
rows were the **on-card values**, not blanks and not library names. That
is the signature of the register being **re-populated from the file**
(`filesystem_cacheResidentRecord()`, 4652) after
`filesystem_clearResidentSourceDirtyFlags()` (4735) had already dropped
the protection flag — i.e. the borrow is being taken over and re-primed by
another HCNAMES transaction during the Bank traversal, so the Bank's
commit writes land in a register that is subsequently replaced by the
card image before phase 85 streams it.

Whichever of those interference paths fires, the remedy is the same and is
given below: the Bank operations must stop depending on a shared,
unguarded, silently-clobberable borrow surviving an entire tree traversal.

### 9.6 Retraction — there was never a Bank name-corruption bug

Row 9's `Moch to<TAB>004` is a **completed root Scene Load of root Scene
slot 004 into resident Scene 08**, performed in Session 055 and written by
the generic writer. Its `004` source token proves it: Bank Load writes
`-`. It is not, and never was, evidence that `op_scene_display_name`
leaked into a Bank child's Scene row.

That withdraws the premise of `S056_NAMES_CORRUPTION.md`. The
`op_bank_child_scene_display_name` snapshot that landed is still correct,
cheap, defensible defensive hardening, and it should stay — but it fixes a
latent hazard, not the observed symptom, and its `H` drift record cannot
fire until §11 lands, because the Bank writer's output never reaches the
card at all. It should not be treated as validated by any `.hcnames`
evidence.

`S056_BANK_SETTINGS_CORRECTION.md`'s two fixes are unaffected and are what
made this diagnosis possible: `settings.cfg=15` is durable, and the `K`
witness is the record that let §9.3 prove phase 86 ran.

---

### 9.7 The targeted fix

> **SUPERSEDED by §11 — do not implement from this section.** Change 1
> (the borrow lock) is withdrawn outright: it has no known offender.
> Changes 2, 3 and 4 survive, renumbered into §11.4 (C1/C2), §11.3
> (Group B) and §11.6 (Group E).


Design rule: **a Bank operation must never rely on the shared name cache
surviving its own traversal, and must never be able to report `DONE` on an
HCNAMES write it did not actually make.** Three changes, ~60 lines, no new
persistent SRAM beyond one byte.

#### Change 1 — Lock the register borrow for the duration of a Bank operation

*File:* `Core/Hardware/SD/filesystem.c`

Add beside `fs_list_cache_kind` (filesystem.c:1326):

```c
/*
 * One-byte exclusive claim on the borrowed 129-row HCNAMES register.
 *
 * What: set while a Bank Load/Save owns fs_list_cache_name as an HCNAMES
 * image, i.e. from the moment its preserve-read closes until its own
 * rewrite has closed and flushed. Why: Bank is the only operation that
 * holds the borrow across an entire tree traversal, and both
 * filesystem_clearNameCacheStorage() and the cache-kind guard inside
 * filesystem_cacheResidentName() fail *silently*, so an unrelated
 * Instrument/index cache transition during that traversal discards the
 * Bank's staged rows with no error, no trace, and a DONE result.
 * Inputs: Bank Load phase 82 / Bank Save phase 82 set it, Bank Load and
 * Bank Save phase 86 clear it, filesystem_start() clears it defensively.
 * Output: cache-domain transitions are refused and witnessed instead of
 * silently destroying the register. Affiliates:
 * filesystem_clearNameCacheStorage(), filesystem_prepareLibraryNameCache(),
 * filesystem_prepareResidentNamesCache(), filesystem_cacheResidentName().
 */
static uint8_t fs_list_cache_hcnames_locked = 0u;
```

Guard the three transition points:

- `filesystem_clearNameCacheStorage()` (1398): return immediately if
  locked, after emitting one `AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT`
  (`'H'`) record with a new flag bit meaning *cache-clear refused*.
- `filesystem_prepareLibraryNameCache()` (1435): same guard.
- `filesystem_prepareResidentNamesCache()` (4599): same guard — a nested
  HCNAMES transaction must not re-prime the register underneath a Bank op.

Set/clear:

- Bank Load phase 82 (10271, after the preserve-read closes cleanly) and
  Bank Save phase 82 (equivalent site, ~13760): `fs_list_cache_hcnames_locked = 1u;`
- Bank Load phase 86 (11196) and Bank Save phase 86 (14192), immediately
  before `filesystem_clearResidentSourceDirtyFlags()`: clear it.
- Every error exit that calls `filesystem_finish(FS_STATUS_ERROR)` from
  inside those two state machines must clear it. The single safe way to
  guarantee that is to clear it in `filesystem_complete()` (3259) rather
  than at each `finish()` site — one line, cannot be bypassed.

#### Change 2 — Make the silent overlay no-op impossible to miss

*File:* `Core/Hardware/SD/filesystem.c`, `filesystem_cacheResidentName()` (4711)

Keep the guard, but stop swallowing it:

```c
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        fs_list_cache_kind != FS_NAME_CACHE_HCNAMES) {
        /*
         * A refused overlay is a data-loss event, not a no-op.
         *
         * Inputs: the requested row and the live cache tag. Output: one 'H'
         * record carrying the row and the observed cache kind; the caller's
         * name is still discarded, but the loss is now provable from the
         * card. Why: this early return is how five Bank operations across
         * two sessions published a completely stale register while every
         * caller reported success. flags carries the refusal reason so it
         * is distinguishable from the Session 056 scratch-drift use.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT,
                             AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_OVERLAY_REFUSED,
                             (uint32_t)row |
                             ((uint32_t)fs_list_cache_kind << 16u));
        return;
    }
```

`filesystem_setResidentSource()` already returns a status; the four Bank
call sites currently discard it with `(void)`. Change those four to check
it and fail the operation on `0`, since a refused source stage is the same
class of silent loss.

#### Change 3 — Prove the write before reporting `DONE`

*File:* `Core/Hardware/SD/filesystem.c`, Bank Load phase 86 (11190-11213)
and Bank Save phase 86 (14186-14210)

Insert two new phases between the close and `filesystem_finish(FS_STATUS_DONE)`:

- **phase 88 — REOPEN `/.hcnames` READ-ONLY.** Same
  `afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME, "r",
  FS_RESIDENT_NAMES_MATCH_MODE, NULL, on_file_opened)` used at phase 0.
- **phase 89 — VERIFY ROW 0, THEN CLOSE AND FINISH.** Read exactly the
  first line with the existing `filesystem_readTextLine()`, compare it
  against the line phase 85 formatted for row 0 (recompute it with
  `filesystem_formatResidentNameLine()` — no buffer retention needed), emit
  one `AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE` (`'K'`) record with a new
  `AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_HCNAMES_VERIFIED` bit, then close
  and `filesystem_finish()`.

Verification result policy: **witness, do not fail.** A mismatch must not
turn a successful Bank Load of the user's musical data into an error — the
payload is already committed and correct (§8 proved that). The `'K'` flag
plus the `'H'` refusal records are enough to identify the condition on the
next card copy, and the operation still completes.

Row 0 is sufficient as the probe: it is staged unconditionally by both
Bank writers on every operation, from durable state
(`op_bank_display_name` / `op_slot`), and it was wrong on all five
observed operations. Reading one line costs one open, one 512-byte
sector read, and one close.

#### Change 4 — Trace and documentation

*File:* `Core/Bank/Scene/AutosaveTrace.h`

- Add `AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_OVERLAY_REFUSED (1u << 0)` and
  `AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_CACHE_CLEAR_REFUSED (1u << 1)`, and
  extend the `'H'` doc comment: `flags == 0` keeps the existing Session 056
  scratch-drift meaning (value32 = child slot + first bytes); the new flags
  select the register-guard meaning (value32 = row in bits 0..15, cache
  kind in bits 16..31).
- Add `AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_HCNAMES_VERIFIED (1u << 2)` and
  document it in the `'K'` comment.

*Files:* `tools/decode_devlogs.py`, `tools/devlog_unpack.py` — decode both
new `'H'` flag meanings and the new `'K'` bit.

*File:* `knowledge_files/specification_reference/DEV_MODES.md` — extend the
`H` and `K` paragraphs; the stage-letter list is unchanged.

*File:* `tools/verify_bank_autosave.py` — its HCNAMES check currently
reports per-row mismatches, which is what made a wholly stale file look
like a scattered five-row defect. Add an up-front whole-file verdict:
if row 0's source token does not equal the active Bank slot from
`settings.cfg`, report **"HCNAMES was not rewritten by the last Bank
operation"** and say so before listing any row diffs.

---

### 9.8 Are Scene, Kit, and Instrument Save/Load exposed?

> **WRONG — do not use this table.** The row "Kit Save |
> filesystem.c:13715 handoff" is false: line 13715 is inside
> `filesystem_saveBankDirectory_tick()` (which begins at 13655), and Kit
> Save publishes nothing at all. The section's conclusion — that
> Scene/Kit/Instrument are "not exposed, by construction" — is also false.
> §10.2 is the correct publication map.


**Not to this defect, by construction — and the card proves it.** Every
other HCNAMES publisher performs its preserve-read and its rewrite inside
one adjacent, uninterrupted phase sequence, *after* all payload/tree work
has already finished:

| Operation | Handoff site | Shape |
|---|---|---|
| root Scene Load | filesystem.c:10119-10137 — `filesystem_prepareResidentNamesCache()` then `current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE; op_phase = 0` | payload complete → read → overlay → write |
| root Scene Save | filesystem.c:14722 — same handoff after the tree is written | tree complete → read → overlay → write |
| Kit Save | filesystem.c:13715 handoff | tree complete → read → overlay → write |
| Instrument Save | filesystem.c:11938 — `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` | file complete → read → overlay → write |
| Kit / Instrument menu entry | `FS_INTERNAL_OP_LOAD_HCNAMES_*` | read only |
| **Bank Load / Bank Save** | **read at phase 0, write at phase 83** | **read → entire traversal → write** |

Inside `filesystem_residentNames_tick()` the gap between the read closing
(phase 3) and the write opening (phase 3, same block, 5030-5042) contains
exactly one thing: the overlay call. Nothing can retag or clear the cache
in that window, because no other file or directory operation is issued.
That is why Session 055's two Scene-Load publications reached the card
while five consecutive Bank operations did not.

Two caveats worth recording rather than assuming away:

1. **Shared exposure, different odds.** All of these paths use the same
   borrowed `fs_list_cache_name`, the same silent
   `filesystem_cacheResidentName()` guard, and the same `"w"` LFN open.
   Change 2's `'H'` refusal record is deliberately placed in the shared
   helper, not in Bank-specific code, so if a Scene/Kit/Instrument publish
   ever *does* lose its overlay it will say so instead of silently
   shipping a stale register.
2. **Change 1's lock must not deadlock them.** The lock is claimed only by
   Bank Load/Bank Save and released in `filesystem_complete()`, and no
   Scene/Kit/Instrument HCNAMES transaction can be in flight while a Bank
   operation owns the facade (the facade is single-operation). The guard
   is therefore a diagnostic net for *intra-Bank* interference, not a
   cross-operation mutex, and must be documented that way.

### 9.9 What the next test will show

> **SUPERSEDED by §11.8.** The duplicate-directory-entry hypothesis at the
> end was subsequently eliminated: reads and writes resolve through the
> same scan in `afatfs_createFileInternal()` and would therefore agree, so
> it cannot produce a file frozen at an old image.


With Changes 1-3 in place, one ordinary Bank Load answers the remaining
question with no special reproduction steps:

- **`'H'` records with `OVERLAY_REFUSED` / `CACHE_CLEAR_REFUSED`** →
  confirmed: something in the Bank traversal was taking the borrow, and
  Change 1 now prevents it. Expect `/.hcnames` to update correctly from
  that same test.
- **No `'H'` records, and `'K'` reports `HCNAMES_VERIFIED` set** → the
  register is now correct and the defect is closed.
- **No `'H'` records, but `'K'` reports `HCNAMES_VERIFIED` clear** → the
  register was staged correctly and streamed correctly, and the bytes still
  did not reach the visible `/.hcnames` object. That moves the
  investigation to the storage layer — specifically
  `afatfs_createFileInternal()`'s LFN create path (asyncfatfs.c:3766-3940),
  where a `AFATFS_FILE_MODE_CREATE` open can stop at the first sufficient
  free directory-entry run (3836-3847) before it has scanned far enough to
  find the existing entry, which would produce a second `/.hcnames` object.
  `.hcnames` is the one root file whose leading dot forces
  `longNameEnabled = 1` on that path; `settings.cfg` and `asavetrc.bin`
  fit 8.3 and take the short-name branch, which only creates at the
  directory terminator. That asymmetry is worth keeping on file, but it is
  the *second* hypothesis, not the first — Change 3 is what decides.

None of this requires another forensic reconstruction from `.hcnames`
alone, which is the failure mode of the last two rounds.

---

## 10. Revised: this is a publication-coverage bug, not a Bank bug

**Supersedes §9.7 Change 1.** New card: `SD_CARD2/` — a Scene loaded into
resident Scene slot 3, a Kit into slot 4, several Instruments into slot 5,
then power off. `SD_CARD2/.hcnames` is again byte-identical to
`SD_CARD/.hcnames` and to the Session 054/055 commit.

First, the card copy is proven faithful, which retires the "the copy tool
skipped it" possibility for good:

```
.hcprms2       DIFFERS (34768 -> 34768 bytes)   <- same size, different content: copied by content
settings.cfg   IDENTICAL (268 bytes)
.hcnames       IDENTICAL (1344 bytes)
```

### 10.1 The Kit-family register writer has never run — not once, ever

`asavetrc.bin` is a cumulative append log spanning every session
(69,012 records; the previous card's 60,396 are a byte-identical prefix).
Decoding every `N` (INSTRUMENT_ENTRY) record across the **whole** file:

```
HCNAMES_REQUEST    ok      3      HCNAMES_COMPLETE   ok   3
HCNAMES_REQUEST    FAILED  999    (rejected while busy - normal retry noise)
HCNAMES_FLUSH              0
HCNAMES_FLUSHED            0
```

`AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSH` is emitted by
`menu_endResidentNameScratchSession()` ([menu.c:3531](Core/Menu/menu.c#L3531))
the moment it calls `filesystem_requestUpdateResidentKitNames()`. That call
is **the only caller in the entire codebase** of the Kit-family register
writer. Zero `HCNAMES_FLUSH` records means that function has never once
executed on this hardware.

That is not a subtle failure. It is a whole publication path that has
never fired, because it is armed only at a Menu UI boundary — the Save
type row leaving the Kit family, or leaving `SAVE_TYPE_SCENE`
([menu.c:8096-8110](Core/Menu/menu.c#L8096)) — and the ordinary user
sequence "load something, then switch off" never crosses it.

### 10.2 The publication map

Grepping every assignment of `current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_*`
in filesystem.c gives the complete set of filesystem-side publications:

| Operation | Publishes via | Rows it covers | Status |
|---|---|---|---|
| root Scene Load | handoff [filesystem.c:10137](Core/Hardware/SD/filesystem.c#L10137) | **Scene name rows only** | runs, reports DONE, card unchanged |
| root Scene Save | handoff [filesystem.c:14744](Core/Hardware/SD/filesystem.c#L14744) | **Scene name rows only** | untested |
| Instrument Save | handoff [filesystem.c:12096](Core/Hardware/SD/filesystem.c#L12096) | instrument rows, **non-morph only** | untested |
| **Kit Load** | *nothing* | — | never publishes |
| **Kit Save** | *nothing* | — | never publishes |
| **Instrument Load** | *nothing* | — | never publishes |
| Kit + 6 Instrument rows for *all* of the above | Menu deferred flush | Kit row + 6 instrument rows per dirty Scene | **never executed (10.1)** |
| Bank Load / Bank Save | own writer, phases 83-86 | all 129 rows | runs, reports DONE, card unchanged |

`current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_KIT` **appears nowhere** in
filesystem.c. Two public entry points are dead code with zero callers
anywhere in `Core/`:

- `filesystem_requestUpdateResidentInstrumentNames()` ([filesystem.c:21833](Core/Hardware/SD/filesystem.c#L21833))
- `filesystem_requestUpdateResidentSceneNames()` ([filesystem.c:21979](Core/Hardware/SD/filesystem.c#L21979))

So the user-visible symptom decomposes exactly:

- **Kit Load into slot 4, Instruments into slot 5** — no publication path
  exists at all. Nothing was ever going to be written.
- **Scene Load into slot 3** — the Scene *name* row has a path and it ran
  (two `R` DONE records, `t=18832` and `t=53538`, mask `0x04`); the Scene's
  Kit and Instrument rows depend on the never-firing Menu flush.
- **Bank Load at boot** — three `K` DONE records for slot 15 this session,
  own writer, still nothing on the card.

### 10.3 Two independent defects, not one

**Defect A — coverage.** Kit Load, Kit Save and Instrument Load have no
HCNAMES publication; Scene Load/Save publish only one of the three row
classes they change; the missing remainder was delegated to a Menu
boundary that never fires. This fully explains what you observed for the
Kit and Instrument loads, and it is fixable purely in the handoff layer.

**Defect B — the write does not land.** Scene Load's transaction and the
Bank writers both complete `FS_STATUS_DONE` with zero `E` (error) and zero
`X` (stall) records in 69,012 records, and the file still does not change
by one byte. Row 3 remains `RedSnap<TAB>-`; a root Scene Load stages a
*numeric* source (`filesystem_setResidentSource(sceneRow, op_slot)`,
[filesystem.c:9315](Core/Hardware/SD/filesystem.c#L9315)) which the dirty
flag protects from the preserve-read, so that row had to change and did
not. Row 0 remains `LoadTst<TAB>012` after three Bank Loads of slot 15.

Defect B is **not** the read-hold-write gap §9.5 proposed: Scene Load's
transaction reads and writes adjacently with no traversal in between and
fails identically. §9.7 Change 1 (the borrow lock) is therefore withdrawn.
It is also not a duplicate directory entry — reads and writes resolve
through the same scan in `afatfs_createFileInternal()` and would agree.

---

### 10.4 The fix

> **SUPERSEDED by §11.2–§11.6**, which lists every code site with full
> comment blocks. The diagnosis in §10.1–§10.3 stands; this plan sketch
> does not, and its Kit Save site reference is wrong (see §11.0).


**Principle: publication belongs to the filesystem operation that changed
the data, and must complete before that operation reports completion.**
This is the same rule Session 056 Part B already established for
`settings.cfg` — never defer identity persistence to a debounce or to a UI
event that may never happen.

#### Fix 1 — Give Kit Load, Kit Save and Instrument Load the handoff they lack

*File:* `Core/Hardware/SD/filesystem.c`

At each completion site, mirror the proven Scene Load pattern at
[10119-10138](Core/Hardware/SD/filesystem.c#L10119) exactly: stage sources,
set the destination mask, `filesystem_prepareResidentNamesCache()`,
`filesystem_bootLoggingArm("HCNAMES ")`, assign `current_op`, `op_phase = 0u`,
`return` — never `filesystem_finish()` first.

- **`filesystem_loadKitDirectory_tick()` completion** —
  `filesystem_setResidentSource(filesystem_residentKitRow(scene), op_slot)`
  for each destination Scene in `op_kit_load_scene_mask`, and
  `FS_RESIDENT_SOURCE_INHERIT` for that Scene's six Instrument rows, then
  hand off to `FS_INTERNAL_OP_UPDATE_HCNAMES_KIT`. This is the first
  producer of that op value in the file.
- **`filesystem_saveKitDirectory_tick()` completion** — identical, with
  `op_slot` being the saved root Kit slot, so the register records where
  the Kit now lives.
- **Instrument Load completion** — mirror
  [12088-12096](Core/Hardware/SD/filesystem.c#L12088): set
  `op_kit_load_scene_mask` / `op_slot` to the destination voice, stage the
  row source, hand off to `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT`.
- **Instrument Save, morph branch** — currently skips publication entirely;
  give it the same handoff as the non-morph branch.

Each handoff **must** be guarded exactly the way Scene Save already guards
its own ([filesystem.c:14667](Core/Hardware/SD/filesystem.c#L14667)):

```c
if (op_bank_payload_active) { /* Bank owns the single final register write */ }
else { /* ... handoff ... */ }
```

so Bank-delegated children keep deferring to Bank's one writer and a Bank
Load does not perform 16 register rewrites.

#### Fix 2 — Publish the whole affected block, not one row class

*File:* `Core/Hardware/SD/filesystem.c`, `filesystem_residentNames_tick()`
phase 3 ([5030-5035](Core/Hardware/SD/filesystem.c#L5030)) and the
first-use bootstrap at [5138-5143](Core/Hardware/SD/filesystem.c#L5138)

`FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` currently calls only
`filesystem_cacheCurrentResidentSceneNames()`, which writes Scene rows and
nothing else. But a Scene Load commits an entire embedded Kit, and its Kit
name plus six Instrument names are already in `fs_identity_name` at that
moment — staged at [filesystem.c:9296-9302](Core/Hardware/SD/filesystem.c#L9296).
Change the Scene branch to overlay the Kit family as well:

```c
else if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE) {
    /*
     * A Scene action replaces three row classes, not one.
     *
     * What: Scene name rows plus the same Scenes' Kit row and six
     * Instrument rows. Why: a Scene Load/Save commits a complete embedded
     * Kit image, and its identity is already in the nine-row identity
     * block; publishing only the Scene name left the Kit family to a Menu
     * UI-boundary flush that never fires (S056 section 10.1). Inputs:
     * op_scene_load_scene_mask, op_scene_display_name, fs_identity_name.
     * Output: every row this action changed is written in one transaction.
     * Affiliates: filesystem_cacheCurrentResidentKitNames() and the
     * retired menu_endResidentNameScratchSession() flush.
     */
    filesystem_cacheCurrentResidentSceneNames();
    op_kit_load_scene_mask = op_scene_load_scene_mask;
    filesystem_cacheCurrentResidentKitNames();
}
```

`filesystem_cacheCurrentResidentKitNames()` reads `op_kit_load_scene_mask`,
so that assignment is the whole bridge; both helpers already exist and
already preserve every unrelated row.

#### Fix 3 — Retire the Menu-deferred Kit-family session

*File:* `Core/Menu/menu.c`

With Fixes 1 and 2 in place there is exactly one publication owner per
operation, and the deferred path becomes a second, unreliable owner of the
same rows. Remove the write half:

- `menu_endResidentNameScratchSession()`'s
  `filesystem_requestUpdateResidentKitNames()` branch,
  `menu_residentNameDirtySceneMask`, and
  `menu_refreshResidentNameScratchKit()` ([menu.c:8103](Core/Menu/menu.c#L8103)).
- Keep the **read** half untouched — `menu_residentNameScratchLoaded()`,
  `HCNAMES_REQUEST` / `HCNAMES_COMPLETE`, and `filesystem_clearNameCache()`
  are the Kit/Instrument browse cache and are working correctly (3 requests,
  3 completions).

Then delete `filesystem_requestUpdateResidentKitNames()`,
`filesystem_requestUpdateResidentInstrumentNames()` and
`filesystem_requestUpdateResidentSceneNames()` along with their
declarations in filesystem.h — all three become unreferenced.

#### Fix 4 — Prove the write before reporting DONE (retained from §9.7)

Unchanged from §9.7 Change 3, and now the *load-bearing* part of the plan:
because of Defect B, Fixes 1-3 can all land correctly and still produce a
completely unchanged card. After the register file closes, reopen it
read-only, read back one row the transaction just staged, compare against
the line `filesystem_formatResidentNameLine()` produced for it, and emit
the result as a flag on the existing completion witness (`K` for Bank, and
a new equivalent for the generic writer). **Witness, do not fail** — a
mismatch must not turn a good payload load into an error.

Probe row: row 0 for Bank; the lowest set bit of the destination mask for
Scene/Kit/Instrument.

#### Fix 5 — Make the silent overlay refusal loud (retained from §9.7)

Unchanged from §9.7 Change 2: `filesystem_cacheResidentName()`
([4711](Core/Hardware/SD/filesystem.c#L4711)) returns silently when
`fs_list_cache_kind != FS_NAME_CACHE_HCNAMES`. Emit one `H` record with an
`OVERLAY_REFUSED` flag carrying the row and the observed cache kind. Also
stop discarding `filesystem_setResidentSource()`'s return value at the
Bank and Scene staging sites.

#### Fix 6 — Trace, tools, docs

- `AutosaveTrace.h`: `AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_OVERLAY_REFUSED`;
  a `HCNAMES_VERIFIED` bit on `K`; a completion witness for the generic
  writer so Scene/Kit/Instrument publication is as visible as Bank's.
- `tools/decode_devlogs.py`, `tools/devlog_unpack.py`: decode all of the above.
- `tools/verify_bank_autosave.py`: add the up-front verdict from §9.7 —
  if row 0's source token does not equal `settings.cfg`'s `active_bank`,
  report "HCNAMES was not rewritten by the last Bank operation" before
  listing any per-row diffs, so a wholly stale file never again reads as a
  scattered subset.
- `DEV_MODES.md`: the new flags, and the publication map from §10.2.

### 10.5 Save operations are covered

> **Intent still valid; the "Fix N" numbers refer to the superseded §10.4
> sketch.** The implementable version is §11.2 Group A, and the rule at the
> end of this section is restated there.


Explicitly, per your requirement that Saves must also update names *and*
sources to match the save slot:

| Save | Today | After the fix |
|---|---|---|
| Kit Save | no publication at all | Fix 1: Kit row source = saved root Kit slot; six Instrument rows INHERIT |
| Instrument Save | non-morph publishes; morph skips | Fix 1: morph branch gets the same handoff |
| Scene Save | Scene rows only (sources already staged correctly at [14690](Core/Hardware/SD/filesystem.c#L14690)) | Fix 2: same transaction also publishes the Scene's Kit + 6 Instrument rows |
| Bank Save | own writer stages row 0 name + source (`SOURCE_STAGED` witness confirms) | unchanged; Fix 4 proves whether it lands |

The rule to hold every one of them to: **a Save publishes the saved
object's row with a direct numeric source equal to the destination slot,
and every row beneath it as INHERIT, in the same operation that wrote the
files.** A Load publishes identically, with the source being the slot it
loaded *from*.

### 10.6 What is still open

Defect B. Fixes 1-3 remove the coverage holes and are worth landing on
their own merits — they are the difference between "no code exists to
write this row" and "code exists". But nothing in §10 explains why an
HCNAMES transaction that reads, overlays, opens `"w"`, streams 129 rows,
closes and reports `DONE` leaves the card byte-identical, while
`.hcprms1`, `.hcprms2`, `asavetrc.bin` and `settings.cfg` in the same root
directory all update. Fix 4 is what will answer it, on the next ordinary
Load, without another forensic round.

---

## 11. Full remediation plan — every code site

> **THIS IS THE PLAN TO IMPLEMENT.** Everything before this section is the
> investigation record, including three diagnoses that turned out to be
> wrong and two superseded plan sketches. §11.0 states exactly what those
> corrections are. If you are here to write code, start at §11.0 and treat
> §§1–10 as background only.


### 11.0 Assessment re-check: three corrections to §9

Before expanding, three things in §9 do not survive re-checking and are
corrected here rather than left to mislead the implementation.

1. **§9.8's table row "Kit Save | filesystem.c:13715 handoff" is wrong.**
   Line 13715 is inside `filesystem_saveBankDirectory_tick()`, which begins
   at [filesystem.c:13655](Core/Hardware/SD/filesystem.c#L13655) — it is
   *Bank* Save's preserve-read, not Kit Save's. `filesystem_saveKitDirectory_tick()`
   ends at `case 21` with a bare `filesystem_finish(FS_STATUS_DONE)`
   ([13642](Core/Hardware/SD/filesystem.c#L13642)) and publishes nothing.
   §10.2 is the correct map.

2. **§9.5 overstated the evidence that the generic writer works.** It
   attributed the two Session 054/055 register changes to a root Scene
   *Load*, from the `004` source token. Both `/Scene/004 Moch to` and
   `/Kit/004 Moch to` exist on the card, and a Scene **Save to** slot 004
   produces exactly the same token as a Scene **Load from** slot 004
   ([filesystem.c:9315](Core/Hardware/SD/filesystem.c#L9315) vs
   [14690](Core/Hardware/SD/filesystem.c#L14690)). The only defensible
   statement is: *some* Scene-level operation in Session 054/055 wrote the
   register, and nothing since has. Do not treat "the generic writer works"
   as established — §10.3's Defect B says a Scene Load's generic
   transaction ran twice this session and changed nothing.

3. **§9.7 Change 1 (the borrow lock) is withdrawn**, as §10.3 states. It is
   not carried into this plan.

And one new finding that makes the fix substantially smaller than §10.4
implied:

**Source staging is already correct at every single site.** Every Load and
Save already calls `filesystem_setResidentSource()` with the right value
before it completes:

| Operation | Staging site | Stages |
|---|---|---|
| Kit Load | [8676-8690](Core/Hardware/SD/filesystem.c#L8676) | Kit row = `op_slot`, 6 Instrument rows = INHERIT, per masked Scene |
| Kit Save | [13608-13620](Core/Hardware/SD/filesystem.c#L13608) | same, `op_slot` = saved root Kit slot |
| Instrument Load | [11627-11631](Core/Hardware/SD/filesystem.c#L11627) | destination Instrument row = `INSTRUMENT_DIRECT` |
| Instrument Save | [12043-12047](Core/Hardware/SD/filesystem.c#L12043) | same |
| Scene Load | [9315-9330](Core/Hardware/SD/filesystem.c#L9315) | Scene row = `op_slot`, Kit + 6 Instrument rows = INHERIT |
| Scene Save | [14690-14712](Core/Hardware/SD/filesystem.c#L14690) | same |
| Bank Load | [10797](Core/Hardware/SD/filesystem.c#L10797) + [4903-4916](Core/Hardware/SD/filesystem.c#L4903) | row 0 = `op_slot`; per-child block = INHERIT |
| Bank Save | [14122](Core/Hardware/SD/filesystem.c#L14122) | row 0 = `op_slot` |

Three of those sites even carry comments explaining that they stage "before
Menu's deferred HCNAMES flush rereads the old register" — the staging was
written against a consumer that has never run (§10.1). **Nothing in this
plan needs to add or change source staging. The entire defect is
publication.**

### 11.1 The three invariants this plan enforces

- **I1 — Ownership.** Exactly one component publishes a given HCNAMES row,
  and it is the filesystem operation that changed the underlying data.
  No UI event, no debounce, no second owner.
- **I2 — Completeness.** An operation publishes *every* row class it
  changed, in one transaction, before it reports completion.
- **I3 — Provability.** No operation may report `FS_STATUS_DONE` on a
  register write without a witness recording whether the bytes reached the
  card.

---

### 11.2 Group A — publication coverage (`Core/Hardware/SD/filesystem.c`)

#### A1 — New shared handoff helper

*New static function, placed immediately before `filesystem_residentNames_tick()`
([4920](Core/Hardware/SD/filesystem.c#L4920)); forward declaration beside the
existing one at [1176](Core/Hardware/SD/filesystem.c#L1176).*

Six completion sites need the identical five-statement handoff. Writing it
once prevents the class of drift that produced the current map, where three
sites got it, three did not, and one got it for only half its branches.

```c
static void filesystem_beginResidentNamePublish(fs_internal_op_t publish_op)
{
    /*
     * Re-target the running operation at the shared HCNAMES register writer.
     *
     * What: borrows the 129-row register into the shared name cache and
     * replaces current_op with one of the three UPDATE_HCNAMES_* values,
     * leaving op_phase at 0 so filesystem_residentNames_tick() runs its
     * read -> overlay -> write -> verify sequence next tick. The caller's
     * completion_callback is deliberately NOT touched: it stays parked until
     * the register write finishes, so Preset/Menu observe one completion for
     * the whole action rather than one for the payload and one for the name.
     *
     * Why: identity publication must belong to the operation that changed
     * the data (invariant I1). Before this helper, Kit Load, Kit Save,
     * Instrument Load and the Instrument-Save morph branch simply called
     * filesystem_finish() and published nothing, delegating their rows to
     * menu_endResidentNameScratchSession() -- a UI-boundary flush that
     * S056_HCNAMES_FOLLOW_UP.md section 10.1 proved has never executed once
     * in 69,012 recorded trace events. The three sites that did hand off
     * each open-coded the same five statements slightly differently.
     *
     * Inputs: publish_op selects which overlay the writer applies
     * (FS_INTERNAL_OP_UPDATE_HCNAMES_KIT / _SCENE / _INSTRUMENT). The caller
     * must already have staged its rows' provenance through
     * filesystem_setResidentSource() and set the destination mask
     * (op_kit_load_scene_mask or op_scene_load_scene_mask) that the matching
     * filesystem_cacheCurrentResident*Names() helper reads.
     *
     * Outputs: fs_list_cache_* is retagged as an HCNAMES image with every
     * non-dirty source reset to UNKNOWN; current_op and op_phase are
     * replaced; boot logging is re-armed so a logging build attributes a
     * stall to the register write rather than the finished payload step.
     * No file is opened here and no SRAM is allocated.
     *
     * This deliberately bypasses filesystem_start(): that function resets
     * every op_* scratch field, which would destroy the destination mask and
     * op_scene_display_name the overlay is about to read. The existing Scene
     * Load handoff has always relied on the same bypass.
     *
     * Affiliates: filesystem_prepareResidentNamesCache(),
     * filesystem_residentNames_tick(), filesystem_cacheCurrentResidentKitNames(),
     * filesystem_cacheCurrentResidentSceneNames(),
     * filesystem_cacheCurrentResidentInstrumentNames(), and the retired
     * menu_endResidentNameScratchSession() flush.
     */
    filesystem_prepareResidentNamesCache();
    /*
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging.
     * It must never print anything to the screen or otherwise delay
     * operations unnecessarily since logging may be used to assess timing
     * failures in other modules that might otherwise be obscured by screen
     * write delays.
     */
    filesystem_bootLoggingArm("HCNAMES ");
    current_op = publish_op;
    op_phase = 0u;
}
```

Once this exists, replace the open-coded handoffs at
[10119-10138](Core/Hardware/SD/filesystem.c#L10119) (Scene Load),
[12088-12096](Core/Hardware/SD/filesystem.c#L12088) (Instrument Save) and
[14722-14744](Core/Hardware/SD/filesystem.c#L14722) (Scene Save) with calls
to it, so all six sites are provably identical. This is a
behaviour-preserving substitution at those three sites.

#### A2 — Kit Load must publish

*Site:* `filesystem_loadKitDirectory_tick()` `case 28: /* RETURN TO ROOT + FINISH */`,
[filesystem.c:8824-8828](Core/Hardware/SD/filesystem.c#L8824)

```c
    case 28: /* RETURN TO ROOT + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Publish the loaded Kit's identity before completing the Load.
         *
         * What: a successful normal (non-morph) root Kit Load hands off to
         * the shared register writer, which republishes the Kit row and the
         * six Instrument rows of every destination Scene in
         * op_kit_load_scene_mask.
         *
         * Why: this operation is the only thing that knows a resident Kit's
         * identity changed. Its provenance was already staged at case 13
         * (filesystem.c:8676) with a comment saying it precedes "Menu's
         * deferred HCNAMES flush" -- but that flush has never run, so before
         * this change a Kit Load wrote zero bytes to /.hcnames and the
         * register kept describing the previous Kit indefinitely. Confirmed
         * on hardware: SD_CARD2 shows a Kit loaded into resident Scene slot 4
         * with row 21 and rows 51-56 unchanged.
         *
         * Inputs: op_close_status from the payload phases, current_op
         * (LOAD_KIT vs LOAD_KIT_MORPH), op_kit_load_scene_mask, the identity
         * block written at case 13, and the source words staged there.
         * Outputs: on the success path current_op becomes
         * FS_INTERNAL_OP_UPDATE_HCNAMES_KIT and the original callback stays
         * parked until the register is durable; every other path completes
         * exactly as before.
         *
         * Morph loads are excluded deliberately: a Morph endpoint is a
         * second parameter image for the same resident voices, not a change
         * of identity, and publishing it would overwrite the real Kit name
         * with the morph source. This matches the existing exclusion in
         * Instrument Save (filesystem.c:11938), which is the only precedent
         * in the file. See SCOPING_TARGETS.md if that policy is revisited.
         *
         * Affiliates: filesystem_beginResidentNamePublish(),
         * filesystem_cacheCurrentResidentKitNames(), Kit Save's symmetric
         * publish (case 21), and preset_getKitRequestSceneMask().
         */
        if (op_close_status == FS_STATUS_DONE &&
            current_op == FS_INTERNAL_OP_LOAD_KIT) {
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_KIT);
            return;
        }
        filesystem_finish(op_close_status);
        return;
```

This is the **first producer of `FS_INTERNAL_OP_UPDATE_HCNAMES_KIT`
anywhere in filesystem.c** — before it, that enum value was reachable only
through the dead public request (D5).

#### A3 — Kit Save must publish, before its index rebuild

*Site:* `filesystem_saveKitDirectory_tick()` `case 21`,
[filesystem.c:13626-13643](Core/Hardware/SD/filesystem.c#L13626)

The existing block already stages sources and sets
`op_library_index_rebuild_kind/_pending`, then calls
`filesystem_finish(FS_STATUS_DONE)` which parks the callback and runs the
`/Kit/.hcindex` rebuild. The publish must be inserted **between** those
two, never after:

```c
        op_library_index_rebuild_kind = FS_NAME_CACHE_KIT;
        op_library_index_rebuild_pending = 1u;
        autosaveTrace_record(... CHECKPOINT_FINISH ... TYPE_KIT ...);
        /*
         * Publish the saved Kit's identity before the index rebuild chain.
         *
         * What: hands off to the shared register writer for the same seven
         * rows whose provenance was staged twenty lines above. The pending
         * index-rebuild request is left armed; it runs after the register
         * transaction completes, exactly as it already does for Scene Save.
         *
         * Why (ordering is load-bearing): the library index rebuild calls
         * filesystem_prepareLibraryNameCache(FS_NAME_CACHE_KIT), which
         * memsets all 9,000 bytes of fs_list_cache_name and retags the
         * domain. Publishing after the rebuild would stream a destroyed
         * register; publishing before it is the only correct order. Scene
         * Save already demonstrates this ordering (filesystem.c:14686 arms
         * the rebuild, 14744 hands off to HCNAMES first).
         *
         * Why at all: Kit Save previously published nothing. A Save must
         * record both the new name and a direct numeric source equal to the
         * destination slot, so a later boot can resolve where the resident
         * Kit came from; without this the register still names the Kit the
         * user overwrote.
         *
         * Inputs: op_kit_save_mode (normal only -- the morph branch above
         * stages no sources and must not publish), op_kit_save_source_scene,
         * op_slot. Outputs: current_op becomes
         * FS_INTERNAL_OP_UPDATE_HCNAMES_KIT with the original callback still
         * parked through both the register write and the index rebuild.
         *
         * op_kit_load_scene_mask is the mask the Kit overlay reads, so the
         * single saved source Scene must be converted to a one-bit mask here;
         * Kit Save otherwise tracks its Scene in op_kit_save_source_scene.
         *
         * Affiliates: filesystem_beginResidentNamePublish(),
         * filesystem_cacheCurrentResidentKitNames(),
         * filesystem_completeLibraryIndexRebuild(), and A2's symmetric
         * Kit Load publish.
         */
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL) {
            op_kit_load_scene_mask =
                (uint16_t)(1u << op_kit_save_source_scene);
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_KIT);
            return;
        }
        filesystem_finish(FS_STATUS_DONE);
        return;
```

**Verify while implementing:** that `filesystem_finish()` /
`filesystem_completeLibraryIndexRebuild()`
([3395](Core/Hardware/SD/filesystem.c#L3395)) consumes
`op_library_index_rebuild_pending` at the *register* transaction's
completion and not at the Kit Save's, i.e. that the flag survives the
`current_op` swap. Scene Save's existing pairing is the reference.

#### A4 — Instrument Load must publish

*Site:* `filesystem_loadInstrument_tick()` `case 16: /* RETURN ROOT + FINISH */`,
[filesystem.c:11651-11655](Core/Hardware/SD/filesystem.c#L11651)

```c
    case 16: /* RETURN ROOT + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Publish the loaded Instrument's identity before completing.
         *
         * What: a successful normal pool Instrument Load hands off to the
         * shared register writer, republishing the one destination
         * Instrument row whose '@' direct source was staged at case 13
         * (filesystem.c:11627).
         *
         * Why: before this change nothing published it. The staging comment
         * at case 13 says the dirty flag "protects it from the older card
         * record until publication succeeds" -- but publication was never
         * requested, so the staged word was silently discarded by the next
         * filesystem_prepareResidentNamesCache() and the register kept
         * naming the previous Instrument. Confirmed on hardware: SD_CARD2
         * shows several Instruments loaded into resident Scene slot 5 with
         * rows 63-68 unchanged.
         *
         * Hidden temporary loads are excluded for the same reason case 13
         * excludes them from the identity block: `.hctmp.<ext>` is an
         * implementation-owned reversible-load restore source, never a
         * user-selectable Instrument, and it must not become resident
         * identity. op_instrument_load_temporary is tri-state (0 = normal,
         * 1 = temp, FS_INSTRUMENT_LOAD_TEMP_MORPH = temp morph); any nonzero
         * value suppresses publication.
         *
         * Inputs: op_close_status, op_instrument_load_temporary,
         * op_instrument_load_destination_scene / _slot. Outputs: on success
         * current_op becomes FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT with
         * the original callback parked; all other paths unchanged.
         *
         * op_kit_load_scene_mask and op_slot are the two fields
         * filesystem_cacheCurrentResidentInstrumentNames() reads
         * (filesystem.c:4744), so both must be pointed at the destination
         * voice here -- Instrument Load otherwise tracks it in its own
         * op_instrument_load_destination_* pair.
         *
         * Affiliates: filesystem_beginResidentNamePublish(),
         * filesystem_cacheCurrentResidentInstrumentNames(),
         * filesystem_requestLoadInstrumentTemp(), and Instrument Save's
         * equivalent publish at filesystem.c:12096.
         */
        if (op_close_status == FS_STATUS_DONE &&
            !op_instrument_load_temporary) {
            op_kit_load_scene_mask =
                (uint16_t)(1u << op_instrument_load_destination_scene);
            op_slot = op_instrument_load_destination_slot;
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT);
            return;
        }
        filesystem_finish(op_close_status);
        return;
```

**Note the `op_slot` overwrite.** `filesystem_cacheCurrentResidentInstrumentNames()`
reads the destination voice from `op_slot`, not from
`op_instrument_load_destination_slot`. Instrument Save already does exactly
this reassignment at [12049](Core/Hardware/SD/filesystem.c#L12049). Confirm
no later consumer of `op_slot` exists on this path before copying the
pattern — the Load is finished, so there should be none, but it must be
checked rather than assumed.

#### A5 — Instrument Save: morph branch (decision required)

*Site:* [filesystem.c:11938-11946](Core/Hardware/SD/filesystem.c#L11938)

```c
        if (!morph_save) {
            filesystem_bootLoggingArm("HCNAMES ");
            current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT;
        } else {
            filesystem_bootLoggingArm("INSINDEX");
            current_op = FS_INTERNAL_OP_CREATE_BOOT_INDEX;
        }
```

**Recommendation: leave the morph branch excluded, and document why.** A
Morph endpoint save writes a second parameter image for the same resident
voice; the voice's identity and its source slot do not change, so
publishing would overwrite a correct row with the morph file's name. The
`else` branch should gain a comment stating this is a deliberate exclusion
rather than an oversight, so the next reader does not "fix" it. A2 applies
the same policy to Kit Morph Load.

This is the one place in the plan where the right answer is a policy call
rather than a defect — flagged for your decision rather than silently
resolved.

#### A6 — Scene publication must cover all three row classes

*Sites:* `filesystem_residentNames_tick()` phase 3
([5030-5035](Core/Hardware/SD/filesystem.c#L5030)) and the first-use
bootstrap at [5138-5143](Core/Hardware/SD/filesystem.c#L5138) — **both**,
they are two copies of the same dispatch.

```c
        if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_KIT)
            filesystem_cacheCurrentResidentKitNames();
        else if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE) {
            /*
             * A Scene action replaces three row classes, not one.
             *
             * What: publishes the Scene name rows, and then the same
             * destination Scenes' Kit row and six Instrument rows, in this
             * one transaction.
             *
             * Why: a Scene Load or Save commits a complete embedded Kit
             * image into each destination Scene, and filesystem.c:9296-9302
             * has already written that Kit's name and its six member names
             * into the nine-row identity block. Publishing only the Scene
             * name left the Kit family to the Menu UI-boundary flush that
             * has never executed (section 10.1), so a Scene Load updated one
             * row out of eight per destination and left seven describing the
             * Kit that used to be there. Their provenance was already staged
             * as INHERIT at filesystem.c:9319-9330, so the rows and their
             * source words disagreed as well.
             *
             * Inputs: op_scene_load_scene_mask (the immutable destination
             * mask captured at request acceptance), op_scene_display_name,
             * and fs_identity_name's Kit + six Instrument rows.
             * Outputs: up to 8 x N rows overlaid in the borrowed register;
             * every unrelated row still comes from the file that was just
             * read. No file I/O and no new SRAM -- both helpers already
             * exist and both already skip unmasked Scenes.
             *
             * The mask copy is the whole bridge:
             * filesystem_cacheCurrentResidentKitNames() reads
             * op_kit_load_scene_mask, while a Scene action carries its
             * destinations in op_scene_load_scene_mask. Overwriting
             * op_kit_load_scene_mask is safe here because this handoff
             * bypassed filesystem_start(), no Kit operation is in flight,
             * and nothing later in this transaction reads it for any other
             * purpose.
             *
             * Affiliates: filesystem_cacheCurrentResidentSceneNames(),
             * filesystem_cacheCurrentResidentKitNames(), Scene Load's
             * handoff (filesystem.c:10137), Scene Save's handoff
             * (filesystem.c:14744), and the retired
             * menu_refreshResidentNameScratchKit() accumulation.
             */
            filesystem_cacheCurrentResidentSceneNames();
            op_kit_load_scene_mask = op_scene_load_scene_mask;
            filesystem_cacheCurrentResidentKitNames();
        }
        else
            filesystem_cacheCurrentResidentInstrumentNames();
```

---

### 11.3 Group B — the proof gate (invariant I3)

Group A is worthless on its own if Defect B is real: every new handoff
would run, report `DONE`, and change nothing, exactly as the existing three
already do. Group B is what makes the next test conclusive instead of
another forensic round.

#### B1 — New verification helper

*New static function beside `filesystem_cacheResidentRecord()`
([4652](Core/Hardware/SD/filesystem.c#L4652)).*

```c
static uint8_t filesystem_residentRowMatchesCache(uint16_t row,
                                                  const char *line)
{
    /*
     * Compare one row read back from the card against what was just staged.
     *
     * What: parses a physical `name<TAB>source` record with the same rules
     * filesystem_cacheResidentRecord() uses, and reports whether both halves
     * equal the live cache cell and provenance word for that row. Returns 1
     * on match.
     *
     * Why: five Bank operations and two Scene Loads have completed
     * FS_STATUS_DONE against an unchanged /.hcnames, with zero 'E' error
     * records and zero 'X' stall records in 69,012 trace events. There is
     * currently no point in the system where "the register write succeeded"
     * is distinguishable from "the register write did nothing", which is why
     * the defect survived two full test sessions and three wrong theories.
     * This helper is the missing distinction.
     *
     * Inputs: the logical row and one NUL-terminated line read back from the
     * reopened register. Reads fs_list_cache_name[row] and
     * fs_resident_source[row]; the source comparison masks off
     * FS_RESIDENT_SOURCE_DIRTY_FLAG so it works whether or not
     * filesystem_clearResidentSourceDirtyFlags() has run yet.
     * Outputs: a boolean only. No state change, no I/O, no allocation.
     *
     * Deliberately re-parses the read-back line rather than re-formatting an
     * expected line: comparing against the cache directly avoids a second
     * FS_TEXT_LINE_MAX buffer, keeping this to about sixteen bytes of
     * transient stack inside the caller's existing frame.
     *
     * Affiliates: filesystem_cacheResidentRecord(),
     * filesystem_parseResidentSourceToken(), filesystem_formatResidentNameLine(),
     * and the verify phases in filesystem_residentNames_tick(),
     * filesystem_loadBankDirectory_tick() and filesystem_saveBankDirectory_tick().
     */
    /* ... parse into char name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u] and a
     *     uint16_t source, then compare both against the cache ... */
}
```

#### B2 — Verify phases in the shared writer

*Site:* `filesystem_residentNames_tick()` `case 6`,
[filesystem.c:5088-5105](Core/Hardware/SD/filesystem.c#L5088)

Split `case 6` so that after `afatfs_chdir(NULL)` and **before**
`filesystem_clearResidentSourceDirtyFlags()` and `filesystem_finish()`, two
new phases run:

- **`case 9` — REOPEN REGISTER READ-ONLY.** Same
  `afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME, "r", FS_RESIDENT_NAMES_MATCH_MODE, NULL, on_file_opened)`
  the transaction already used at phase 0. A `NULL` handle here is itself
  the answer and must be witnessed, not treated as an error.
- **`case 10` — READ PROBE ROW, WITNESS, CLOSE, FINISH.** Skip lines with
  `filesystem_readTextLine()` until `op_item_offset` reaches the probe row,
  call B1, emit the new `'U'` record (E1), close, then run the original
  `filesystem_clearResidentSourceDirtyFlags()` + `filesystem_finish(FS_STATUS_DONE)`.

Probe row selection, computed from state already present:

| `current_op` | probe row |
|---|---|
| `UPDATE_HCNAMES_SCENE` | `filesystem_residentSceneRow(lowest set bit of op_scene_load_scene_mask)` |
| `UPDATE_HCNAMES_KIT` | `filesystem_residentKitRow(lowest set bit of op_kit_load_scene_mask)` |
| `UPDATE_HCNAMES_INSTRUMENT` | `filesystem_residentInstrumentRow(lowest set bit of op_kit_load_scene_mask, op_slot)` |

**Policy: witness, do not fail.** A mismatch must not turn a good payload
Load into an error — the musical data is already committed and correct
(§8). The witness plus the `'H'` records are enough to identify the
condition from the next card copy.

#### B3 / B4 — Verify phases in the two Bank writers

*Sites:* Bank Load `case 86`
([filesystem.c:11190-11213](Core/Hardware/SD/filesystem.c#L11190)) and Bank
Save `case 86` ([filesystem.c:14186-14210](Core/Hardware/SD/filesystem.c#L14186)).

Identical structure to B2 using new phases `88`/`89` (83-87 are taken in
both state machines), probe row `0`, and the existing `'K'` record extended
with a `HCNAMES_VERIFIED` flag bit rather than a second record. Bank Save's
`case 86` must keep setting `op_library_index_rebuild_kind/_pending` before
the new phases, for the same ordering reason as A3.

The comment block for both should state: *inputs* — the just-closed
register and `fs_list_cache_name[0]` / `fs_resident_source[0]`; *outputs* —
one flag bit on the existing completion witness, no behaviour change;
*why* — §9.4 proved row 0 must have received the staged value and did not,
and this is the one probe that separates "staged wrong" from "written and
lost"; *affiliates* — `filesystem_residentRowMatchesCache()`, the `'K'`
stage in AutosaveTrace.h, and `on_bank_load_complete()` /
`on_bank_save_complete()` in presetManager.c.

---

### 11.4 Group C — make silent losses loud

#### C1 — `filesystem_cacheResidentName()` refusal

*Site:* [filesystem.c:4711-4726](Core/Hardware/SD/filesystem.c#L4711).
Exactly as drafted in §9.7 Change 2: emit one `'H'` record carrying the row
and the observed `fs_list_cache_kind` before the early return, so a lost
overlay is provable instead of invisible. Unchanged from that draft.

#### C2 — `filesystem_setResidentSource()` rejection

*Site:* [filesystem.c:4545-4557](Core/Hardware/SD/filesystem.c#L4545).

§9.7 proposed changing the call sites to check the return value. **Revised:
instrument the helper instead.** There are eighteen `(void)`-cast call
sites across eight operations (the table in §11.0), and editing all of them
adds risk and noise for no extra information. One `'H'` record with a
`SOURCE_STAGE_REFUSED` flag inside the `!filesystem_residentSourceValid()`
branch covers every current and future caller, in the same spirit as the
universal `'E'` witness in `filesystem_complete()`.

---

### 11.5 Group D — retire the second owner (`Core/Menu/menu.c`)

Only after Group A has landed, or the Kit-family rows have no owner at all.

- **D1** — `menu_endResidentNameScratchSession()`
  ([menu.c:3505-3545](Core/Menu/menu.c#L3505)): delete the
  `filesystem_requestUpdateResidentKitNames()` branch and its two
  `menu_traceInstrumentEntry(... HCNAMES_FLUSH ...)` calls; keep the clean
  discard path so the function still ends a browse session.
- **D2** — `menu_refreshResidentNameScratchKit()` and
  `menu_residentNameDirtySceneMask`: delete the function, its declaration,
  the accumulation call at [menu.c:8103](Core/Menu/menu.c#L8103), and the
  variable.
- **D3** — the two type-row boundary conditions that exist only to trigger
  the flush ([menu.c:8080-8110](Core/Menu/menu.c#L8080) and the Kit-family
  condition directly above it): remove the flush calls and their long
  rationale comments, keep `filesystem_clearNameCache()`.
- **D4** — `menu_residentNameScratchFlushComplete()`
  ([menu.c:3480-3504](Core/Menu/menu.c#L3480)): delete, and remove its
  `HCNAMES_FLUSHED` trace call.
- **D5** — delete the three now-unreferenced public entry points and their
  declarations: `filesystem_requestUpdateResidentInstrumentNames()`
  ([21833](Core/Hardware/SD/filesystem.c#L21833) /
  [filesystem.h:653](Core/Hardware/SD/filesystem.h#L653)),
  `filesystem_requestUpdateResidentKitNames()`
  ([21907](Core/Hardware/SD/filesystem.c#L21907) /
  [filesystem.h:684](Core/Hardware/SD/filesystem.h#L684)),
  `filesystem_requestUpdateResidentSceneNames()`
  ([21979](Core/Hardware/SD/filesystem.c#L21979) /
  [filesystem.h:708](Core/Hardware/SD/filesystem.h#L708)). Two already have
  zero callers today; the third loses its only one at D1.

**Keep untouched:** `menu_residentNameScratchLoaded()`,
`menu_requestKitEntryNames()`, `menu_requestInstrumentEntryNames()`,
`filesystem_requestLoadResidentKitNames()` /
`...InstrumentNames()` / `...SceneName()`, and `filesystem_clearNameCache()`.
That is the browse-cache **read** path and it is working correctly — three
accepted `HCNAMES_REQUEST` records, three `HCNAMES_COMPLETE` (§10.1).

---

### 11.6 Group E — trace, tools, documentation

- **E1** `Core/Bank/Scene/AutosaveTrace.h`
  - `AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_OVERLAY_REFUSED (1u << 0)` and
    `AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_SOURCE_STAGE_REFUSED (1u << 1)`, with
    the `'H'` doc comment extended: `flags == 0` keeps the existing Session
    056 scratch-drift layout; nonzero flags select the register-guard layout
    (row in bits 0..15, cache kind in bits 16..31).
  - `AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_HCNAMES_VERIFIED (1u << 2)` on `'K'`.
  - New stage `AUTOSAVE_TRACE_STAGE_NAME_PUBLISH = 'U'` — the generic
    writer's completion witness, so Scene/Kit/Instrument publication is as
    visible as Bank's. flags: bit 0 DONE, bit 1 VERIFIED, bits 2..3 the row
    class (Scene/Kit/Instrument). value32: probe row in bits 0..15,
    destination mask in bits 16..31. One record per publish.
- **E2** `tools/decode_devlogs.py`, `tools/devlog_unpack.py` — `STAGE_ENUM`,
  `STAGE_PRODUCER` and decode logic for `'U'`, plus the new `'H'` and `'K'`
  flag meanings. Both files share these tables and both must be edited.
- **E3** `knowledge_files/specification_reference/DEV_MODES.md` — the `'U'`
  paragraph, the extended `'H'`/`'K'` paragraphs, the stage-letter list
  (`... B S A V M C P T X O U E`), and the §10.2 publication map as
  reference material.
- **E4** `tools/verify_bank_autosave.py` — up-front whole-file verdict: if
  row 0's source token does not equal `settings.cfg`'s `active_bank`, print
  **"HCNAMES was not rewritten by the last Bank operation"** *before* any
  per-row diff, so a wholly stale register can never again be misread as a
  scattered subset. Also drop or clearly label the "expected" values that
  assume nothing changed after a Load (§2).
- **E5** `MEMORY.md` — replace the Session 056 entries that describe the
  Names-Corruption fix as addressing an observed symptom (§9.6 withdrew
  that), and record the publication-ownership invariant I1 so the next
  session does not reintroduce a UI-boundary publisher.

---

### 11.7 SRAM impact

**No new persistent SRAM is required by this plan.** Explicitly:

| Item | Static SRAM | Note |
|---|---|---|
| A1 `filesystem_beginResidentNamePublish()` | 0 | reuses `current_op` / `op_phase` |
| A2-A6 handoffs | 0 | reuse `op_kit_load_scene_mask`, `op_scene_load_scene_mask`, `op_slot` |
| B1 `filesystem_residentRowMatchesCache()` | 0 | ~16 bytes **transient stack** (one 9-byte name + one `uint16_t` + indices) inside the existing tick frame |
| B2-B4 verify phases | 0 | reuse `op_file`, `op_line_buf`, `op_line_len`, `op_item_offset`, `op_close_done` |
| C1-C2 trace records | 0 | existing ring |
| D1-D5 removals | **−2 bytes** | `menu_residentNameDirtySceneMask` (`uint16_t`) is deleted |
| E1-E5 | 0 | header constants, Python, docs |

Net static SRAM: **−2 bytes.** Transient stack: **≈16 bytes** added to the
deepest filesystem tick frame, which already carries `op_line_buf`-sized
locals elsewhere. Flash/`.text`: rough estimate **+400 to +700 bytes** for
six handoffs, two helpers and six verify phases, minus what D1-D5 remove.

**One item in `SCOPING_TARGETS.md` would need real SRAM and is deliberately
not in this plan:** giving HCNAMES a dedicated 129 × 9 = **1,161-byte**
name register instead of borrowing `fs_list_cache_name`. That is the
architectural fix for the whole borrow hazard class, and it needs your RAM
Allocation Approval. It is written up there as a refactor candidate, not
proposed here.

### 11.8 Landing order

1. **C then B** — loud failures and the proof gate first. If they land
   alone and the next Bank Load reports `HCNAMES_VERIFIED` clear with no
   `'H'` records, Defect B is confirmed to be below the publication layer
   and Group A would have changed nothing observable. Landing these first
   is what stops a fourth speculative round.
2. **A** — the coverage fixes.
3. **D** — remove the second owner, only once A is proven on hardware.
4. **E** — alongside whichever group introduces each record.

### 11.9 Flow differences between Load/Save types

There are four structurally different completion shapes across the eight
operations, three different chaining idioms that all bypass
`filesystem_start()`, and two competing publication owners. That is the
reason this defect could exist in three different forms simultaneously.
Recorded, with refactor candidates, in `SCOPING_TARGETS.md` §"Session 056 —
resident-name publication ownership and Load/Save completion shapes".

## 12. Implementation notes — 2026-08-23

Section 11 is now implemented.

- Group A: `filesystem_beginResidentNamePublish()` centralizes the HCNAMES
  handoff. Normal Kit Load, Kit Save, Instrument Load, Instrument Save, Scene
  Load, and Scene Save now publish from their filesystem completion paths. Scene
  publication covers its Scene row plus the matching Kit row and six Instrument
  rows. Morph-only Kit and Instrument Save/Load paths remain identity-neutral.
- Group B: the generic HCNAMES writer emits `U` after reopening the register and
  probing the lowest affected row. Bank Load/Save reopen and probe row 0, and
  the existing Preset `K` witness now carries the Bank verification bit. A
  mismatch is recorded without failing an already-committed payload operation.
  Bank Load's existing retry states already occupied phases 88/89, so its probe
  states use phases 93..97; Bank Save uses 88..92.
- Group C: rejected HCNAMES overlays and rejected source stages now emit `H`
  refusal records with row/detail coordinates.
- Group D: the Menu deferred Kit-family writer, dirty Scene mask, and three
  public deferred-update entry points were removed. Menu still performs the
  HCNAMES read needed by Kit/Instrument browsing and discards that read session
  at UI boundaries; it no longer owns durable publication.
- Group E: `AutosaveTrace.h`, both development-log decoders,
  `DEV_MODES.md`, `verify_bank_autosave.py`, and `MEMORY.md` document the new
  witnesses and filesystem publication-ownership invariant. The validator now
  reports `HCNAMES was not rewritten by the last Bank operation` before its
  per-row differences when row 0's source disagrees with `settings.cfg`.

Validation completed:

- `make clean && make` — passed; only pre-existing unused-function/toolchain
  warnings remain.
- `make img` — passed and regenerated `build/LXRV2_lxr02.img`.
- `python3 -m py_compile tools/decode_devlogs.py tools/devlog_unpack.py
  tools/verify_bank_autosave.py` — passed.
- `git diff --check` — passed.
- The existing `SD_CARD/` fixture still reports the historical frozen HCNAMES
  image, now with the new whole-file verdict first; no hardware retest has yet
  exercised the new `U`/`K` evidence or confirmed the physical write fix.

---

## 12. Implementation review, and one required correction to §11.3

Review of the landed implementation against §11. Read-only pass; the only
files touched by this review were build artefacts.

### 12.1 Verdict — §11 is implemented, and implemented correctly

| §11 change | Landed at | Notes |
|---|---|---|
| A1 shared handoff helper | `filesystem_beginResidentNamePublish()`, filesystem.c:5037 (decl 1204) | six call sites, all identical; three pre-existing open-coded handoffs converted |
| A2 Kit Load publish | `case 28`, filesystem.c:9155 | guarded `op_close_status == DONE && current_op == FS_INTERNAL_OP_LOAD_KIT`; Morph correctly excluded |
| A3 Kit Save publish | `case 21`, filesystem.c:14054 | published **after** arming the index rebuild and **before** `filesystem_finish()` — the ordering that mattered |
| A4 Instrument Load publish | `case 16`, filesystem.c:12053 | `!op_instrument_load_temporary` guard present; mask/`op_slot` translation present |
| A5 Instrument Save morph | filesystem.c:12494 | left excluded, with the policy stated in the `else` branch as requested |
| A6 Scene → all three row classes | filesystem.c:5240 **and** 5451 | both dispatch copies, including the first-use bootstrap |
| B1 verify helper | `filesystem_residentRowMatchesCache()`, filesystem.c:4957 | re-parses the read-back line and compares to the live cache; masks the dirty bit; no second line buffer |
| B2 generic verify | phases 9-14, filesystem.c:5325-5417 | |
| B3 Bank Load verify | phases 93-97, filesystem.c:11526-11592 | |
| B4 Bank Save verify | phases 88-92, filesystem.c:14627-14720 | |
| C1 overlay refusal | `filesystem_cacheResidentName()`, filesystem.c:4753 | |
| C2 source-stage refusal | `filesystem_setResidentSource()`, filesystem.c:4541 | instrumented in the helper, not the eighteen call sites, as §11.4 revised |
| D1-D5 retire second owner | menu.c −337 lines; all three `filesystem_requestUpdateResident*Names()` deleted | browse-read path (`menu_residentNameScratchLoaded()`, `filesystem_requestLoadResident*`) correctly retained |
| E1-E5 trace/tools/docs | `'U'` stage, `H` refusal flags, `K` bit 2, both decoders, DEV_MODES stage list | |

Two implementation choices that improve on the plan and should be kept:

- `filesystem_residentPublishMask()` / `...ProbeRow()` / `...Class()`
  (filesystem.c:4910-4954) factor the probe-row selection §11.3 specified as
  a table into three small helpers, and correctly return
  `op_scene_load_scene_mask` for the Scene class *after* A6 overwrites
  `op_kit_load_scene_mask`. That subtlety was not called out in §11 and was
  got right anyway.
- The Bank Save failed-open path (filesystem.c:14643) re-arms
  `op_library_index_rebuild_kind/_pending` rather than falling through, so a
  probe that cannot open the register still rebuilds `/Bank/.hcindex`.

**Build:** clean `make clean && make` succeeds with no warnings from any
changed file. `text=381740 data=400 bss=94744`.

**SRAM claim independently verified.** Rather than trusting the map file,
the diff was scanned for file-scope `static` declarations: **zero added,
one removed** (`menu_residentNameDirtySceneMask`, `uint16_t`). Net **−2
bytes**, exactly as §11.7 stated. The verify path allocates nothing: it
reuses `op_bytes_done` for the probe row and `op_hcnames_probe_matches` for
the boolean result, and all three sites explicitly zero the latter before
use, so its dual role with the root-absence probe counter is safe.

---

### 12.2 Required correction — the "witness, do not fail" policy is not fully applied

§11.3 states the policy explicitly: *"**Policy: witness, do not fail.** A
mismatch must not turn a good payload Load into an error."*

The implementation honours this for a **mismatch** (the probe simply leaves
`op_hcnames_probe_matches` at zero) and for a **failed open** (the phase
above returns `FS_STATUS_DONE` with the witness clear). It does **not**
honour it for a **read error on the probe**. Three identical sites:

| Writer | Read-error branch | Terminal phase |
|---|---|---|
| generic | `case 12`, filesystem.c:5367 | `case 14`, filesystem.c:5405 |
| Bank Load | `case 95`, filesystem.c:11552 | `case 97`, filesystem.c:11583 |
| Bank Save | `case 90`, filesystem.c:14662 | `case 92`, filesystem.c:14693 |

Each sets `op_close_status = FS_STATUS_ERROR`, and each terminal phase then
does:

```c
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_finish(op_close_status);
            return;
        }
```

#### What that costs when it fires

The register has already been streamed, closed and flushed by the time the
probe runs. The probe is read-only. So a probe I/O error cannot change what
is on the card — but under the current code it:

1. **Reports a fully successful operation as a filesystem failure.** Preset
   completes with `pm_completed_ok == 0`; Menu shows the error overlay for a
   Load whose payload *and* whose register both landed correctly.
2. **Skips `filesystem_clearResidentSourceDirtyFlags()`**, so staged source
   words keep their `FS_RESIDENT_SOURCE_DIRTY_FLAG` set. The next
   `filesystem_prepareResidentNamesCache()` preserves them, and the next
   unrelated publication writes this operation's provenance into its
   register image. This is a real cross-operation data leak, not merely a
   status problem.
3. **Skips the `/Bank/.hcindex` rebuild** on Bank Save (`case 92` returns
   before arming it), so a newly created or renamed Bank folder stays
   invisible to the browser until reboot. Note the failed-*open* path at
   filesystem.c:14643 correctly arms it — only the read-error path does not.
4. **Makes the new instrument lie.** The generic writer never emits its `U`
   record at all on this path. Bank still emits `K` from
   `on_bank_load_complete()`, but with `load_done == 0`, and the
   `(load_done && hcnames_verified)` guard therefore reports
   **"not DONE, not verified"** for an operation whose register write
   succeeded.

Point 4 is the one that matters. Group B exists precisely because five Bank
operations completed `DONE` against an unchanged card and nothing recorded
it. If the card is doing something anomalous at the storage layer — which
is exactly what Defect B suspects — the read-back probe is where that
anomaly surfaces first, and the current code responds by discarding the
evidence and emitting a witness that says the opposite of what happened.

#### The correction — summary

Six small edits in `filesystem.c`, plus four documentation updates that keep
the header and trace contracts truthful about what a clear VERIFIED bit now
means. No new state, no SRAM, no behaviour change on any path that currently
works. **The full change-by-change plan is §12.6.**

**At each of the three read-error branches** — delete the
`op_close_status = FS_STATUS_ERROR;` line so the phase simply advances to
its close phase, leaving `op_hcnames_probe_matches` at zero:

```c
        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            /*
             * A failed read-back is evidence, not a payload failure.
             *
             * What: abandons the probe and proceeds to the normal close,
             * leaving op_hcnames_probe_matches at zero so the K/U witness
             * reports VERIFIED clear.
             *
             * Why: the register was streamed, closed and flushed before this
             * phase ran, and this probe is read-only, so nothing here can
             * change what is on the card. Promoting a probe I/O error to
             * FS_STATUS_ERROR would (a) report a committed, correctly
             * published operation as a filesystem failure, (b) skip
             * filesystem_clearResidentSourceDirtyFlags() and leak this
             * operation's staged source words into the next transaction,
             * (c) on Bank Save, skip the /Bank/.hcindex rebuild, and (d)
             * emit a witness claiming "not DONE, not verified" for an
             * operation that succeeded -- destroying the evidence in exactly
             * the scenario this probe was added to illuminate.
             *
             * Inputs: the read status only. Output: op_phase only. This
             * matches the failed-open policy in the phase above and the
             * mismatch policy in the branch below, so all three probe
             * outcomes now behave identically. Affiliates:
             * filesystem_recordResidentNamePublish(),
             * AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_HCNAMES_VERIFIED, and
             * S056_HCNAMES_FOLLOW_UP.md sections 11.3 and 12.2.
             */
            op_phase = 13u;   /* 91u for Bank Load, 91u for Bank Save */
            return;
        }
```

**At each of the three terminal phases** — delete the
`if (op_close_status != FS_STATUS_DONE)` early-return block, so each has a
single unconditional exit that emits the witness, clears the dirty flags,
arms any pending index rebuild, and finishes `FS_STATUS_DONE`. Add one line
to each phase's existing comment:

```c
         * The probe has exactly one exit: whatever it observed, the payload
         * and register work already completed, so this phase always publishes
         * the witness and completes DONE. See section 12.2.
```

After this, `op_close_status` is never written by the verify phases. The
`op_close_status = FS_STATUS_DONE;` initialisations at generic `case 11`,
Bank Load `case 86` and Bank Save `case 86` become defensive no-ops and can
be left in place.

---

### 12.3 How necessary is this correction?

Separated by what it actually affects, because the answer differs sharply.

**For correct `/.hcnames` generation: not necessary.** The probe runs
strictly after the register file has been written, closed and flushed. It
opens the file read-only and never writes. Nothing in this correction
changes a single byte that reaches the card, on any path. If Groups A and B
are otherwise correct, `.hcnames` will be generated correctly with or
without this fix.

**For correct Load/Save behaviour: necessary, but only on an error path.**
The trigger is a genuine card I/O error while reading a file the firmware
closed milliseconds earlier. A malformed register cannot trigger it — the
preserve-read at phase 2 uses the same status check and would already have
failed the operation long before the probe. So the probability is low. But
the blast radius is disproportionate to the trigger: a false failure
reported to the user, a genuine cross-operation provenance leak (point 2),
and a skipped Bank index rebuild (point 3). A diagnostic probe should not
be able to break an operation it was added only to observe.

**For the next hardware test being conclusive: necessary.** This is the
decisive consideration. §11.8 puts Groups C and B first specifically so that
one ordinary Load answers whether Defect B is real. The three outcomes
§11.9 enumerates all assume the witness is trustworthy. Under the current
code there is a fourth, unlisted outcome — probe read error — in which the
generic witness is absent entirely and the Bank witness actively reports the
opposite of the truth. That is the same category of misleading evidence that
produced three wrong diagnoses in §§3, 4 and 9.5, and it would be a poor
trade to reintroduce it inside the instrument built to prevent it.

**Verdict: fix before the next hardware test, not urgently otherwise.** Six
line deletions and three comment blocks across three sites, no new state,
zero SRAM, and it cannot regress any path that currently works — every
change is on a branch that today produces a wrong answer.

---

### 12.4 Two minor follow-ups (non-blocking)

- **Retired trace constants.**
  `AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSH` and `_FLUSHED`
  (AutosaveTrace.h:321-322) are now unproducible — Group D removed their only
  producer — but are not marked as such. The file already has the right
  pattern for this: stage `'Y'` carries a "RETIRED — no longer producible"
  block explaining what removed it and why the layout is kept for old
  captures. These two deserve the same treatment, or a future reader will
  hunt for a Menu flush path that no longer exists.
- **Dead caller branches.** `menu_endResidentNameScratchSession()` now always
  returns zero, which makes the bodies of
  `if (... && menu_endResidentNameScratchSession()) { ... }` at menu.c:3465
  and menu.c:5150 unreachable. The function's own comment says the zero
  return is deliberate, to preserve caller shape, so this is intentional
  rather than an oversight — but it leaves two dead blocks for a later
  cleanup pass.

### 12.5 One assumption to confirm on hardware, not by reading

A3 assumes `op_library_index_rebuild_kind/_pending`, armed in Kit Save's
`case 21` *before* the handoff, survives the `current_op` swap and fires
after the register transaction completes. The code is arranged correctly and
`filesystem_beginResidentNamePublish()` does not touch those fields, but it
is the one interaction in this change set whose failure is **silent**: the
Kit `/Kit/.hcindex` would simply not rebuild, and nothing would report it.
Worth an explicit check on the next Kit Save — save a Kit to a new slot and
confirm it appears in the browser without a reboot.

---

### 12.6 Implementation plan — the §12.2 correction

Ten changes across four files. Every comment block below is written to be
pasted in place as the change's own documentation.

Shared rationale, stated once so each site's comment can stay short: **the
read-back probe is strictly posthumous.** By the time any of these phases
runs, the register has been streamed, closed by `afatfs_fclose()` and
flushed through `filesystem_finish()`'s gate, and the probe reopens it
`"r"`. No probe outcome can change a byte on the card. Therefore all three
probe outcomes — *matched*, *mismatched*, *could not be read* — must have
identical control flow, differing only in the witness bit they publish.

#### C1 — generic writer: stop promoting a probe read error to `FS_STATUS_ERROR`

*File:* `Core/Hardware/SD/filesystem.c`
*Site:* `filesystem_residentNames_tick()`, `case 12 /* READ THROUGH THE
GENERIC PUBLISH PROBE ROW */`, the `read_status` guard (~5375)

```c
        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
-           op_close_status = FS_STATUS_ERROR;
            op_phase = 13u;
            return;
        }
```

Comment to add above `op_phase = 13u;`:

```c
            /*
             * A failed read-back is evidence, not a payload failure.
             *
             * What: abandons the probe and proceeds to the normal close,
             * leaving op_hcnames_probe_matches at zero so the U witness
             * reports VERIFIED clear.
             *
             * Why: this phase runs after the register was streamed, closed
             * and flushed, and it holds a read-only handle, so nothing here
             * can change what is on the card. Promoting a probe I/O error to
             * FS_STATUS_ERROR would report a committed, correctly published
             * operation as a filesystem failure; skip
             * filesystem_clearResidentSourceDirtyFlags() in case 14 and leak
             * this operation's staged source words into the next
             * transaction's preserve-read; and suppress the U record
             * entirely -- destroying the evidence in exactly the scenario
             * this probe was added to illuminate. See
             * S056_HCNAMES_FOLLOW_UP.md sections 11.3 and 12.2.
             *
             * Inputs: read_status only. Outputs: op_phase only; op_close_status
             * is deliberately left at the FS_STATUS_DONE set in case 11.
             * Affiliates: filesystem_recordResidentNamePublish(), case 11's
             * failed-open policy, and the mismatch policy in the branch below
             * -- all three probe outcomes now share one control flow.
             */
```

#### C2 — generic writer: single unconditional exit

*Site:* same function, `case 14 /* WAIT PROBE CLOSE + PUBLISH WITNESS */` (~5411)

```c
        if (!afatfs_chdir(NULL))
            return;
-       if (op_close_status != FS_STATUS_DONE) {
-           filesystem_finish(op_close_status);
-           return;
-       }
        filesystem_recordResidentNamePublish(1u);
```

Comment to add above `filesystem_recordResidentNamePublish(1u);`:

```c
        /*
         * The probe has exactly one exit.
         *
         * What: publishes the U witness, clears the staged source dirty bits,
         * and completes the original operation DONE, regardless of what the
         * probe observed. Why: the payload and the register write both
         * completed before this phase was reachable; the probe's only product
         * is one bit of evidence. A conditional exit here previously allowed a
         * read error to fail a successful publication and skip the dirty-flag
         * clear. Inputs: op_hcnames_probe_matches and the probe row in
         * op_bytes_done. Outputs: one U record, cleared provenance staging,
         * and FS_STATUS_DONE to the parked callback. Affiliates:
         * filesystem_recordResidentNamePublish(),
         * filesystem_clearResidentSourceDirtyFlags(), and section 12.2.
         */
```

#### C3 — Bank Load: same read-error branch

*Site:* `filesystem_loadBankDirectory_tick()`, `case 95 /* READ BANK ROW 0 AND
START CLOSE */` (~11560). Identical edit to C1: delete
`op_close_status = FS_STATUS_ERROR;`, keep `op_phase = 96u;`. Reuse C1's
comment with two substitutions — "case 97" for "case 14", and "the K witness
in on_bank_load_complete()" for "the U witness", plus one added sentence:

```c
             * For Bank the loss is worse than a missing record: K is still
             * emitted by Preset, but with load_done clear, so its
             * (load_done && hcnames_verified) guard publishes
             * "not DONE, not verified" for an operation whose register write
             * actually succeeded.
```

#### C4 — Bank Load: single unconditional exit

*Site:* `case 97 /* WAIT CLOSE + COMPLETE BANK LOAD */` (~11589). Delete the
`if (op_close_status != FS_STATUS_DONE) { filesystem_finish(op_close_status);
return; }` block. Reuse C2's comment, dropping the U-record sentence (Bank's
witness is emitted later by Preset, not here).

#### C5 — Bank Save: same read-error branch

*Site:* `filesystem_saveBankDirectory_tick()`, `case 90 /* READ BANK-SAVE ROW 0
AND START CLOSE */` (~14670). Identical edit; `op_phase = 91u;` retained.
Add one further sentence to the comment, because Bank Save carries an extra
consequence:

```c
             * Bank Save additionally arms op_library_index_rebuild_kind /
             * _pending in case 92. Failing out of the probe skipped that,
             * leaving a newly created or renamed root Bank folder invisible
             * to the browser until reboot -- note that the failed-open path
             * above already arms it correctly, so only this branch diverged.
```

#### C6 — Bank Save: single unconditional exit

*Site:* `case 92 /* WAIT CLOSE + RESTORE ROOT BANK INDEX */` (~14699). Delete
the same early-return block. The existing comment about parking the callback
for the `/Bank/.hcindex` rebuild stays; prepend C2's "single exit" paragraph
so the index-rebuild arming is visibly unconditional.

#### C7 — `filesystem.h`: state what a clear VERIFIED bit means

*File:* `Core/Hardware/SD/filesystem.h`
*Site:* the doc comment above `filesystem_lastHcnamesVerified()` (~616-625)

The function's behaviour does not change, but its **contract** does: after
C1-C6, a clear result no longer implies "the register is wrong". Callers
must not treat it as a failure signal. Replace the existing comment with:

```c
/*
 * Return the last Bank-owned row-0 HCNAMES read-back result.
 *
 * What: exposes the witness-only boolean retained through Bank completion so
 * Preset can add bit 2 to its existing K trace record.
 *
 * Why: FS_STATUS_DONE and the callback do not by themselves prove that the
 * visible register contains the just-staged Bank identity -- five Bank
 * operations across two sessions completed DONE against a byte-identical
 * /.hcnames before this probe existed.
 *
 * A zero result has three distinct causes and distinguishes none of them:
 * the probe row did not match the staged cache/source pair, the register
 * could not be reopened, or the read-back itself failed. All three are
 * deliberately equivalent here, because none of them can affect what was
 * already written to the card. Callers must treat this strictly as
 * diagnostic evidence: it must never gate payload completion, menu state, or
 * any retry. A false clear costs one ambiguous trace bit; a false failure
 * would cost a correctly saved Bank.
 *
 * Inputs/outputs are RAM-only. The value is reset by the next Bank proof.
 * Affiliates: on_bank_load_complete()/on_bank_save_complete(),
 * AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_HCNAMES_VERIFIED, and
 * S056_HCNAMES_FOLLOW_UP.md sections 11.3 and 12.2.
 */
uint8_t filesystem_lastHcnamesVerified(void);
```

#### C8 — `AutosaveTrace.h`: make the witness bits self-describing

*File:* `Core/Bank/Scene/AutosaveTrace.h`
*Sites:* the `'K'` stage comment (~160-167) and the `'U'` stage comment
(~168-174)

Both currently document a set VERIFIED bit and say nothing about a clear
one, which invites the reading "clear means the register is stale" — the
same over-reading that produced §§3, 4 and 9.5. Append to each:

```c
     * A clear VERIFIED bit is ambiguous by design: it means the probe did
     * not confirm a match, which covers a genuine mismatch, a register that
     * could not be reopened, and a failed read-back. It never means the
     * operation failed -- the payload and the register write both completed
     * before the probe ran, and no probe outcome can alter the card. Treat a
     * clear bit as "look at this card capture", not as a fault report.
```

#### C9 — decoders: print the ambiguity, not just the bit

*Files:* `tools/decode_devlogs.py`, `tools/devlog_unpack.py`

Both already decode `K` bit 2 and `U` bit 1. Extend the rendered text so a
future forensic pass is not misled, e.g.

```
HCNAMES_VERIFIED=0 (probe did not confirm: mismatch, reopen failure, or
read failure -- not an operation failure)
```

Both files carry the same tables and both must be edited; they are checked
by decoding `SD_CARD2/asavetrc.bin` and confirming no traceback.

#### C10 — `DEV_MODES.md`: same wording for the `K` and `U` paragraphs

*File:* `knowledge_files/specification_reference/DEV_MODES.md`

The `U` paragraph already says "A mismatch is witnessed, not promoted to a
payload failure." Extend it and the `K` paragraph with the three-cause
ambiguity from C8, so the card-side documentation and the header agree.

---

### 12.7 Implementation plan — the §12.4 follow-ups

#### F1 — mark the unproducible entry phases RETIRED

*File:* `Core/Bank/Scene/AutosaveTrace.h`
*Site:* `AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSH` (4u) and
`_HCNAMES_FLUSHED` (5u), ~317-318

Group D deleted their only producer. Keep the values — old captures decode
against them, and §10.1's "zero FLUSH records in 69,012 events" is the
central evidence of this whole investigation — but say so, following the
existing precedent set by stage `'Y'`:

```c
/*
 * RETIRED -- no longer producible. Values 4 and 5 were emitted by
 * menu_endResidentNameScratchSession() when it requested, and
 * menu_residentNameScratchFlushComplete() when it completed, the Menu-side
 * deferred Kit-family HCNAMES write.
 *
 * That publication path was removed entirely: identity publication now
 * belongs to the filesystem operation that changed the data, not to a UI
 * boundary. The reason it was removed is that it never once executed --
 * zero phase-4 and zero phase-5 records across 69,012 cumulative trace
 * events spanning every recorded session, which is precisely why Kit Load,
 * Kit Save and Instrument Load published nothing for months.
 *
 * The numbers are reserved and must not be reused: existing card captures
 * decode against them, and their *absence* is the primary evidence in
 * S056_HCNAMES_FOLLOW_UP.md section 10.1. New publication witnesses use
 * stage 'U' instead.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSH     4u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSHED   5u
```

Optionally mirror one line of this into the decoders' phase tables.

#### F2 — hoist the now-unconditional session-end calls

*File:* `Core/Menu/menu.c`
*Sites:* ~3463 and ~5149

**Read this before editing.** `menu_endResidentNameScratchSession()` now
always returns zero, so the *bodies* of these two `if` statements are dead —
but the **call itself is still required**: it discards the seven-name browse
scratch and releases the shared cache through `filesystem_clearNameCache()`.
Deleting the call along with the dead branch would leak a stale browse
session into the next Scene's Kit/Instrument entry. Hoist it, do not remove
it.

Site 1 (~3463), inside the entry-name request helper:

```c
-   if (menu_residentNameScratchValid &&
-       menu_residentNameScratchScene != scene &&
-       menu_endResidentNameScratchSession()) {
-       return 1u;
-   }
+   /*
+    * End a browse session that belongs to a different Scene before starting
+    * this one. The helper is synchronous since HCNAMES publication moved to
+    * the filesystem (S056 section 11.5), so it always returns zero and this
+    * is a plain statement rather than a deferred-completion branch. The call
+    * itself is still required: it releases the seven-name scratch and the
+    * shared cache.
+    */
+   if (menu_residentNameScratchValid &&
+       menu_residentNameScratchScene != scene) {
+       (void)menu_endResidentNameScratchSession();
+   }
```

Site 2 (~5149), the Kit-family type-row boundary:

```c
-   if (what != SAVE_TYPE_KIT && what != SAVE_TYPE_KIT_MORPH &&
-       menu_endResidentNameScratchSession()) {
-       menu_refreshLoadSceneLeds();
-       return;
-   }
+   /*
+    * Leaving the Kit family ends the browse session. Kit <-> KitMrp and
+    * Kit <-> Instrument transitions deliberately keep it, so the type guard
+    * must be preserved exactly. Nothing is deferred any more, so control
+    * falls through to the ordinary cache release and browser reselect below.
+    */
+   if (what != SAVE_TYPE_KIT && what != SAVE_TYPE_KIT_MORPH) {
+       (void)menu_endResidentNameScratchSession();
+   }
    filesystem_clearNameCache();
```

The two existing `(void) menu_endResidentNameScratchSession();` statement
sites (~7089, ~8642) already have the correct shape and need no change.

Once both are hoisted, `menu_endResidentNameScratchSession()` has no
remaining value-consuming caller and its return type can be narrowed to
`void`, with its comment's "return value remains zero so existing callers
keep their nonblocking boundary shape" sentence deleted. That is optional
and can be a separate commit.

---

### 12.8 Cost, verification, and acceptance

**Cost.** Six deletions and six comment blocks in `filesystem.c`; one
rewritten comment in `filesystem.h`; two appended paragraphs and one retired
block in `AutosaveTrace.h`; two hoists in `menu.c`; text in two Python
decoders and one spec document.

**SRAM: zero.** No variable is added, removed, widened or narrowed by
§12.6. F2 removes no storage either. The net figure for the whole Session
056 change set therefore stays at **−2 bytes** (§12.1).

**Flash:** slightly negative — six branches removed, comments only otherwise.

**Verification, in order:**

1. `make clean && make` — required, because `AutosaveTrace.h` and
   `filesystem.h` both change and the Makefile has no header dependency
   tracking (`SCOPING_TARGETS.md`). Then `make img`; confirm `bss` is
   unchanged from 94744.
2. `python3 tools/decode_devlogs.py SD_CARD2/asavetrc.bin > /dev/null` —
   both decoders must still parse an existing capture after C9/F1.
3. Grep-check that no `op_close_status` assignment remains inside any probe
   phase: `grep -n "op_close_status = FS_STATUS_ERROR" Core/Hardware/SD/filesystem.c`
   should show no hit between the `case 88`-`case 97` ranges of either Bank
   state machine or `case 9`-`case 14` of `filesystem_residentNames_tick()`.
4. Grep-check that `menu_endResidentNameScratchSession()` has no remaining
   value-consuming caller.

**Acceptance on hardware** — one ordinary session covers all of it:

| Action | Expect |
|---|---|
| boot (Bank Load) | `K` with DONE set; VERIFIED set if the register landed |
| Load a Kit into any Scene | `U` with class=Kit, and the Kit row + six Instrument rows updated in `/.hcnames` |
| Load an Instrument | `U` with class=Instrument, one row updated |
| Load a Scene | `U` with class=Scene, and **eight** rows updated for that Scene, not one |
| Save a Kit to a new slot | `U` with class=Kit, row source equal to the new slot, **and the Kit appears in the browser without a reboot** (§12.5's silent-failure check) |
| any `H` record at all | a lost overlay or a rejected source stage — investigate before trusting the rest |

If `/.hcnames` still does not change while `U`/`K` report VERIFIED clear on
every operation, Defect B is confirmed below the publication layer and the
next step is the storage layer, as §11.9 sets out — but that conclusion is
only trustworthy with §12.6 landed, which is the entire argument for doing
it first.

### 12.9 Implementation of the §12.2 correction and §12.4 follow-ups — 2026-08-23

The required correction is now landed.

- Generic HCNAMES publication, Bank Load, and Bank Save no longer promote a
  read-back probe error to `FS_STATUS_ERROR`. Each probe now abandons only its
  diagnostic read, closes the read handle, preserves the already-completed
  payload/register result, clears staged source dirtiness, and emits the
  appropriate witness path. A clear verification bit now covers mismatch,
  reopen failure, and read failure without claiming that the operation failed.
- The three probe terminal phases now have unconditional DONE exits. Bank Save
  therefore always reaches its existing `/Bank/.hcindex` rebuild handoff, and
  generic publication always emits `U` after a probe read error.
- `filesystem.h`, `AutosaveTrace.h`, `DEV_MODES.md`, and both decoders now use
  the same three-cause ambiguity contract for `K`/`U` verification bits.
- The retired Menu HCNAMES flush phase values 4/5 are explicitly reserved for
  old captures. Menu's two value-consuming scratch-session branches were
  hoisted into synchronous cleanup calls, and the helper now returns `void`;
  the remaining call sites retain the required cache disposal.

Validation is pending the required clean rebuild, image generation, decoder
parse, probe-branch grep, and diff checks.
