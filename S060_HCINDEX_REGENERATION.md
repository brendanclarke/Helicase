# Session 060 `.hcindex` boot regeneration survey

Date: 2026-08-31

Status: read-only code/card survey. No production code changed.

## Finding

The confirmed reason one typed-index failure leaves several Instrument
directories without `.hcindex` is fail-fast boot control flow combined with a
discarded result:

- `filesystem_createBootIndexBlocking()` first repairs all Instrument names,
  then processes types serially.
- Each type is scanned and its index is written before the next type starts.
- Any ordinary repair, scan, start, or index-write error immediately returns
  zero and skips every remaining type.
- `main.c` casts that result to `void`. It reacts only to the separate boot
  timeout latch, so an ordinary error is silent and boot continues.

This fully explains why missing typed indexes can survive repeated boot and why
later types are not attempted. The copied card does not preserve enough boot
diagnostic state to prove the exact lower-level Snare failure.

## Current evidence

The registry order is fixed in
`Core/DSP/Instruments/InstrumentManager.c:32-49`:

1. Drum
2. Snare
3. Cymbal
4. HiHat

The supplied card captures progressed as follows:

| Capture | Drum | Snare | Cymbal | HiHat |
| --- | --- | --- | --- | --- |
| `SD_CARD_AFAT_RESULT/` | absent | absent | absent | absent |
| `SD_CARD_LOAD_SAVE_TEST_RESULT/` | present, 100 rows | absent | absent | absent |
| `SD_CARD_AFAT_PHASE_2_RESULT/` | present, valid 99 rows | absent | absent | absent |

The earlier 100-row Drum index included the reserved temporary file. The
current 99-row index exactly matches the 99 visible Drum Instruments and
excludes `.hctmp.drm`. The Phase-Two hardware test performed Bank operations,
which do not rebuild a root typed-Instrument index. This is strong evidence
that the current boot completed the Drum scan/write and then stopped while
processing Snare. It is not timestamp evidence; the device has no RTC.

Snare, Cymbal, and HiHat each contain 31 visible files plus their reserved
`.hctmp.<ext>` file. Their visible names are canonical, match the directory's
registered extension, and expose no obvious malformed filename that should
break the scan.

## Failure path in code

### Boot caller

`main.c:711-729` calls the typed refresh. At lines 727-729 it does:

```c
(void)filesystem_createBootIndexBlocking();
if (filesystem_bootLoggingTimedOut())
    goto boot_filesystem_timeout;
```

The helper's ordinary zero result is ignored. Unlike the Kit index call at
`main.c:616-621`, it does not enter `boot_filesystem_failure`.

### Blocking helper

`Core/Hardware/SD/filesystem.c:22837-22918` implements the typed refresh:

- lines 22860-22861 abort if the all-type name repair fails;
- lines 22863-22877 select one registry type and one shared cache;
- lines 22878-22894 scan only that type and abort on any non-DONE result;
- lines 22896-22912 write that type's `.hcindex` and abort on any non-DONE
  result; and
- only lines 22915-22918 represent completion of all four types.

There is no continue-on-error path, accumulated failure result, per-type retry,
or durable record of the failed type.

### Snare scan candidate

`filesystem_scanInstruments_tick()` at
`Core/Hardware/SD/filesystem.c:18754-18970` opens
`Instrument/Snare`, iterates its objects, closes it, and returns to root. A
physical iterator failure, fatal FAT state, or failed parent traversal produces
`FS_STATUS_ERROR`. Any of those would stop the boot helper before the Snare
writer starts.

The logical card copy shows a readable directory and 31 ordinary `.snr` files,
so there is no static filename/content reason to prefer this explanation. It
remains possible because the copy does not preserve a transient device I/O
failure.

### Snare create candidate

`filesystem_createBootIndex_tick()` at
`Core/Hardware/SD/filesystem.c:3930-4207` enters the selected directory and at
lines 4093-4099 requests:

```c
afatfs_fopen_lfn(".hcindex", "w", ...)
```

An existing Drum index follows the open/truncate path. Snare is the first type
that must create a previously absent `.hcindex`, including its VFAT LFN/SFN
directory-entry run. A NULL completion at writer phase 10
(`filesystem.c:4102-4108`) becomes the ordinary error which stops the outer
loop.

This absent-file create path is therefore the leading underlying suspect. It
is not proven. A clean logical directory copy cannot distinguish it from a
preceding Snare scan failure.

The visible Snare population does not suggest a directory-capacity problem.
With ordinary 8.3 entries, dot entries, and the reserved temporary LFN/SFN run,
the two-entry `.hcindex` run should fit within the current directory sector.
The raw FAT entry layout was not captured, so this estimate cannot prove the
actual terminator position. Nothing here specifically implicates Phase Two's
new-directory first-sector initialization.

## Why the existing logs do not decide it

- The device has no RTC, so FAT timestamps cannot identify which boot wrote a
  file.
- `bootlog.bin` is written only through the boot failure/timeout path. The
  ignored ordinary helper result never takes that path.
- `filesystem_complete()` records operation errors in the AutoSave trace, but
  the supplied `asavetrc.bin` spans older activity, has no reliable current-boot
  delimiter, and reports 44,348 dropped records during the later Bank load/save
  workload.
- The logical host copy does not contain raw directory sectors, LFN/SFN entry
  positions, cache state, or the runtime phase at failure.

Consequently, the exact first failure is localized to the Snare one-type scan
or Snare `.hcindex` create/write chain, with creation the stronger candidate;
it cannot be asserted as proven from this capture.

## Fastest discriminating checks

1. Enter Snare Instrument Load once. Missing-index recovery uses the same
   one-type scan and `.hcindex` writer. Copy the card immediately afterward.
   A valid 31-row Snare index proves the physical scan/create path can work at
   runtime and points toward a boot-only sequencing or transient failure.
2. If Snare recovery fails, preserve the card and trace before a Bank load or
   other large dirty burst. The terminal operation and phase can then separate
   `SCAN_INSTRUMENTS` from `CREATE_BOOT_INDEX`.
3. Repeat for Cymbal and HiHat only after Snare. This determines whether the
   later missing files are merely skipped consequences or independent create
   failures.
4. Reboot after all four files exist and copy the card without other storage
   work. All four indexes should remain valid and exactly match their visible
   typed files.

## Likely repair direction

Do not treat the three missing files as three independent boot failures until
Snare is isolated. A complete fix should:

- retain and act on `filesystem_createBootIndexBlocking()`'s result in
  `main.c`;
- report the failed registry type and whether repair, scan, or write failed;
- consider attempting later registry types while accumulating an overall
  failure, so one bad type cannot suppress unrelated indexes; and
- change AsyncFATFS or the scan/writer only after the failing Snare phase is
  captured.

Runtime selected-type recovery remains a valid safety net, but it does not
satisfy the boot contract that every existing typed Instrument directory is
refreshed.
