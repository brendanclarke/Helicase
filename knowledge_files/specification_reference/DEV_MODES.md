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
`/bootlog.bin` and `/asavetrc.bin`. Update this reference only if its
documented input schema or invocation changes.

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
`D I J N L R W F G S A V M C P T` and their meanings/values are owned by
`AutosaveTrace.h`. `D` is one accepted
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
