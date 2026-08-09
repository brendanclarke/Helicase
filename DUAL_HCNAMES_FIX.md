# DUAL_HCNAMES_FIX

## Observed hardware result

After a boot/test pass, the SD card contained two root entries displayed as
`/.hcnames`. They were renamed to `/.hcnames1` and `/.hcnames2` before copying
the card into this workspace, so the original FAT LFN/SFN entry chains are no
longer available for raw comparison.

The captured files are nevertheless byte-for-byte identical:

- both are 1,063 bytes and contain the required 129 newline-delimited rows;
- their SHA-256 values match;
- their Bank, Scene, Kit, and Instrument identity contents are therefore not
  competing versions of the register.

This is a directory-entry duplication fault, not an HCNAMES content conflict.

## Implemented preventive correction

`filesystem.c` formerly opened every `/.hcnames` reader and writer through
`afatfs_fopen_lfn(..., AFATFS_MATCH_CASE_SENSITIVE, ...)`. That is incorrect
for this firmware-owned singleton in FAT's case-insensitive namespace: an
existing host-created case variant must be preserved as the same register, not
treated as a missing file eligible for creation.

The implementation now defines the documented private
`FS_RESIDENT_NAMES_MATCH_MODE` as `AFATFS_MATCH_CASE_INSENSITIVE` and uses it
for every HCNAMES read, rewrite, Bank-load write, Bank-save write, autosave
ensure read, and recovery read. The change does not allocate RAM, alter the
HCNAMES file format, or change HCNAMES row ownership.

Firmware build passed after this change.

## Remaining root cause candidate: failed open is mistaken for absence

The case-folding correction prevents case-variant duplication, but it cannot
prove why two exact-looking FAT entries were created. The stronger remaining
candidate is the following control-flow ambiguity.

Several HCNAMES paths receive only `op_file == NULL` from the AsyncFATFS open
callback. They currently use that result as "the register is absent":

- Bank Load phase 80 continues with a blank 129-row cache, then later opens
  `/.hcnames` for write after the selected Bank commits.
- `filesystem_residentNames_tick()` update mode bootstraps a fresh register
  from the targeted Scene/Kit/Instrument identity rows.

`NULL` can represent a missing file, but can also result from an interrupted
or otherwise failed open. If a real existing register is not found/opened by
that operation, the subsequent write/create path can allocate another root
entry. The shared filesystem facade prevents concurrent HCNAMES writers, so
concurrent writer ownership is not the leading explanation.

Do not interpret an ordinary failed read as permission to create a register.
That would hide the storage failure and can duplicate a root authority file.

## Prospective remediation

Before any HCNAMES bootstrap write, add one bounded, read-only root-directory
scan that is separate from the failed open attempt.

1. Return to root and enumerate objects with the existing AsyncFATFS finder.
2. Match `/.hcnames` with the same case-insensitive display-name rule used by
   normal open/create operations.
3. Classify the result explicitly:
   - **absent:** allow the existing bootstrap create path;
   - **one or more matching entries:** do not create; reopen/rewrite through
     the existing selected object path, and retain/report duplicate detection;
   - **finder, directory, close, or FAT failure:** finish with
     `FS_STATUS_ERROR`; do not create and do not publish a partial HCNAMES
     cache.
4. Keep the scan state operation-local, reuse the existing finder/handle
   scratch, and close the scan handle through the ordinary callback path. Do
   not add a persistent HCNAMES cache, alternate file, or retry loop.

The Bank Load path needs this most urgently because it runs at boot and can
otherwise write a complete-looking register after an unresolved read failure.
The targeted Scene/Kit/Instrument update bootstrap must use the same helper so
all HCNAMES creation follows one absence proof.

## Diagnostic requirement

The root scan must preserve failure transparency. On a boot Bank Load failure,
the existing `BKHCREAD` / `BKHCWRIT` retained boot-log details remain the coarse
storage boundary; add a concise distinct detail only if it can identify the
new scan's wait-capable phase without rearming `BANKLOAD` or resetting its
ten-second deadline.

At runtime, surface a normal `FS_STATUS_ERROR` rather than silently creating a
new file. If duplicate matching entries are found, log or display a clear
duplicate-register condition before choosing any repair policy. Do not delete,
rename, or merge entries automatically: the renamed `/.hcnames1` and
`/.hcnames2` files are evidence, and automated cleanup needs a separately
approved recovery policy.

## Test sequence

1. Preserve the two captured renamed files outside the active card root.
2. With no canonical `/.hcnames`, boot once and confirm exactly one canonical
   file is created with 129 rows.
3. Reboot repeatedly and perform Bank, Scene, Kit, and Instrument actions
   that update identity. Confirm there remains exactly one case-folded
   HCNAMES entry and that its other rows are preserved.
4. Use a deliberate case-variant host fixture to confirm folded lookup opens
   it rather than creating a second entry.
5. Exercise a controlled failed-read/finder fixture. Confirm it reports an
   error, writes no new HCNAMES entry, and preserves the original file.
6. Retain any recurrence before host renaming so its raw LFN/SFN chains can be
   inspected.

## Implementation record — applied

The prospective remediation is now implemented in `filesystem.c`, with the
public bootstrap contract updated beside its declaration in `filesystem.h`.
Every new or changed branch carries an adjacent comment describing both its
behavior and its preservation rationale.

### Root absence proof

`filesystem_hcnamesProbeBegin()` / `filesystem_hcnamesProbe_tick()` implement
one bounded, asynchronous, read-only root scan. They reuse the facade's normal
`op_file`, `op_object_finder`, and `op_object` scratch; the only added state is
two operation-local bytes for the probe phase and a match count saturated at
two. No HCNAMES rows, object identities, or alternate copy are retained.

The probe case-folds AsyncFATFS `displayName` values with the same ASCII FAT
rule now shared by autosave's no-overwrite scan. It closes the root handle
normally and produces exactly one of these results:

- absent only after a complete root scan and successful close;
- one matching entry, which permits one ordinary folded read retry;
- duplicate (two or more matching entries), which returns a named error and
  makes no card mutation;
- root-open, finder, or close error, which returns a named error and makes no
  card mutation.

The one-match retry is deliberately not a loop. A second NULL callback is an
error (`HNRtry` for a targeted update or `BKHtry` for Bank Load), so neither a
transient failure nor an unreadable existing entry can fall through to create.

### Creation and rewrite call paths

The proof now guards every create-capable HCNAMES path:

1. The boot-only `filesystem_writeResidentNamesBlocking()` writer scans first.
   It may create after absence, or refresh one proven existing entry. Duplicate
   or scan failure reports `HNDup` / `HNPrb` and leaves the card untouched.
2. `filesystem_residentNames_tick()` targeted Scene/Kit/Instrument updates no
   longer treat their initial NULL read as absence. Only an `ABSENT` result
   overlays the completed action's identity rows and opens the bootstrap
   writer. A `PRESENT` result retries the existing reader once; errors and
   duplicates never write.
3. Bank Load phase 80 no longer proceeds immediately with a blank cache after
   a NULL HCNAMES open. Only an `ABSENT` result proceeds to the existing
   first-use path; a unique match is re-read once and every error/duplicate
   terminates Bank Load before phase 83 can open the HCNAMES writer.

Existing successful-read rewrite paths (Bank Load after preload, Bank Save,
and normal targeted updates) remain unchanged: they already operate on a file
that opened successfully and do not mask a failed read as first use.

### Additional boot diagnostic detail

The Bank proof adds retained eight-byte detail codes without calling the
deadline-arming API:

- `BKHCROOT` — opening root for the absence proof after the direct HCNAMES
  read returned NULL;
- `BKHCSCAN` — enumerating or closing that root scan;
- `BKHCDUP ` — the scan found multiple case-folded HCNAMES entries.

The pre-existing `BKHCREAD` and `BKHCWRIT` labels remain the direct read/write
boundaries. The new labels use `filesystem_bootLoggingSetDetail()`, whose
contract only replaces the retained code; it cannot restart the enclosing
ten-second `BANKLOAD` deadline.

### Validation performed

- `git diff --check` passed.
- `make -j4` passed. The image reports `text 371708`, `data 400`, and
  `bss 78476` bytes. The build retains the repository's pre-existing unused
  helper and newlib syscall-stub warnings; it introduced no compile errors.

Hardware and fault-injection tests remain required. In particular, test the
six scenarios in the preceding sequence with the physical card before removing
the captured `/.hcnames1` and `/.hcnames2` evidence.
