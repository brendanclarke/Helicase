# Session 056 — Bank Load/Save + AutoSave Hardware Test Results

**Date**: 2026-08-22
**Test performed by user**: one Bank Load, triggered from the Load:Bank menu
**while the sequencer was actively playing**, then `SD_CARD/` in the repo was
overwritten with the resulting card image for analysis.
**This session**: read-only analysis of the copied card. No firmware changes
were made.

## TL;DR

- The Bank Load itself worked: HCNAMES was rewritten with the new Bank's
  identity almost perfectly (127 of 128 rows correct).
- **One real bug found**: HCNAMES row 9 (Scene 08's identity) was corrupted
  with an unrelated root-library name (`Moch to`) instead of the correct
  Bank-local Scene name (`KitWool`). This is new evidence for the "shared
  name-cache" hazard class already flagged (but not yet fixed) in
  `MEMORY.md` / Session 040 / Session 055.
- `settings.cfg` and both AutoSave records (`.hcprms1`/`.hcprms2`) still show
  the **previous** Bank (011 "Full"), not the loaded Bank (012 "LoadTst").
  This is most likely explained by the card being pulled before the 1s
  settings debounce and 5s AutoSave debounce had a chance to fire — not
  proven to be a functional bug. **Needs a retest with a deliberate wait
  before removing the card** (see Recommended Retest below).
- The trace ring (`asavetrc.bin`, 2,048 records) overflowed during this
  boot's whole-resident-Bank dirty mark, dropping 6,134 records and leaving
  **zero trace evidence for the manual Bank Load itself** — everything
  decodable in the file is from this boot's automatic startup Bank Load, not
  the user's interactive test. This is the already-known, still-undecided
  `AUTOSAVE_TRACE_RECORD_COUNT` item in `MEMORY.md`; it directly limited how
  much this investigation could conclude from the trace alone.
- `SD_CARD/Bank/old012-c76c/` is pre-existing debris from ~12 days before
  this test (host-clock dated 2026-08-10) using a legacy `old<slot>-<hash>`
  naming scheme retired by Session 053. It is unrelated to this test;
  recommend deleting it so future card audits aren't confused by it.

---

## 1. What was tested

Card contents were checked with `tools/verify_bank_autosave.py` (compares
`.hcnames`, `settings.cfg`, and the winning AutoSave record against the
on-disk Bank tree) and `tools/devlog_unpack.py` (decodes `asavetrc.bin`).

```
$ python3 tools/verify_bank_autosave.py SD_CARD 12
Bank AutoSave validation: FAIL
  slot=012 winner=.hcprms1 present_mask=0x0000 expected=0xffff
  - HCNAMES row 9 Scene 08: expected 'KitWool', got 'Moch to'
  - settings.cfg active_bank: expected 12, got 11
  - .hcprms1 Bank restore slot: expected 12, got 11
  - .hcprms1 Bank name: expected 'LoadTst', got 'Full'
  - .hcprms1 active_scene: expected 5, got 0
  - .hcprms1 voice edit mask: expected 0x0020, got 0x0000
  - .hcprms1 scene_present_mask: expected 0xffff, got 0x0000
  - .hcprms1 Scene 05/08 payload does not match sceneset.scg/kitset.kcg
```

Bank 012 "LoadTst" (the Bank actually visible in `Bank/012 LoadTst/` on the
card) was identified as the loaded Bank because HCNAMES matches it almost
exactly — running the same validator against Bank 011 (the slot named in
`settings.cfg`) instead produces ~120 mismatches, confirming 012 is the
Bank that was really loaded and 011 is stale leftover state.

## 2. Confirmed working

- **HCNAMES publish**: row 0 (Bank name/source) and 127 of 128 identity rows
  (15 of 16 Scene names, all 16 Kit names, all 96 Instrument names) were
  correctly rewritten to Bank 012's actual on-disk tree. This is the bulk of
  what a Bank Load's HCNAMES commit has to get right, and it did, even with
  playback running concurrently.
- **On-disk Bank tree itself** (`Bank/012 LoadTst/`) is structurally sound:
  16 Scene children, each with a matching Kit folder and six correctly typed
  Instrument members (`kitset.kcg` cross-checked clean for every scene
  sampled).
- **AutoSave format integrity**: the winning record (`.hcprms1`) has a valid
  magic/version/commit byte and a correct CRC32C — whatever it contains is
  internally well-formed, just stale (see §3.2).

## 3. Findings

### 3.1 HCNAMES row 9 (Scene 08) corrupted with a root-library name — real bug

Bank 012's actual Scene 08 folder is `08 KitWool`, confirmed on disk:

```
$ ls "SD_CARD/Bank/012 LoadTst"
...
08 KitWool
...
$ ls "SD_CARD/Bank/012 LoadTst/08 KitWool"
Kit KitWool  effects.fx  pattern.pat  sceneset.scg
```

But HCNAMES row 9 reads `Moch to`, not `KitWool`. Checking every Bank in the
library, **no Bank's Scene 08 is ever named `Moch to`** — so this isn't a
stale copy of the previous Bank's Scene-08 name. Searching the card for that
string finds it as the name of **root-level** `Kit/004 Moch to` and
`Scene/004 Moch to` — an entirely unrelated numbered-library slot:

```
$ grep -rl "Moch" SD_CARD/
SD_CARD/.hcnames
SD_CARD/Kit/.hcindex
SD_CARD/Scene/.hcindex
$ find SD_CARD -iname "*moch*"
SD_CARD/Kit/004 Moch to
SD_CARD/Scene/004 Moch to
...
```

So one of the 16 Scene-identity rows written during this Bank Load's HCNAMES
sweep picked up a **root numbered-library** name instead of the Bank-local
child's own name. `MEMORY.md` already documents this exact class of defect
twice:

- Session 040: "Bank Load must reset shared Scene child-discovery scratch
  before every Bank-local Scene payload. Otherwise a full Bank reuses child
  00's embedded Kit/pattern/effect names for child 01."
- Session 055: "the AutoSave writer reads the shared 9,000-byte name cache
  live while serializing its record, and Menu clears that same cache
  directly... from 18 call sites... needs a proper ownership interlock —
  deliberately left as a `SCOPING_TARGETS.md` item."

Both describe the same shared `fs_list_cache_name[1000][9]` cache being
touched by more than one consumer without full isolation. This test's
specific variable — **playback actively running** during the Bank Load — is
a strong candidate for what introduced the extra concurrent cache access
this time (screensaver/LED/display refresh, or some other foreground poll
that also reads root numbered-library rows out of the same shared cache
mid-sweep). This is the one finding from this test that is not explainable
by timing/debounce and should be treated as a real, reproducible-looking bug.

**Recommended next step**: reproduce with playback running vs. stopped and
see if the corrupted row only appears in the playback case; if so, look for
what runs against `fs_list_cache_name`/root `.hcnames` borrowing during
Bank Load's per-child loop that wouldn't run with the sequencer idle.

### 3.2 `settings.cfg` / AutoSave record still show the previous Bank — likely a capture-timing artifact, not confirmed as a bug

Both `settings.cfg` (`active_bank=11`) and the AutoSave Bank section
(`.hcprms1` bytes @3920..3934: `0b 00 46 75 6c 6c 00 00 00 00 00 00 00 00
00` → slot=11, name="Full") reflect Bank 011, not the loaded Bank 012.

This is almost certainly the boot-time state, not something written *after*
the manual Bank Load:

- `settings.cfg`'s `active_bank=11` matches exactly what boot would have
  auto-loaded before the user's manual Bank Load ever ran.
- The AutoSave Bank section's slot=11/name="Full" exactly matches that same
  boot-time Bank, and the record it's embedded in was produced by the
  **recovery/cold-start path** (`asavetrc.bin` shows `A → V winner=none
  gen=0 → T status=DONE` with no `M`/`C`/`P` in between — the fast
  "merge into an existing valid winner" path was never used, meaning
  neither `.hcprms1` nor `.hcprms2` validated as a prior generation at boot,
  so the firmware rebuilt a fresh baseline from live `BankData` state at
  that time). That baseline capture happened once, early in this boot, and
  nothing in the trace shows a second AutoSave write cycle afterward.
- Both persistence paths are intentionally **debounced/async**:
  `settings.cfg`'s writer needs 1 second of quiet plus an idle filesystem
  facade (`SETTINGS_AUTOWRITE_DEBOUNCE_MS`); the AutoSave writer needs 5
  seconds (`AUTOSAVE_WRITER_INTERVAL_MS`) plus time to validate both
  candidates, capture the dirty payload, and publish. Neither is instant.
- `asavetrc.bin` contains **zero trace records for the manual Bank Load** —
  every decodable `B` (Bank-present-mask) record in the entire file (12
  total, spanning this session and prior ones) has `resident=0xffff
  load=0xffff` at a tick in the 1700–1900ms range, i.e. all from boot-time
  automatic Bank Loads, none from an interactive one. The periodic trace
  flush (`AUTOSAVE_TRACE_FLUSH_INTERVAL_MS`, 500ms) also only runs on an
  idle facade.

The simplest explanation consistent with all of the above: HCNAMES (a
synchronous part of the Bank Load's own commit) updated immediately, but the
three *background* mechanisms — settings debounce, AutoSave debounce, and
trace flush — never got enough idle time to run before the card was pulled.
That is a capture-procedure gap in this test, not necessarily a firmware
defect. It is **not proven either way** by this evidence alone.

### 3.3 Trace ring too small for a whole-Bank operation — known, still undecided

The boot-time whole-resident-Bank dirty mark (`autosave_markResidentBankDirty()`,
run once per boot after AutoSave tracking arms) overflowed the 2,048-record
ring:

```
t=10815 F gate_held pending=2048
t=10997 G dropped=6134
```

6,134 of the diagnostic records from that single sweep were permanently
lost (not just overwritten — the ring rejects new records once full, so
they never reached the file). `MEMORY.md` already flags
`AUTOSAVE_TRACE_RECORD_COUNT` (currently 2,048, temporarily expanded from a
normal default of 64) as "**Needs a decision**, not yet reverted." This test
is a concrete example of that gap costing real diagnostic value — a good
argument for resolving it before the next round of Bank-focused hardware
testing, since Bank operations are exactly the case that produces the
largest dirty-mark bursts.

### 3.4 `Bank/old012-c76c/` — pre-existing debris, unrelated to this test

```
$ ls -la SD_CARD/Bank/old012-c76c
04 Slak/  12 Slak/
```

This uses an `old<slot>-<hash>` naming scheme that predates Session 053's
rewrite of the overwrite path ("no temporary-root or old-name promotion
exists" — see `FILESYSTEM_SPEC.md`; the current quarantine helper produces
`err<name>`, never `old<slot>-<hash>`). Its host-recorded mtime (2026-08-10)
is ~12 days older than everything else touched by this test. It is stale
debris from before the current save/overwrite implementation, not something
this test's Bank Load created. Safe to delete during card cleanup; not a
new defect.

*(Aside: most firmware-written files on this card carry a fixed
`2015-11-30 23:00:00` timestamp — the LXR-02 has no RTC, so FAT mtimes from
the device are not reliable evidence of *when* something happened this
session. Only `settings.cfg` and the host-populated Bank folders carry
real-looking dates, from whatever tool wrote them last with a real clock.)*

---

## 4. Recommended retest

To get a clean answer on §3.2 specifically:

1. Start playback, trigger Bank Load to a Bank different from the currently
   active one (as before).
2. After the Load completes on-screen, **back out to the idle/main screen**
   and **wait at least 15 seconds** (comfortably past the 1s settings
   debounce and the 5s AutoSave debounce plus write time) before pulling the
   card.
3. Re-run `tools/verify_bank_autosave.py SD_CARD <slot>` and check
   `asavetrc.bin` for a fresh `B`/`A`/`V`/`M`/`C`/`P`/`T` sequence tied to
   the manual load (not just the boot-time one).
4. If `settings.cfg`/AutoSave *still* show the old Bank after that wait,
   that would be strong evidence of a real playback-contention bug (e.g. the
   facade never going idle while the sequencer runs) rather than a capture
   artifact, and worth its own investigation.
5. Separately, repeat a Bank Load with playback **stopped** and check
   whether row 9 (or any other identity row) still gets corrupted, to help
   confirm whether §3.1 is specifically playback-triggered.

## 5. Suggested follow-ups (not yet actioned)

- Investigate §3.1 (HCNAMES Scene-08/root-name cross-contamination) as the
  primary actionable defect from this test.
- Decide the `AUTOSAVE_TRACE_RECORD_COUNT` question (§3.3) — the current
  2,048 "temporary" size is provably insufficient for a whole-Bank sweep.
- Delete `SD_CARD/Bank/old012-c76c/` during routine card cleanup (§3.4).
