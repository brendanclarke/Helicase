# Development Modes Specification

## Authority and scope

This is the authoritative reference for development-mode selection, current
diagnostic outputs, logging ownership, and future instrumentation. It does not
own AutoSave format/writer behavior, product filesystem layout, or low-level
AsyncFATFS semantics; those belong to `AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, and
`ASYNCFATFS_REFERENCE.md` respectively.

This document describes the Session 048 logging baseline plus the current
2026-08-17 Session 051 Scene-follow-up build. Plans and failed working-tree
experiments that mention a unified `/devlog.bin` are not implemented state.

The build has exactly two development modes:

| Flag | Output | Filesystem activity |
| --- | --- | --- |
| `DEV_MODE_DIAGNOSTIC` | Screen/OLED only | Must not add file operations |
| `DEV_MODE_LOGGING` | File records only | May schedule documented log writes |

Do not add a third mode. Trace is logging. A diagnostic that writes or appends
a file belongs to `DEV_MODE_LOGGING`; a diagnostic that renders on the display
belongs to `DEV_MODE_DIAGNOSTIC`.

The current `config.h` baseline is:

```c
#define DEV_MODE_DIAGNOSTIC 0
#define DEV_MODE_LOGGING 1
```

## `DEV_MODE_DIAGNOSTIC`

This mode exists for information that must remain visible on the OLED when an
operation stalls. It may use documented operation/phase observers and bounded
LCD waits needed to make the last coordinate visible.

It must not:

- open, create, append, remove, scan for, or sync a diagnostic file;
- own a file trace ring;
- change filesystem outcomes or retry policy;
- be enabled merely because file logging is enabled.

Display waits perturb timing. Results obtained with this mode enabled must not
be represented as timing-neutral.

## `DEV_MODE_LOGGING`

This mode captures information in RAM and/or persists it without printing to
the screen. It must not call LCD functions or add arbitrary delays. Normal
runtime logging file work is lower priority than settings persistence,
AutoSave, and foreground Load/Save operations.

When the flag is zero:

- AutoSave trace producers are no-op/zero-return stubs;
- logging-only rings, cursors, and deadlines are not allocated;
- no normal diagnostic append is scheduled;
- the boot-timeout diagnostic write path is compiled out.

Any future retained logging allocation requires exact bytes, region, lifetime,
and owner in `SRAM_MANIFEST.md` before implementation.

## Current logging files

The current build has two producer-specific root files. There is no
`DEV_LOG_FILENAME`, `/devlog.bin`, `SaveFixTrace`, or `/savefix.bin` in the
current code.

### `/bootlog.bin`

The boot logger writes one eight-byte printable ASCII operation token after a
pre-audio filesystem operation reaches the configured timeout/failure path.
The path abandons the dirty filesystem state, remounts, opens `/bootlog.bin`
with direct write/create/truncate mode, writes the payload, closes, and flushes
on a bounded best-effort deadline.

Examples include `BANKLOAD`, `KITQUAR `, and `HCNAMES `. The record has no NUL
and no newline.

For every ordinary failure, the payload is exactly the eight-byte token. A
frozen `ASENSURE` timeout appends a 64-byte, eight-record HCPRMS capsule, so
that specific payload is exactly 72 bytes. The raw suffix uses stages `0xE0`
through `0xE7`, all integers are little-endian, and it exists only when
`DEV_MODE_LOGGING` is enabled:

| Stage | Bytes after stage | Meaning |
| --- | --- | --- |
| `E0` | schema, target, ensure phase, facade status, file operation, append phase, flags | `flags bit 7=active`, `bit 0=frozen` |
| `E1` | `bytes_done:u32`, chunk length `u16`, zero-write streak | latest application progress |
| `E2` | last written `u16`, chunk offset `u16`, requested `u16`, target generation | latest `afatfs_fwrite()` call |
| `E3` | file cursor `u32`, logical size `u24` | pre-destroy file coordinates |
| `E4` | search cluster `u32`, sectors/cluster, wrapped flag, flags | flags: full, file available, `bytes_done` at cluster boundary |
| `E5` | append previous cluster `u32`, cursor cluster `u24` | allocator ownership |
| `E6` | dirty, locked, reading, writing, flush, active-cache-index, full | cache summary; cache index `0xff` means none |
| `E7` | SD state, operation, offset `u16`, retry count `u16`, callback-pending | transport state before abort |

The file is forensic evidence only. Failure to create it must not prevent boot
from continuing to the firmware's existing failure handling. Conversely, an
absent file does not prove that no boot failure occurred: the same SD/FAT layer
being diagnosed is also required to persist the record.

The completed read-only decoder is `tools/decode_devlogs.py`; it decodes both
`/bootlog.bin` and `/asavetrc.bin`. A second, low-token decoder,
`tools/devlog_unpack.py`, imports the same lookup tables and produces one
compact line per record instead of prose; it does not change the on-card
format. Update either only if the documented input schema or invocation
changes.

### DEV_LOGGING_IWDG — genuine pre-audio hard-lockup backstop

Every case above still depends on the foreground making it back to a
cooperative check. A raw blocking call that never returns at all -- stuck
inside `SD_init()`'s command loop, the bit-bang SPI byte clocking, or a fixed
`timebase_holdPreAudioMs()` hold -- skips every cooperative poll, so nothing
after it ever runs to notice or log it, and a card pulled after that kind of
freeze shows neither `/bootlog.bin` nor a new `/asavetrc.bin` record (this
was the actual finding behind the 2026-08-21 session's "no bootlog.bin"
question).

`DEV_LOGGING_IWDG` (config.h) closes that gap with the STM32F765's
independent watchdog (IWDG), which resets the MCU on its own free-running
hardware timer regardless of interrupts or software state.

> **This flag defaults to 0 and is UNVERIFIED on hardware.** Its first version
> hung the instrument indefinitely on the boot splash (IWDG init spun on
> `IWDG_SR` before writing the `0xCCCC` key that starts the LSI, which nothing
> else in this firmware enables). `filesystem_devIwdgStart()` is now ordered
> correctly, bounds every handshake against TIM6 milliseconds, and starts
> nothing at all if `LSIRDY` never appears -- so it cannot hang, and it cannot
> arm the dangerous ~512 ms reset-default period. Enable it deliberately for a
> hang-hunting session, not as a standing default, and re-audit feed coverage
> first for any long-running blocking operation. Full analysis:
> `SCENE_LOAD_PAT_RESTORE.md`.

`filesystem.c` starts it once per boot (`filesystem_devIwdgBootCheck()`, called
from main.c immediately after `filesystem_bootLoggingBegin()`) and feeds it
from **both** foreground pumps -- `filesystem_tick()` and
`filesystem_blockPoll()` -- each reachable only from ordinary foreground code,
never an ISR. The second feed is mandatory, not defensive: the modal sample
install runs entirely through `filesystem_blockPoll()` (the blocking helpers
deliberately bypass `filesystem_tick()`) and can exceed the ~32.8 s period
while erasing six 256 KB sectors and streaming megabytes over bit-bang SPI.
Without it, that operation would be reset mid `sampleFlash` erase/program,
risking sample-FLASH corruption rather than a clean reboot.

Two distinct hangs are covered:

- A call that never returns: `filesystem_tick()` is simply never called
  again, feeding stops, and the IWDG's native ~32.8s period (max prescaler,
  max 12-bit reload, nominal 32kHz LSI) resets the MCU.
- A foreground loop that keeps calling `filesystem_tick()` forever without
  ever finishing boot (e.g. a state-machine retry that never reaches DONE and
  never trips its own `BOOT_FILESYSTEM_TIMEOUT_MS` deadline): feeding is
  deliberately stopped once `DEV_LOGGING_IWDG_EXPIRE` (2 minutes, measured
  against the free-running 32-bit `systick_ticks` rather than the wrapping
  16-bit `time_sysTick`) elapses since `filesystem_bootLoggingBegin()` without
  reaching `filesystem_bootLoggingEnd()`.

Once started for a boot the IWDG cannot be stopped in software, so it remains
armed for the rest of that session, including all of runtime after boot
completes -- it becomes a full-runtime hang backstop, not only a boot one.
It owns no NVIC/interrupt configuration: the IWDG has no interrupt line on
this part, only the hardware reset, so enabling it does not touch vector
priorities or any existing ISR.

Because the IWDG has no early-warning interrupt on this part, it cannot write
`/bootlog.bin` before it resets. Instead, a 12-byte capsule (`magic` +
mirrored `fs_boot_logging_code`) lives in the `.devwdg_noinit` linker section
(STM32F765VIHx_FLASH.ld, previously-unmapped SRAM2), explicitly excluded from
the startup zero-fill loop so it survives an IWDG-caused warm reset. On the
next boot, `filesystem_devIwdgBootCheck()` checks RCC_CSR's `IWDGRSTF` flag;
if it is set and the capsule's magic is still valid, the capsule's retained
code is copied into `fs_boot_logging_code` and written to `/bootlog.bin`
through the existing `filesystem_writeBootFailureLogBlocking()` path -- the
same 8-byte token format as an ordinary cooperative timeout, no new on-card
schema. `magic` alone is never trusted as proof of a watchdog reset; RCC_CSR's
hardware flag is the actual gate, and is cleared (`RMVF`) every boot after
being read so it cannot misattribute a later, unrelated reset. RAM allocation:
12 of a 32-byte approved ceiling (linker `ASSERT` enforces the ceiling),
DEV_MODE_LOGGING-and-DEV_LOGGING_IWDG-gated only, filesystem.c.

### `/asavetrc.bin`

`AutosaveTrace.c` owns a bounded SRAM ring when logging is enabled. The normal
default is 64 records (512 bytes); the current approved diagnostic build sets
`AUTOSAVE_TRACE_RECORD_COUNT` to 2,048 records (16,384 bytes). Producers append
RAM records; `filesystem.c` owns the AsyncFATFS append/close/flush operation.
The exact logging-only allocation is recorded in `SRAM_MANIFEST.md`.

Each record is eight bytes:

```text
stage:u8, flags:u8, tick16:u16, value:u32
```

Integer fields are little-endian. Stage bytes are uppercase
`D I J N L R W F G B S A V M C P T X O E` and their meanings/values are owned
by `AutosaveTrace.h`. `X`, `O`, and `E` were added in Session 054 while
chasing the recursive-delete `ScnS05` defect (see
`knowledge_files/log_archive/054_SESSION_HANDOFF_LOG.md`); a fourth stage,
`Y` (`SCAN_PARENT_DIAG`), was added and then fully retired in the same
session once its root cause was fixed outright — its layout stays documented
below only so any `asavetrc.bin` already captured during that window still
decodes; it has no live producer.

`X` (`PHASE_STALL`) is a purely diagnostic, edge-triggered "this cooperative
state machine's phase stopped advancing" observer
(`filesystem_pollPhaseStall()`), used at three sites: the delete-slot
resolver (site 0, 50,000-poll threshold), Bank Save's own entry/metadata
phases (site 1, 20,000 polls), and the runtime AutoSave parameter drain
(site 2, 30,000 polls — the one site where a stall also forces a real
`FS_STATUS_ERROR` completion instead of only observing, since a wedged drain
previously had no bounded escape at all). `flags` bits 0-2 select the site;
`value` packs the stalled phase, the numbered slot, and (site 0 only, when
inside native delete) the asyncfatfs subphase.

`O` (`SAVE_LIFECYCLE`) is an ordered per-save-type timeline —
`REQUEST`/`DELETE_RESULT`/`CREATE_RESULT`/`SOURCE_STAGED`/`FINISH` — for Kit,
Scene, Bank, and Instrument Save, plus three Menu-side `REQUEST`-tagged
branch witnesses for `menu_requestKitEntryNames()`'s cache-domain decision
(these three reuse the CRC16 bit range as a local branch tag 1/2/3, not a
CRC — decode with that call site in mind). Instrument's `CREATE_RESULT`
additionally packs a raw (unfinished, not `~crc32c`-complemented) CRC32C
content fingerprint, comparable only against another value produced the same
way.

`E` (`OPERATION_ERROR`) is a universal backstop: both of filesystem.c's
shared terminal-completion functions (`filesystem_complete()` and
`filesystem_completeLibraryIndexRebuild()`) emit one record whenever an
operation ends in `FS_STATUS_ERROR`, regardless of which specific failure
branch was hit — by construction, not by having hand-instrumented every
branch. `value` packs the failing `current_op`/`op_phase`/`op_slot`; flag bit
0 reports whether the more specific delete-slot failure-reason field was
also set (cross-reference the paired `'O'` `DELETE_RESULT` record in that
case); flag bit 1 distinguishes the two hook sites.

**Known diagnostics gap (Session 055):** `N` (`INSTRUMENT_ENTRY`) is only
emitted while `menu_instrumentLoadActive` is true, so every refusal/entry
record on the *top-level* Kit or Scene Load/Save row is silently suppressed
— an absent `N` record does not prove no request was posted. Worse, the
Scene entry path (`menu_requestSceneEntryName()`) has no trace producer at
all. Both cost real investigation time chasing the Session 055 Load-menu
freeze (its absence looked like "nothing happened" when in fact the request
was being posted and refused every pass). A top-level equivalent of the `N`
entry trace, and a trace producer for the Scene entry path, are recommended
before the next Load/Save-family investigation; not yet implemented.

`D` is one accepted
payload-bit OR. `I` is one whole-Instrument marker outcome: flags report valid
payload-map base, live tracking, and whether all requested bytes reached the
dirty funnel; value packs Scene, slot, expected byte count, and accepted byte
count. It exists because a whole Instrument can produce enough `D` records to
wrap the small ring before a card is copied. Records are acknowledged only
after the file operation succeeds; overflow is reported by the ring's bounded
dropped-record counter.

`J` is the committed-Instrument pre-marker witness. It records the destination
Scene/slot/type and flags for “whole marker requested” and “whole marker
called.” It is emitted immediately after the SceneData assignment and follows
the marker's `I` record, allowing a failed provenance gate to be separated from
an internal marker rejection without changing persistence behavior.

`N` is a nested-Instrument-entry timing milestone. Its low flag bits select
entry, HCNAMES read/flush, temporary snapshot, or typed-index request and
completion; flag bit 7 reports a rejected request or failed callback. Its value
packs the Scene, slot, and selected type. The paired ticks expose the exact SD
operation behind a delayed blank `kit` name without changing Menu behavior or
adding any state.

`L` is the terminal whole-load marker. Flag bit 0 reports mutation tracking;
the value packs kind in bits 0..1 (`0` Kit, `1` Scene) and the destination
Scene in bits 2..5. It follows all subordinate `D`/`I` records for that scope.

`R` is the root Scene Load completion witness: it is emitted at callback entry;
flag bit 0 reports that filesystem status was `DONE`, and the value is the
destination Scene mask. `W` records the intentional Load/Save-page suppression
of an armed dirty writer; flag bit 0 reports dirty work and the value is the
debounce deadline. `F` records trace-flush suppression: bit 0 is the
command-active gate with pending-record count in the value, and bit 1 is an
append error. `G` reports a changed trace-ring dropped count in its value.

`B` is the Session 052 Bank present-mask witness. With flags bit 0 clear it
records the resident mask at Bank Load metadata commit and packs the effective
selected-child load mask in value bits 0..15. With bit 0 set it records the
resident mask at the AutoSave drain's first present-mask byte and packs the
payload offset (10) in bits 0..15. It is diagnostic-only and uses no new RAM.

The 2026-08-16 root-Scene hardware fixture is the reference example for the
terminal publication chain: Scene 15 loaded root Scene 024 and produced
`R flags=0x01 value=0x00008000`, Kit `L=0x3c`, Scene `L=0x3d`, one
command-active `F`, one page-suppression `W`, then `A/V/M/C/P/T`; `P` reported
generation 6. The final root-index callback must acknowledge its captured
terminal result before Menu teardown, otherwise these RAM records and the
AutoSave writer remain blocked behind a non-idle filesystem facade.

## Current duplicate-name limitation

The two current log writers do not implement a complete duplicate-safe
singleton proof. In particular, AsyncFATFS append/write modes include CREATE,
so a direct open after an ambiguous lookup or filesystem failure can create a
second FAT directory entry with the same visible name. Duplicate
`bootlog.bin` and `asavetrc.bin` entries have been observed during hardware
testing; the same error pattern previously affected `.hcnames`.

The rollback intentionally preserves this known limitation rather than
claiming the failed replacement is active. Until a separately reviewed repair
lands:

- preserve duplicate entries as evidence before a host filesystem expunges
  them;
- do not interpret a failed open callback as proof that the name is absent;
- do not create a numbered or alternate fallback filename;
- do not silently select, merge, delete, or overwrite one duplicate;
- do not report logger success when scan/open/write/close/flush failed.

A correct future singleton append must complete and successfully close a
case-insensitive root scan, distinguish zero/one/multiple matching entries,
open a unique existing file without CREATE, seek to EOF, and create the
canonical name only after zero matches are proven. Multiple matches and every
scan/open/type error must remain visible failures. That design must be proven
without making the diagnostic path capable of masking the original failure.

## Failed unified-log experiment: do not reapply

An uncommitted Session 046 experiment attempted to consolidate all logging to
`/devlog.bin` and add duplicate-safe append handling in the same change. The
hardware result was a boot timeout, no `devlog.bin`, and a partial 32,768-byte
`/.hcprms2`. The repository was reset to `c9807fa`.

Do not copy that patch or describe unified DEVLOG as current behavior. If log
consolidation is reconsidered, it must be separated from AutoSave CRC changes,
settings persistence, Bank Save pacing, Bank Load behavior, and `.hcprms`
boot-lock diagnostics. First reproduce and localize the `.hcprms` failure with
bounded milestones that can survive or be displayed when the SD path cannot
write. Any older diagnosis plan that names `/devlog.bin` as current authority
is stale and must not be implemented as written.

## Scheduling and failure rules

- Runtime producers copy fixed records to bounded RAM only; they do no FAT or
  SD work.
- `filesystem.c` remains the sole owner of diagnostic file handles.
- Normal trace flushing starts only while the facade is idle and yields to
  settings persistence and foreground work. It is deferred while an accepted
  Load/Save command is active; after that command has finalized it may append
  on a Load/Save page, whereas the AutoSave writer remains page-suppressed
  until exit.
- Logging must not add a blind sleep, busy wait, LCD wait, or arbitrary pacing
  interval to the operation being observed.
- A trace cursor advances only after write, close, and flush succeed.
- Function failures in Load/Save or AutoSave paths must not be converted into
  success to keep logging moving.
- The exceptional boot logger runs only after the observed boot operation has
  already failed; logging failure must not replace or hide that original
  operation and substep.
- Logging to the same SD card cannot be the sole evidence channel for an SD
  boot lock. Pair file logging with `DEV_MODE_DIAGNOSTIC` screen coordinates or
  another separately approved non-SD witness when diagnosing that class.

## Adding or supplementing diagnostics

Before adding instrumentation:

1. choose `DEV_MODE_DIAGNOSTIC` for screen output or `DEV_MODE_LOGGING` for
   file output; do not add another mode;
2. identify the exact unresolved function/substep and evidence the hook will
   provide;
3. keep producer work bounded and preserve the original return/error value;
4. identify any RAM growth and obtain SRAM sign-off;
5. route all file ownership through `filesystem.c`;
6. define when a record becomes durable and when its RAM slot is acknowledged;
7. make duplicate/ambiguous-name failures explicit rather than creating a
   fallback;
8. build with logging both on and off and prove the off image contains no
   logging-only storage or diagnostic file activity.

Do not log every main-loop or filesystem tick. Record transitions, bounded
milestones, and errors so observing SD pressure does not become the pressure.
Do not combine a diagnostic change with a behavioral fix unless the evidence
already identifies that fix and the two cannot be separated safely.

## Validation

For the current baseline:

- preserve raw captures before inspection;
- parse `/bootlog.bin` as one eight-byte token, with the HCPRMS suffix only
  when that token is `ASENSURE` and the file is exactly 72 bytes;
- parse `/asavetrc.bin` only in eight-byte boundaries using the layout above;
- treat a non-multiple-of-eight trace size as partial/corrupt evidence;
- test logging-on and logging-off builds;
- confirm the logging-off image has no AutoSave trace ring symbol and performs
  no diagnostic file write;
- test absent, unique, case-variant, duplicate, scan-failure, open-failure, and
  power-interruption cases before declaring any future singleton repair done.

`tools/decode_devlogs.py` is the decoder for ordinary boot tokens, the
conditional `ASENSURE` capsule, and `/asavetrc.bin` records. It does not
decode AutoSave record payloads or replace the later, intentionally deferred
general development-log converter.
