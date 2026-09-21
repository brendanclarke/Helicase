# S069 AutoSave CPU and same-boot re-enable assessment

## Scope and verdict

This assessment correlates the source used by
`SD_CARD_SLACK_REACTIVE_TEST_OUT/` with the files recovered from that card.
The captured `LXRV2_lxr02.img` has Git blob id
`7de91985d0401335f81013aa5e5c54d41a49866c`, exactly matching the build image
stored at commits `50b88ea` and `a174fb6`. The relevant source is identical in
those two revisions. Current HEAD `1f7a772` is later than the capture and
contains the targeted S069 Pass-1 dirty-count fix, so current line numbers
must not be used to infer what the captured firmware executed.

| Reported symptom | Assessment |
|---|---|
| AutoSave ON costs another 4–6 CPU-widget points | **Confirmed defect.** The clean scalar dirty test scans 3,856 volatile bytes on every idle filesystem scheduler pass. AutoSave OFF returns before that test. |
| AutoSave does not work after OFF -> ON in the same boot | **Not supported; contradicted by the supplied card.** The re-enable path ran and the late step edits are present in the durable Pattern AutoSave files. The absence of a renewed CPU increase is a consequence of the first defect, not evidence that ON failed. A Pattern save taking more than ten seconds remains possible because the captured firmware provides no ten-second completion bound, but that is latency, not a dead re-enable path. |

## 1. Confirmed defect: clean AutoSave performs an unbounded-rate full-mask scan

### Exact cause

The scalar AutoSave mask is 3,856 bytes
(`a174fb6:Core/Bank/Scene/Autosave.h:54-60`). In the captured firmware,
`autosave_maskHasDirty()` loops from byte 0 through byte 3,855 and reads each
byte from the volatile mask until it finds a set bit
(`a174fb6:Core/Bank/Scene/Autosave.c:1934-1951`). A clean mask is therefore the
worst case: every call reads all 3,856 bytes.

`filesystem_tick()` is called once per foreground loop (`main.c:1305`). The
measured foreground rate is approximately 7,600 calls per second, as also
recorded in the filesystem source
(`a174fb6:Core/Hardware/SD/filesystem.c:24887-24890`). Whenever the facade is
idle, the scalar scheduler runs and its clean-state gate calls
`autosave_maskHasDirty()`
(`a174fb6:Core/Hardware/SD/filesystem.c:24194-24204,
24781-24819`). The steady clean cost is therefore approximately:

```text
3,856 mask bytes/call * 7,600 calls/second
= 29,305,600 volatile byte inspections/second
```

The approximately 7,600-per-second figure is the measured speed of an
unconstrained foreground loop, **not** a configured AutoSave polling rate.
`FS_IDLE_POLL_MS = 5` limits only idle `afatfs_poll()` calls; it does not
rate-limit the later AutoSave scheduler
(`Core/Hardware/SD/filesystem.c:1891,24801-24815,24860-24861`). The dirty-mask
test runs before the writer considers its five-second due time. The five
seconds is a delay *after dirty work is noticed*, not an interval between
dirty-mask checks (`a174fb6:Core/Hardware/SD/filesystem.c:24201-24218`;
`config.h:373-397`).

When AutoSave is OFF, the same scheduler returns at its policy gate before
reaching the dirty predicate
(`a174fb6:Core/Hardware/SD/filesystem.c:24103-24128`). That single branch
difference precisely explains the repeatable 4–6-point ON/OFF delta. The
steady delta is CPU spent polling an empty SRAM mask; it is not SD traffic,
CRC work, or a periodic save.

### Why the displayed value falls slowly

The menu's “CPU use” value is the percentage of DWT-accounted time for which
the two-slot audio ready queue has at least one free slot. It is an audio
render/refill-pressure measurement, not a general-purpose MCU utilization
counter (`Core/Hardware/AudioCodecManager.c:248-262,308-348`). Menu samples it
once every 500 ms and displays a ten-sample moving average
(`Core/Menu/menu.c:1395-1402,9972-9993`). The display therefore has an
approximately five-second memory. A gradual 84 -> 78–80 decline after turning
AutoSave OFF is expected, and leaving/re-entering Settings does not reset that
sample ring.

### Costs that are not the ON/OFF differential

The captured firmware also invokes `autosave_maskHasDirty()` from the 500 Hz
Pattern service's repair-budget sampler and recounts 2,048 Pattern bitmap bits
at the idle tick tail
(`a174fb6:Core/Bank/Scene/Pattern/PatternStackService.c:397-409,
487-497,1554-1557`). Those operations add roughly 1.93 million mask-byte
reads and 1.024 million bitmap tests per second respectively, but they run
with AutoSave both ON and OFF. They can contribute to the 78–80 baseline; they
cannot cause the observed 4–6-point switch-dependent change.

### Why CPU does not immediately rise again after re-enable

Successful runtime re-enable deliberately calls
`autosave_markResidentBankDirty()`
(`a174fb6:Core/Hardware/SD/filesystem.c:24084-24091`). That seeds a large
nonempty mask. The defective predicate then returns at its first dirty byte
instead of scanning all 3,856 bytes. Consequently the expensive condition is
**AutoSave ON and scalar mask clean**, not merely “the menu value says ON.”
The low CPU reading after ON is therefore consistent with successful
re-enable and pending/draining work. Once the scalar mask became completely
clean, the pre-fix firmware would resume the expensive full scan.

### Polling-rate alternative and status of the actual fix

If the captured, pre-Pass-1 implementation had to retain the 3,856-byte full
scan, a **50 ms (20 Hz)** check of the *clean, unarmed* scalar mask would be a
reasonable targeted throttle. It would reduce this call site from about 7,600
to 20 full scans per second: 77,120 rather than 29,305,600 inspected bytes per
second, about 380 times less work. A newly dirty bit would be noticed at most
50 ms later, so the normal five-second first-writer delay would begin at most
50 ms later (a 1% timing difference). A 100 ms/10 Hz check would also be
plausible, but 50 ms is the more conservative responsiveness choice. Checking
only every five seconds would be inappropriate: that could add almost five
seconds *before* starting the existing five-second delay.

Only clean, unarmed dirty discovery should be throttled in such a legacy
design. The foreground filesystem tick, OFF/ON setup, recovery, already-armed
deadline checks, Load/Save suppression/resumption, admitted transactions, and
250 ms dirty-backlog continuations must keep their normal responsiveness.
Rate-limiting only the scalar scheduler would also leave the separate 500 Hz
Pattern-service call to the same old predicate; it is not a complete removal
of clean-state scanning.

**No such throttle is needed in current HEAD.** S069 Pass 1 instead added an
exact `autosave_dirty_count`, maintained at the mask mutation boundaries, and
made `autosave_maskHasDirty()` an O(1) count comparison
(`Core/Bank/Scene/Autosave.c:101-112,2010-2055`). This removes the old full
scan from both the scalar scheduler and the Pattern-service caller without
adding detection latency. The implementation postdates the supplied SD image.

`DEV_MODE_LOGGING` is currently enabled (`config.h:88`). In that configuration,
every 1,000th predicate call performs a diagnostic full-mask popcount and
compares it with the maintained count
(`Core/Bank/Scene/Autosave.c:2022-2055`). At roughly 7,600 filesystem calls
plus up to 500 Pattern-service calls per second, this is approximately eight
full audits per second while idle and enabled, versus roughly 7,600 full
scans per second in the captured firmware. Production builds with development
logging disabled omit the audit entirely. Thus the *sustained* 4–6-point
clean-scan defect is fixed in the current source; an occasional DEV audit is
not the same workload. The audit does briefly mask interrupts while counting,
so its individual peak cost is distinct from the old sustained cost. Pass 1
was hardware-accepted without operation problems
(`S069_ATS_PAT_BOUNDED_CLAUDE.md:703-718`), but a quantitative ON/OFF
CPU-widget measurement of the current build is still needed to establish the
exact remaining difference, if any.

## 2. Same-boot OFF -> ON did re-enable AutoSave

### The source path is connected

In the captured revision, a changed AutoSave menu cell calls
`filesystem_setAutosaveEnabled()` immediately after updating the setting
(`a174fb6:Core/Menu/menu.c:3024-3044`). The OFF -> ON transition clears the
discard/setup-failure state and queues asynchronous setup when runtime and a
resident Bank are available
(`a174fb6:Core/Hardware/SD/filesystem.c:23445-23493`). The idle scheduler then
admits `FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES`
(`a174fb6:Core/Hardware/SD/filesystem.c:24160-24180`). On success, its callback
sets boot-ready/recovery state, enables mutation tracking, and marks the whole
resident Bank dirty
(`a174fb6:Core/Hardware/SD/filesystem.c:24048-24091`). There is no missing menu
call, stale same-value early return, or playback gate in that successful path.

A step toggle then flips the live Pattern trigger bit and calls the common
Pattern dirty funnel
(`a174fb6:Core/Bank/Scene/Pattern/PatternData.c:846-861`). Thus post-enable
step edits do reach Pattern AutoSave ownership.

### The card proves that the reported step edits were saved

The late `K` records were initially easy to misread. Their defined layout is:

```text
bits  0..7   track
bits  8..15  absolute step
bits 16..23  Pattern/Scene
bit      24  pre-toggle trigger state
```

This layout is defined in `Core/Bank/Scene/AutosaveTrace.h:170-180,415-422`
and packed immediately before `pat_toggleStep()` in
`Core/Hardware/frontPanel/buttonHandler.c:676-705`. The low byte is the track,
not the Scene.

Applying that layout to the captured trace gives the following direct proof:

- The 44 late `K` events between records `#074794` and `#074956` address
  Pattern/Scene 5, tracks 0, 5, and 6. They cover 43 unique coordinates; step
  122 is toggled twice and correctly ends OFF.
- For every one of those 43 final coordinates, the trigger bit in
  `/.pat05a` generation 22 equals the post-toggle state. The address is read at
  PAT4 offset `160 + 2 * (track * 128 + step)`.
- `/.pat05a` generation 22 and `/.pat05b` generation 21 are both CRC32C-valid,
  exact-size 10,656-byte PAT4 records. Their resident payloads are identical;
  only the generation/CRC header fields differ.
- Physical `.hcnames` line 136, the Pattern row for Scene 5, is
  `Empty<TAB>@<TAB>R`. Per the Pattern AutoSave contract, `@|R` is published
  only after a successful durable PAT4 completion with no post-snapshot edit
  left pending.
- `settings.cfg` ends with `autosave=1`.
- Both scalar records are also exact-size, CRC32C-valid, and clean:
  `/.hcprms1` is generation 85 and `/.hcprms2` is generation 86. The end of
  `asavetrc.bin` publishes generation 86 and records terminal `DONE`. There is
  no AutoSave operation error (`E`) or phase-stall (`X`) record.

These facts rule out a permanent same-boot rearm failure, a failed setup latch,
and loss of the recorded step edits in this run. The step changes were
durably AutoSaved.

### What can still look like a failure at ten seconds

The capture cannot establish the exact Pattern publication time. The tested
firmware emits no Pattern-drain lifecycle record, and the card's FAT timestamps
are not usable for this measurement. Therefore it cannot prove or disprove
that the PAT4 publication occurred before a particular ten-second observation
point.

The pre-Pass-1 source also provides no ten-second Pattern completion guarantee:

- successful re-enable marks every present Scene's scalar and Pattern state,
  not only the next edit
  (`a174fb6:Core/Bank/Scene/Autosave.c:1885-1907`);
- settings and both diagnostic trace writers are admitted before scalar
  AutoSave, and Pattern AutoSave is admitted after all of them
  (`a174fb6:Core/Hardware/SD/filesystem.c:24772-24824`);
- Pattern scheduling selects the lowest dirty Scene bit and has neither a
  rotating fairness cursor nor a maximum-latency timer
  (`a174fb6:Core/Hardware/SD/filesystem.c:24290-24364`).

That design can make a newly edited Pattern wait behind re-enable work and can
make a ten-second spot check appear unsuccessful. It is a latency and
observability weakness, not evidence that the ON transition failed. The
supplied capture demonstrates eventual successful publication of the exact
steps in question.

There is a separate source-level failure mode: if the runtime ensure operation
actually returns an error, `filesystem_autosaveSetupCompleted()` sets
`fs_autosave_setup_failed`, leaves tracking disabled, and does not retry until
another policy/lifecycle transition
(`a174fb6:Core/Hardware/SD/filesystem.c:24064-24082`). The UI does not expose
that latch. It did **not** occur in this capture: successful scalar and Pattern
publications, the final `@|R`, and the absence of an AutoSave operation-error
record exclude it.

## Targeted conclusion

1. The 4–6-point CPU defect is the 3,856-byte clean dirty-mask scan executed at
   approximately 7,600 foreground polls per second: about 29.3 million
   volatile byte inspections per second. AutoSave OFF bypasses it. Current
   Pass-1 source replaces that repeated scan with an O(1) count; only the
   occasional development-logging audit still scans the full mask.
2. There is no demonstrated same-boot re-enable failure to fix in the supplied
   run. Re-enable succeeded and the recorded steps are in the durable PAT4
   winner. The unchanged CPU reading after ON follows from early exit on the
   newly dirty full-Bank mask. If “saved within ten seconds” is a requirement,
   the separate defect is the captured firmware's lack of a bounded Pattern
   admission/completion latency and corresponding trace evidence—not a broken
   menu rearm path.
