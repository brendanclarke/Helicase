# Autosave File Pair — creation baseline and validation format

## Current boundary

After normal pre-audio filesystem work has loaded a real Bank, boot ensures
that the two hidden root registers exist:

```text
/.hcprms1
/.hcprms2
```

No resident Bank means autosave is inactive and this operation does no I/O.
For an existing target, boot proves it exists by an LFN-aware root scan and
never opens it for write. This pass does not read/apply an autosave record,
set mask bits, write parameters/effects, track dirty state, or update either
record after creation.

## Fixed v1 layout: 23,248 bytes

The former two-byte Bank Slot field is removed. A dedicated 64-byte control
header occupies the top of each file; the mask still describes only the
Bank/Scene payload and never describes header metadata.

| Absolute range | Size | Contents |
| --- | ---: | --- |
| `0..3` | 4 | ASCII magic `HCPR` |
| `4` | 1 | Format version `1` |
| `5` | 1 | Commit marker `0xA5` when valid |
| `6..7` | 2 | Reserved, zero |
| `8..11` | 4 | Little-endian unsigned generation |
| `12..15` | 4 | Little-endian CRC32C |
| `16..63` | 48 | Reserved, zero |
| `64..2639` | 2,576 | Replacement mask, initially all zero |
| `2640..2767` | 128 | Bank section: Bank name in its first 8 bytes, then zero padding |
| `2768..23247` | 20,480 | Sixteen 1,280-byte Scene sections |

The mask has `2,576 × 8 = 20,608` bits, exactly one for every payload byte
from the Bank section through the last Scene byte. It deliberately excludes
the 64-byte control header.

Each Scene still has the requested 128-byte Scene section, a 256-byte empty
Effects section, and an 896-byte Kit section. The Kit contains its 128-byte
name/parameter area and six 128-byte Instrument records. An Instrument keeps
three zero type bytes, an 8-byte zero-padded name, 72 zero parameter bytes,
and zero padding.

HCNAMES ownership remains unchanged: Bank is the BankData display name; Scene
rows are `1..16`, Kit rows `17..32`, and Instrument rows `33..128`. Names are
trimmed only at trailing spaces/NULs, then zero-padded; embedded printable
spaces remain unchanged.

## Validity, corruption, and ping-pong order

CRC32C uses the reflected Castagnoli polynomial. It covers every one of the
23,248 bytes, with header bytes `12..15` treated as zero while calculating.
Thus it covers the magic/version/commit marker/generation, the full mask,
every name, every parameter byte, and all padding. It is intended to detect
accidental SD corruption and torn writes; it is not a cryptographic hash.

A future reader accepts a record only when all of these are true:

1. File size is exactly 23,248 bytes.
2. Magic is `HCPR`, version is `1`, and commit marker is `0xA5`.
3. Recomputed CRC32C equals the stored four-byte CRC.
4. Any future Bank-identity policy also accepts the record.

Among valid records, the larger `uint32_t` generation is current, using a
wrap-safe comparison when a writer exists. If both valid generations tie,
`/.hcprms1` wins deterministically.

The creation pair is deliberately initialized as:

| File | Generation | Validity role |
| --- | ---: | --- |
| `/.hcprms1` | 1 | Current valid baseline; fixture CRC32C `0x76EFB841` |
| `/.hcprms2` | 0 | Previous valid baseline; fixture CRC32C `0xA0FCAFCD` |

The current code creates these values only when a target is absent. It does
not rewrite old-format or corrupt existing targets, because preserving existing
objects is the scope boundary of this first pass.

When the later writer is added, it must use this durability order on the
inactive record: clear the commit marker, write mask/payload, write generation
and CRC, sync, write `0xA5` as the final commit byte, then sync again. A reset
before the final marker leaves the older valid record as the winner.

## Implementation map and memory contract

| File | Current role and why it exists |
| --- | --- |
| `Core/Bank/Scene/Autosave.h` | Owns filenames, exact offsets/size assertions, control-header constants, the formatter API, and the complete-record validator declaration so future code cannot reinterpret the wire format. |
| `Core/Bank/Scene/Autosave.c` | Emits deterministic header/mask/name bytes in a caller-owned range, calculates CRC32C without a table or retained buffer, and validates a complete supplied record. It contains no filesystem calls or persistent storage. |
| `Core/Hardware/SD/filesystem.h` | Declares the narrow boot-only ensure wrapper and documents its no-Bank/no-overwrite contract. |
| `Core/Hardware/SD/filesystem.c` | Reads HCNAMES into the existing cache, scans root before each create, streams only missing targets through the existing 512-byte buffer, and assigns generations A=1/B=0. |
| `main.c` | Calls the wrapper after the Bank/fallback ladder and before audio initialization, only when `bank_hasResidentBank()` is true. |
| `Makefile` | Links `Autosave.c` into the firmware. |

No new persistent RAM is allowed or used. The operation borrows existing
`staging_buf[512]`, `fs_list_cache_name[1000][9]`, the serialized filesystem
operation fields, and its normal file handle. The formatter and validator use
only scalar local variables.

## Implementation notes — 2026-07-29

1. The original creation-only pair was 23,184 bytes and encoded the validated
   Bank slot in the Bank section. It has now been superseded by this 23,248-byte
   header-first format; no Bank slot is stored anywhere in the record.
2. `Autosave.c/.h` were replaced with the compact pure formatter/CRC32C
   implementation. The filesystem calculates the initial CRC once per newly
   created target and stores that scalar in existing operation scratch while
   streaming its chunks. The rejected dirty ledger, payload codec, scheduler,
   and staged overlay barrier remain absent.
3. The supplied `SD_CARD/.hcprms1` and `SD_CARD/.hcprms2` fixtures are to be
   regenerated from the same formatter after this format revision. Their
   contents must be byte-for-byte valid under `autosave_validateRecord()`,
   with `/.hcprms1` at generation one and `/.hcprms2` at generation zero.
4. The boot path still does only creation of absent files. Hardware/card tests
   must verify absent-file creation, byte-for-byte preservation across reboot,
   no-Bank inactivity, corruption rejection once a reader is introduced, and
   final-sync behavior under a card error.
