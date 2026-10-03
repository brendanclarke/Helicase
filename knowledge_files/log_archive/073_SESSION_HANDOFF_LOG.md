# Session 073 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch**: `dev-ph5-effects`
**Session dates**: 2026-09-28 / 2026-09-29
**Base commit**: `05bbd83` ("session 072, fx bus closeout and logs")
**State at close**:
- HEAD `692abf8` ("dsp refactor") holds the flash expansion, the
  Load:[Samples] fix and the DSP CPU refactor.
- Uncommitted in the working tree (the user manages commits):
  - the post-review corrections (§6.9);
  - the growth-drill knob removal (§5.3);
  - the `tools/dsp_golden/` → `tools/dsp_test/` and `SRAM_MANIFEST.md` →
    `STORAGE_SRAM_MANIFEST.md` renames (both staged by `git mv`) and
    `tools/dsp_test/DSP_TEST.md`;
  - this session's documentation (§8).
- `LXRV2_update_v1.70.img` (Erica factory firmware) and
  `build/LXRV2_lxr02.img` were committed in `692abf8` and are deleted in the
  working tree. The user did that; nothing in the build deletes them
  (`make clean` removes only `build/`).
- The session's root documents are superseded by this log and may be deleted
  (§13).

**Authority**:

| Document | Role |
|---|---|
| This log | Implementation record, decisions, measurements, open items |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | Flash, sample flash and RAM layout (renamed from `SRAM_MANIFEST.md` this session) |
| `knowledge_files/specification_reference/INSTRUMENTS_DSP_REFERENCE.md` | Instrument, modulation and LFO DSP: structure, costs, how to modify (new) |
| `knowledge_files/specification_reference/EFFECTS_MIXER_DSP_REFERENCE.md` | Mixer, FX bus, Effect DSP and output pipeline: structure, costs, how to modify (new) |
| `knowledge_files/specification_reference/CPU_USE_DSP_AUDIT.md` | DSP cost audit; Session 073 section and priority items 16–25 |
| `tools/dsp_test/DSP_TEST.md` | The host DSP test bench |
| `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md` | Deferred Load-menu speed analysis (the trace logger's share) |
| `S074_EFFECT_BUGS_BUFFER_USE.md` | Startup document for Session 074 |

---

## End of session

```
DATE: 2026-09-29
SESSION GOAL: S073_FLASH_EXPANSION.md (program flash +256 KiB), then
              S073_CPU_USE_DSP_REDUCTION_REFACTOR.md (lower and constant
              worst-case DSP CPU), starting from S073_SESSION_STARTUP.md.
COMPLETED:
  - Flash expansion (Phase C only; Phases A/B skipped by user decision):
    736 KiB application window, samples from sector 7, Reset_Handler first,
    per-sector CRC32 boot image check.
  - Post-flash bugfixes: Load:[Samples] restored (A); slow Load type
    switching analysed and deferred (B); growth-drill knob removed (C).
  - DSP CPU refactor Steps 0-6 (Step 7 and Step 8 rejected), implemented by
    the user from S073_CPU_REDUCTION_IMPLEMENTATION.md, reviewed, corrected.
  - tools/dsp_test host bench (renamed from dsp_golden) and DSP_TEST.md.
  - Session log, index, specification updates (two new DSP references,
    STORAGE_SRAM_MANIFEST.md), SCOPING, MEMORY, volatile notes, S074 startup.
VERIFIED ON HARDWARE: Yes, by the user.
  - The flash-expansion image loads through the bootloader and boots.
  - Load:[Samples] installs and plays samples (first install at sector 7).
  - DSP refactor: "hardware test seems ok"; about 10 % less CPU on the
    worst-case Scene with the dual-filter (StereoFilter) Effect.
  - Not reported individually: old install -> 0 samples (Gate C3), bytes
    free after install, install with the sequencer playing.

CHANGES THIS SESSION (details in §4-§8, file list in §11):
- STM32F765VIHx_FLASH.ld: 736 KiB FLASH, __sample_flash_start, ASSERTs,
  .image_check, Reset_Handler first, stack comment (SRAM2)
- Core/Hardware/flashImage.c/h (new): boot image check
- tools/stamp_image_check.py (new); Makefile .bin stamp step, .ld prerequisite
- Core/SampleRom/SampleMemory.h, sampleFlash.c: sector-7 floor + interlock
- Core/Hardware/memtest.c/h, filesystem.c (comment), tools/link_budget.py
- Core/Menu/menu.c: SAVE_TYPE_SAMPLES in the Load whitelist; modal waits
  for a running storage operation
- DSP refactor: ResonantFilter.c/h, InstrumentManager.c/h, the four
  *Parameters.c tables, EffectParamRows.h, AudioCodecManager.c, clocks.c,
  BufferTools.c/h, distortion.c/h, voicePostChain.h (new), the four voice
  .c files, Oscillator.c, mixer.c, main.c (diagnostic digit)
- tools/dsp_test/ (new; built as tools/dsp_golden/)
- Docs: CPU_USE_DSP_AUDIT.md, the new/renamed specs, SCOPING_TARGETS.md,
  MEMORY.md, README.md, knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md

KNOWN ISSUES INTRODUCED:
- None functional.
- ITCM grew 400 B (osc_setFreq now its own ITCM function; recorded).
- Cosmetic: a duplicated comment line in ResonantFilter.c (S073 Step 1
  guard comment, "would make the sound and CPU contract ambiguous.").
KNOWN ISSUES RESOLVED:
- Flash headroom: 8,080 B -> 266,560 B.
- Load:[Samples] missing from the Load page since July (a62221f).
- Stack wording (S072 debt 5): the linker now says top of SRAM2.
- S072 debt 4 (modNode_waveInterpGeneration initializer): verified harmless
  in S073 review (§9.3).

NEXT SESSION RECOMMENDED GOAL: Session 074 (S074_EFFECT_BUGS_BUFFER_USE.md):
  1. Effect page: underline the names of parameters automated in the current
     Pattern and locked in the FX sequence.
  2. The first Effect type that uses the shared DTCM buffer.
BLOCKERS: none. Decisions needed are listed in the S074 startup document.

CRITICAL REMINDERS FOR NEXT SESSION:
- Constant-CPU rule: never save CPU by skipping DSP work because something is
  inactive, silent or at zero. Budget the worst case with everything active.
- The sample floor is sector 7. Never roll back to a pre-S073 image over a
  grown image with samples installed before S073 without reinstalling.
- The bootloader is unproven past 0x08080000. The boot image check reports a
  bad sector as "Img BAD s:.....6".
- DMA buffers are Normal non-cacheable; pack_audio_half() must end with DSB.
- New instrument parameter rows need the right IM_SPECIAL_* tag; run
  make -C tools/dsp_test special_tags.
- Any RAM change (including ITCM code growth) needs byte count, region,
  lifetime and owner, and the user's acknowledgement.
- Use `make all` (bare `make` can stop at build/main.o).
- Commits are the user's. Do not suggest when to commit.
```

---

## 1. Build metrics

| Point | text | data | bss | Flash payload | Window | Headroom | ITCM | DTCM statics | FXBUF |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| S072 close (`05bbd83`) | 483,024 | 416 | 426,336 | 483,440 | 491,520 | 8,080 | 3,768 | 4,448 | 126,624 |
| Flash expansion | 483,744 | 416 | 426,336 | 484,160 | 753,664 | 269,504 | 3,768 | 4,448 | 126,624 |
| + Load:[Samples] fix | 483,936 | 416 | 426,336 | 484,352 | 753,664 | 269,312 | 3,768 | 4,448 | 126,624 |
| DSP refactor (`692abf8`) | 486,688 | 416 | 426,336 | 487,104 | 753,664 | 266,560 | 4,168 | 4,448 | 126,624 |
| Session close (working tree) | 486,688 | 416 | 426,336 | 487,104 | 753,664 | 266,560 | 4,168 | 4,448 | 126,624 |

Notes:

- **RAM:** `data` and `bss` did not change all session. The `bss` column
  includes the 126,624 B NOLOAD DTCM arena (see `STORAGE_SRAM_MANIFEST.md`).
- **ITCM +400 B** at the DSP refactor: `osc_setFreq()` (`INITCM`) is now
  linked as its own 400 B function in ITCM, called through an 8 B flash
  veneer. Before, it was inlined into its callers in flash. Recorded under
  the RAM policy (byte count 400, region ITCM, static code, owner
  `Oscillator.c`).
- **Close image:** `lxr02.bin` 487,104 B, SHA-256
  `1bd8be5201eecf0222d3cdc6c36bf272ce38a33aee535ca5fcfd0a47c155fc82`. The
  post-review corrections, the drill removal and the rename left it
  byte-identical to the `692abf8` build.
- **Warnings:** 20, all pre-existing (none from S073 files).
- **Flash image layout at close:** `.isr_vector` 456 B, `.image_check` 32 B,
  `.text` 481,520 B, then the `.itcm` (4,168), `.data` (416) and `.dtcm`
  (512) load images.

---

## 2. What Session 073 delivered (summary)

1. **Program flash grew from 480 KiB to 736 KiB.** Sector 6 moved from
   sample storage to the application. The closed bootloader was not studied
   and no probe images were built (user decision). Instead, every boot now
   checks one stamped CRC32 per application sector and names any sector the
   bootloader failed to write.
2. **Load:[Samples] works again.** It had not been in the Load menu's type
   list since July. The install modal also waits for a storage operation that
   was already running.
3. **Slow Load type switching** was analysed and deferred. The trace logger
   stays on for now.
4. **DSP CPU dropped by about 10 %** on the worst-case Scene, with no change
   to the sound beyond the approved rounding-level filter differences:
   - the ZDF filter uses 2 divisions per sample instead of 5;
   - instrument parameter writes no longer search strings;
   - the DMA pack stores words into a bufferable region;
   - the voice post-chains and the mixer dry + send run as single passes;
   - wavetable octave selection uses a threshold table instead of `log2f()`.
5. **A standing rule:** CPU is never saved by skipping DSP work when
   something is inactive or silent.
6. **A reusable host test bench** for DSP code (`tools/dsp_test/`).

---

## 3. Working method used this session

- **Plans first.** Each area started from a plan document, reviewed against
  the code, with decisions and open questions added for the user before any
  implementation (`S073_FLASH_EXPANSION.md` §10;
  `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md` §0 and §2).
- **The flash expansion and the menu fixes** were implemented by the
  assistant on the user's instruction.
- **The DSP refactor** followed the S072 method: the assistant wrote a
  code-level schedule (`S073_CPU_REDUCTION_IMPLEMENTATION.md`, every change by
  file and line with full comment blocks, pre-validated in a scratch copy);
  the user implemented it (commit `692abf8`); the assistant reviewed the
  diff, rebuilt, re-ran every harness gate, and corrected what the review
  found (§6.9).
- **No new utilities.** Sound claims were proved with the host compiler, the
  ARM toolchain and Python's standard library only.
- **Hardware checks belong to the user:** listening, the worst-case Scene,
  controls, and the Effect budget.

---

## 4. Flash expansion (`S073_FLASH_EXPANSION.md`)

### 4.1 Why

Free application flash fell from 34,356 B at S072 Step 1 to 8,080 B at the
S072 close. At that rate the 480 KiB window would run out during Phase 5 or
6. The chip has 2 MiB in single-bank mode: sector 0 (32 KiB) holds the
closed LXRV2 bootloader, the application had sectors 1–5
(`0x08008000–0x0807FFFF`), and samples had sectors 6–11.

### 4.2 Decision and evidence

- **Phases A and B were skipped by user decision** (bootloader dump and
  analysis; probe images T1–T4 that would have tested truncation and erase
  behaviour past 480 KiB). The user chose to build a normal image with
  `make clean` / `make img` and test on the device. The preserved test
  design is in Appendix A, in case it is ever needed.
- **A0 (from the user):** installed samples generally survive a normal
  firmware update, so the bootloader does not erase sectors 6–11 for a
  normal-size image.
- **Factory image `LXRV2_update_v1.70.img`** (user-supplied, Erica Synths):
  - payload 275,832 B (ends at `0x0804B578`), initial SP `0x20080000`;
  - no `FLASH_KEYR`/`FLASH_CR` literals and no flash keys, so it has no
    flash-writing code and no sample storage;
  - it gives no evidence about payloads over 480 KiB. Our ~483 KB images
    were the largest the bootloader had handled.
  - Do not commit vendor binaries (`*.img` is not git-ignored).
- **Consequence:** it is still unknown whether the bootloader erases by image
  size or always a fixed range (sectors 1–5). The first image that grows into
  sector 6 settles it, and the boot image check (§4.3 item 2) makes a failure
  visible on the device.
- **Sample capacity cost:** the sample audio region shrank from 1,570,464 B
  (about 17.8 s of 44.1 kHz 16-bit mono) to 1,308,320 B (about 14.8 s).
  Accepted (decision E3).

### 4.3 What changed

1. **Linker (`STM32F765VIHx_FLASH.ld`):**
   - `FLASH` length `0x78000` → `0xB8000` (window `0x08008000–0x080BFFFF`);
   - `__sample_flash_start = 0x080C0000`, with ASSERTs that the `FLASH`
     region ends there and that `_etext` and `_eflash_load` stay below it;
   - `KEEP(*(.text.Reset_Handler))` first in `.text`, so the reset vector
     stays in sector 1 however the image grows (now `0x080081E8`), with an
     ASSERT that it does;
   - the `.image_check` block (item 2) and its ASSERT (32 B, in sector 1);
   - the header comment corrected: the stack starts at the top of **SRAM2**
     (`0x20080000`).
2. **Boot image check (new, in production):**
   - A 32 B `.image_check` block sits right after the vector table: word 0
     magic (`IMC?` placeholder, `IMCK` when stamped), words 1–6 one CRC32 per
     application sector 1–6 over that sector's share of the image (the block
     itself skipped), word 7 the image length. `(READONLY)` keeps it out of
     `size`'s data column.
   - `tools/stamp_image_check.py` stamps it into `lxr02.bin` in the Makefile
     `.bin` rule (Python `zlib.crc32`, reflected CRC-32, poly `0xEDB88320`).
     If the binary layout is not what the linker promises, the tool fails and
     the Makefile deletes the `.bin`, so no unstamped image can be packaged.
   - `Core/Hardware/flashImage.c/h`: `flashImage_verifyAtBoot()` runs in
     `main.c` after `din_init()`/`time_initTimer()` and before any sample,
     DSP or storage code. It recomputes the CRCs with a 16-entry nibble table
     (about 20 ms, no RAM, about 0.5–0.7 KB flash).
     - Silent on success.
     - On a mismatch it shows `Img BAD s:<bad sectors>` / `Reflash. BAR1=go`;
       an unstamped image shows `Img unstamped`. Boot then waits for a BAR1
       press and release (PB7), so a checker fault can never brick a good
       image.
     - The block is in sector 1, which every update rewrites, so a bad
       sector 6 cannot corrupt the words that report it. (The first design
       put the block at the image end; that would have been damaged by the
       very failure it must report.)
3. **Sample floor → sector 7:**
   - `SampleMemory.h`: `SAMPLE_ROM_START_ADDRESS 0x080C0000`,
     `SAMPLE_FIRST_SECTOR 7u`;
   - `sampleFlash.c`: every erase/write guard uses `SAMPLE_FIRST_SECTOR`;
     `_Static_assert`s tie the address to the sector base; a runtime
     interlock (`floor_matches_linker()`) refuses every erase and write if
     the macro differs from the linker's `__sample_flash_start`;
   - `memtest.c/h`: `ERASE_SECTOR_FLOOR` follows `SAMPLE_FIRST_SECTOR`; the
     scope check watches sector 6 (now the last application sector); labels
     updated;
   - `filesystem.c`: comment "six 256 KB sectors" → five.
4. **`tools/link_budget.py`** reads the limit from `__sample_flash_start`
   (default `0x080C0000` only if the symbol is missing).
5. **Makefile:** `flashImage.c` in `SRCS`; the linker script is now a
   prerequisite of the ELF (layout edits relink without a clean); the stamp
   step in the `.bin` rule.
6. **Old sample installs need no migration code.** `sampleMemory_infoValid()`
   rejects offsets below `SAMPLE_ROM_START_ADDRESS + 4`, and entry 0 of a
   pre-S073 install is at `0x08080004`. The device boots with 0 samples until
   Load:[Samples] runs.
7. **Removed later (§5.3):** the `FLASH_GROWTH_DRILL_KB` knob that the first
   implementation added without a request.

### 4.4 Host gates

| Gate | Result |
|---|---|
| Clean build | Links; no new warnings. `text=483,744` (+720), `data`/`bss` unchanged. ITCM, DTCM statics and FXBUF unchanged. |
| Link budget | 484,160 / 753,664 B; headroom 269,504 B |
| Image | `LXRV2_lxr02.img` 484,176 B, checksum OK; header size = `.bin` size = `_eflash_load − origin`; reset vector `0x080081E9` |
| Check code against the stamp | `flashImage.c`'s CRC and sector code, compiled on the host and run on the `.bin`, matched all six stamped words |
| Negative cases (host) | One flipped byte in sector 5 → `s:....5.`; placeholder magic → `Img unstamped` |
| 64 KiB drill (scratch build) | Image end `0x0808E3E0` (sector 6); `_etext` `0x0808D188`; `Reset_Handler` still `0x080081E8`; check passes |
| Drill, sector 6 programmed without erase (simulated as `new & old`) | `Img BAD s:.....6` |
| Drill, truncated at 480 KiB (simulated) | `Img BAD s:.....6` |
| Gate C3 logic | `sampleMemory_refresh()` stops at entry 0 (`0x08080004` < new floor): 0 samples |

Not run: the §7.2 C2 one-off hardware floor call (the guards are compile-time
and interlock-checked).

### 4.5 Hardware results

| Step | Date | Result | Notes |
|---|---|---|---|
| Boot / image check | 2026-09-28 | **PASS** | Loads through the bootloader and boots; no image-check screen |
| Old install → 0 samples | 2026-09-28 | Not reported | |
| Load:[Samples] | 2026-09-28 | **PASS** after the §5.1 fix | First install at the sector-7 floor (Gate C4, functional) |
| Regression | 2026-09-28 | **FAIL** | Slow Load type switching (§5.2); cause not S073-specific (no menu or storage change in the flash work) |
| Growth drill | — | Withdrawn | Unrequested; knob removed (§5.3) |

### 4.6 Risks that remain

| # | Risk | Mitigation |
|---|---|---|
| R1 | The bootloader flashes the wrong file: tagged copies in the card root can take the `~1` 8.3 alias, and macOS writes `._LXRV2_lxr02.img` (AppleDouble). | Keep exactly one `.img` in the card root; remove `._*` files after copying. |
| R6 | **Rolling back past S073.** A pre-S073 sample install, then a grown S073+ image with code in sector 6, then a pre-S073 image without reinstalling: the old firmware accepts the old index and plays program code as audio at full scale. The other directions are safe (a pre-S073 install under S073 shows 0 samples; an S073 install under pre-S073 firmware plays correctly). | Never roll back past S073 onto a pre-S073 sample install without running Load:[Samples] first, at low volume. |
| — | The bootloader may not erase or program sector 6. | Unknown until the image grows past `0x08080000`. The boot image check reports it by sector number; recover by holding the encoder at power-on with a known-good image. |

---

## 5. Post-flash menu bugfixes (`S073_POST_FLASH_MENU_BUGFIXES.md`)

The first hardware pass of the flash-expansion image (2026-09-28) found two
defects and one unwanted addition.

### 5.1 A — Load:[Samples] missing (fixed, hardware PASS)

- **Cause:** the Load page cycles only through `menu_loadSaveLoadTypes[]`
  (`menu.c`): Kit, KitMrp, Scene, Bank, Pattern. `SAVE_TYPE_SAMPLES` was not
  in it. The whitelist came in with `a62221f` (July, "Kit save by
  slot-directory working again") and Samples was never in it. Not an S073
  regression: installed samples survive updates, so nobody had needed to
  reinstall. Everything behind the row still existed: the OK dispatch
  (`case SAVE_TYPE_SAMPLES` → `menu_loadSamplesModal()`), the modal, the
  `"Samples "` label and `filesystem_installSampleFolderBlocking()`.
- **Second problem:** the installer returns 0 at once if the storage facade
  is `FS_STATUS_BUSY`, and the modal suspended audio without waiting for a
  background operation already running. Once OK is accepted no background
  writer can *start* (the trace flushes and settings writer defer to an
  accepted command; the AutoSave writer and Pattern drains stand down on the
  Load page), but one started just before OK could still own the facade, and
  the modal blocks the main loop that would finish it.
- **Fix (`menu.c`):**
  - `SAVE_TYPE_SAMPLES` appended to `menu_loadSaveLoadTypes[]` (Load only;
    the Pot-1 ring sizes itself from the array);
  - `menu_loadSamplesModal()` pumps `filesystem_tick()` while the facade is
    busy, up to 10 s, showing `Waiting SD...`; on timeout the installer
    refuses and the normal failure text shows;
  - the modal's existing one-second suspend/resume busy-wait is kept
    (decision D-A1).
- **Checked in code:** row 2 shows `OK` (no filesystem request for an
  unnumbered type); OK is accepted once the cursor leaves the type row;
  leaving Kit/KitMrp for Samples ends the name session like other types; the
  fast drain stays off for Samples (the modal owns its codec suspend).
- **Build:** `text` +192 B; `data`/`bss` unchanged.
- **Hardware:** PASS (user). Not reported individually: bytes free after
  install; an install while the sequencer plays with AutoSave active.

### 5.2 B — Slow Load type switching (analysed, deferred)

- **Symptom:** switching between Load types takes several seconds, and the
  name row is often blank for many seconds while `.hcindex` loads. Save-page
  switching is fine. Kit, KitMrp, Instrument and Morph Instrument are worst.
- **Requirement (user, D-B1):** the type switch is instantaneous; names fill
  within tens of milliseconds.
- **Decision (user):** the trace logger (`DEV_MODE_LOGGING 1`) is the main
  contributor that can be removed, and it **stays on for now**. The analysis
  is kept in `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md` for
  when logging is switched off.
- **Load-only causes found in code** (they apply even when nothing new is
  loaded):
  1. `menu_parseEncoder()` **discards** turns and clicks while
     `menu_storageBusy` is set or Preset is not idle (except number scrolling
     on Load Kit/KitMrp/Instrument), so a type-change detent made during card
     work is lost.
  2. Instrument Load and Morph Instrument Load **write** a `.hctmp` temp on
     every entry (`menu_prepareInstrumentLoadTemp()`); Save mode skips it.
     A comment in `menu_requestInstrumentEntryNames()` claiming Morph bypasses
     the temp is stale.
  3. Every Kit entry and Instrument entry emits AutoSave trace records, and
     with logging on an `/asavetrc.bin` append is scheduled every 500 ms. The
     trace flush and settings writer are **not** held off on the Load/Save
     pages (the AutoSave writer and Pattern drains are). The trace file has
     no size cap and is appended with `"a"`.
  4. Load Kit/KitMrp number scrolling performs a real Kit load and apply.
  5. Leaving the Kit family after a load runs an HCNAMES checkpoint that
     consumes the type-change detent.
  6. The Instrument `kit` row restore reloads the `.hctmp` snapshot.
- **Why sectors are slow while browsing:** `filesystem_tick()` calls
  `afatfs_poll()` once per main-loop pass; the SD shim moves one 16-byte
  burst per poll, so one 512 B sector costs 35+ main-loop passes. The
  four-pass fast drain applies only to accepted commands with the codec
  suspended.
- **Fix directions (not scheduled):** N1 navigation never waits
  (latest-wins requests, placeholder names); N2 lazy Instrument temp; N3
  HCNAMES checkpoints off the navigation path (needs a redesign of the S051
  identity-block rule); N4 hold trace/settings writers off on Load/Save pages
  and cap the trace; N5 bounded multi-poll for browser reads; N6 less work
  per switch; N7 audition loads that do not block navigation.
- **Test plan (not run):** stopwatch/video timing per type, A/B images
  (S073, `05bbd83`, S073 with `DEV_MODE_LOGGING 0`), trace decode of
  `/asavetrc.bin` and `/bootlog.bin` with `tools/decode_devlogs.py`.
  Instrumentation would need about 8 B of logging-only SRAM1 (D-B2, deferred).

### 5.3 C — Growth-drill knob removed

- `FLASH_GROWTH_DRILL_KB` (added in the first flash implementation without a
  request) linked a constant table of that many KiB and showed
  `Img OK <end>` / `drill <addr>` for 3 s on a passing check. The user did
  not want it.
- Removed on 2026-09-29 from `config.h`, `flashImage.c` (table, hex helper,
  OK screen) and the `flashImage.h` contract. A passing check returns
  silently, as it did with the knob at 0. The image is byte-identical.
- **D-C1 (keep or remove the boot image check itself): not decided.** The
  check was also added beyond the original plan. It is left in place (the
  recommendation): it is the only thing that would report a sector the
  bootloader failed to write.

---

## 6. DSP CPU refactor (`S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`)

### 6.1 Rules set by the user (2026-09-28)

- **Constant, predictable CPU.** No CPU saving may come from skipping,
  bypassing or switching off DSP work because an element is inactive,
  silent or at zero (distortion at 0, a silent voice, a send at 0). Freed
  CPU gets filled by other features, and then everything requested at once
  underruns. Headroom is only the worst case with everything active. Any
  proposal that turns processing off for a feature that can be active must
  be raised with the user specifically; the expected answer is no. Recorded
  in `MEMORY.md` (DSP CPU Policy).
- **No new utilities** (no emulator install); sound claims proved with the
  existing host compiler, ARM toolchain and Python.
- **No profiler** (F-5): the per-stage DWT cycle profiler proposed as Step 0a
  (15 stages × last/peak × 4 B = 120 B) was declined. The user measures with
  the existing `cpu` widget, the underrun count and the worst-case Scene.
- **Commits are the user's** (F-6).
- **All SRAM increases approved**; in the end the refactor added none (ITCM
  code grew 400 B, §1).

### 6.2 Per-step decisions and required sound class

| Step | Decision | Required class | Result |
|---|---|---|---|
| 0. Golden harness + ARM check | Approved (no profiler) | old == old | PASS |
| 1. ZDF filter: batched divisions | Approved | S1 (rounding-level) | S1 PASS |
| 2. Descriptor special writers without strings | Approved | S0 | S0 |
| 3. DMA pack: word stores (3a) + MPU Normal non-cacheable (3b) | Approved | S0 | S0 |
| 4. Fused voice post-chain | Approved **only if the sound is unchanged** | S0; the shape-0 distortion bypass removed | S0 |
| 5. Mixer dry + send in one pass | Approved **if functionality is unchanged**; D-5 = implement | S0 | S0 |
| 6. Octave selection without `log2f()` | Approved; check it is "good enough" (no new tools) | differences only at octave edges | 16 edge cases within 2 ulps |
| 7. Silence gating for idle voices | **Rejected** (constant-CPU rule) | — | not implemented |
| 8. Software noise PRNG | **Rejected**: do not change the noise character | — | not implemented |

Resolved follow-ups: F-1 prove S0 with the host + ARM check; F-2 keep the
existing conditional-cost paths (FX bus off, per-voice send skip at zero
send, Effect coefficient recompute on change, algorithm choice), the user
checks the Effect budget manually; F-3 no PRNG; F-4 the user's own
worst-case Scene and kits are the fixtures; F-5 no profiler; F-6 commits.

### 6.3 Cost picture before the refactor (audit, static estimates ±50 %)

Budget: 4,897 cycles per output frame (216 MHz / 44,108 Hz); about 156,700
cycles per 32-frame block; control rate 1,378 Hz. Worst case: two Advanced
plus four Basic voices sounding, ZDF filters with drive, distortion on, 12
LFO targets, two waveform-interpolated oscillators, StereoFilter with every
send open: about 1,650–2,400 cycles/frame (34–49 %).

| Rank | Cost centre | Est. share | Step |
|---|---|---|---|
| 1 | ZDF filters (6 voices + 2 FX channels), 5–6 `VDIV` per sample | 16–21 % | 1 |
| 2 | LFO/Morph descriptor writes: up to 20 `strcmp`, 2 `strstr`, 7 `strncmp` per write, 1,378 Hz | 1.5–4.5 % | 2 |
| 3 | Oscillators | 4–6 % | 6 (small) |
| 4 | Distortion (`VDIV` per sample) and separate post-chain passes | 5–7 % | 4 (fusion only) |
| 5 | Mixer dry + send passes | 4–6 % | 5 |
| 6 | DMA pack to strongly-ordered memory | 1–1.6 % | 3 |

Expected total recovery: 9–14 % of the whole CPU in the worst case.

### 6.4 Step 1 — batched ZDF divisions (S1)

- **Files:** `Core/DSPAudio/ResonantFilter.c/.h`.
- **Algebra:** the Padé pieces of `tanhXdX(v)` are normalised so both are
  ≥ 1: `a = v²`, `N = (a/945 + 105/945)·a + 1`,
  `D = (15a/945 + 420/945)·a + 1` (`svf_padeNum()`/`svf_padeDen()`).
  - Stage A (one division): `u = in·drive/32767`, `invA = 1/(Dx·D1)`,
    `x = u·Nx·D1·invA`, `t1 = N1·Dx·invA` (input soft clip and `t1` share it).
  - Stage B (one division): with `v0 = ½(½(x+zi) − 2R·s1 − s2)`,
    `E = D0 + 2fR·N0`, `P = f²·N0·t1`, `F = P + E`,
    `Q = P·x + s2·E + f·D0·t1·s1`, `invB = 1/(D0·E·F)`; then
    `t0 = N0·E·F·invB`, `g0 = D0²·F·invB`, `y1 = Q·D0·E·invB`.
  - The rest of the loop and the output switch are unchanged; LP keeps its
    `fastTanh()`. `s1/s2/zi` stay in locals and are written back once.
  - The float Effect twin `SVF_calcBlockZDFFloat()` got the identical change.
  - `FILTER_NAIVE_2_POLE` is unchanged (one division, `q` hoisted).
  - An `#error` rejects configurations the algebra does not cover
    (`ENABLE_NONLINEAR_INTEGRATORS 0` or the shaper).
- **Result:** ARM `vdiv` in the filter object 81 → 39; per sample 5 → 2
  divisions per ZDF type (LP float 6 → 3). Loop bodies grew from 73–89 to
  85–104 instructions, which the saved divisions (about 14 cycles each,
  mostly on one dependent chain) far outweigh. Flash +416 B (int16) and
  +304 B (float).
- **S1 evidence** (28,800 configurations × 10 signals, 276,480,000 samples
  per variant): int16 SDR 91.69 dB, float 86.55 dB; ≤ 1 LSB outside the
  self-oscillating family; every configuration over 16 LSB at cutoff 0.8 /
  resonance 0.98, where rebuilding the old code with `-ffp-contract=off`
  also diverges (int16 SDR 91.82 dB, 7 configs; float 86.78 dB, 9 configs).
  So the change moves the output only as much as a compiler flag does.

### 6.5 Step 2 — descriptor special writers without strings (S0)

- **Files:** `InstrumentManager.h/.c`, the four `*Parameters.c` tables,
  `EffectParamRows.h`, `main.c` (diagnostic only).
- **Change:** `instrument_runtime_binding_t` gained `uint8_t special` in its
  former padding byte (`_Static_assert`s pin the binding at 6 B and
  `ParamDescriptor` at 28 B, so no flash table grows). Bits 0–4 hold an
  `IM_SPECIAL_*` writer ID; bits 5–6 select the oscillator (osc1/osc2/osc3/
  noise), replacing the `osc1_`/`osc2_`/`osc3_`/`noise_` prefix match.
  `BIND_SPECIAL`, `ROW_SPECIAL` and `ROW_MENU_SPECIAL` macros tag rows;
  Effect rows use `IM_SPECIAL_NONE`.
  `instrumentManager_writeSpecialRuntime()` is now one `switch` on the tag,
  each case calling exactly what the matching old key branch called
  (including the HiHat closed/choke decay caches and the runtime-type
  rechecks). `instrumentManager_oscBySelector()` replaces the string lookup.
- **Writers:** `NOISE_FREQ, PITCH_COARSE, PITCH_FINE, FILTER_FREQ,
  FILTER_RESO, FILTER_DRIVE, FILTER_TYPE, AMP_ATTACK, AMP_DECAY,
  HAT_DECAY_CHOKE, AMP_SLOPE, PITCH_EG_DECAY, PITCH_EG_SLOPE,
  PITCH_EG_AMOUNT, TRANSIENT_WAVE, TRANSIENT_FREQ, INSTRUMENT_DRIVE,
  LFO_RATE` (18). The HiHat closed decay is the `AMP_DECAY` case on a HAT
  slot.
- **Proof:** `check_special_tags.py` — 155 rows, 0 mismatches against the old
  classifier. Under `DEV_MODE_DIAGNOSTIC` the old matcher survives as a pure
  classifier and `instrumentManager_specialTagSelfCheck()` shows the clamped
  mismatch count as the `s` digit on the `FxBf` boot row. The string calls in
  the writer path went from 22 to 0. Flash +352 B in the runtime writer;
  descriptor tables unchanged.

### 6.6 Step 3 — DMA pack (S0)

- **3a word stores (`AudioCodecManager.c`):** `dma_buffer`/`dma_buffer2`
  are `aligned(4)`; `pack_frameWord()` writes each channel frame as one
  32-bit store, `ror16((s24 & 0xFFFFFF) << 8)`, whose little-endian image is
  exactly the old MSW/LSW halfword pair. DMA stays in halfword mode. A
  `may_alias` word type keeps the view legal. Final ELF: `pack_half` has 2
  `str` + 2 `ror` per frame and no `strh`; buffers at `0x2002001c` and
  `0x2002061c`.
- **3b MPU (`clocks.c`):** region 1 (the 4 KB `.dma_nocache` at
  `0x20020000`) changed from Strongly-Ordered (TEX=0 C=0 B=0) to Normal
  non-cacheable (TEX=001 C=0 B=0, S=1, AP=011, XN=1):
  `MPU_RASR = (1<<28) | (3<<24) | (1<<19) | (1<<18) | (11<<1) | 1`. Stores go
  through the write buffer; nothing is cached, so DMA coherency is unchanged.
  `pack_audio_half()` ends with `dsb` so the half is complete before the ISR
  returns. The ADC scan buffer shares the region; its reads stay uncached.
- **Proof:** the `pack` target reports 0 differing bytes (the schedule's
  pre-validation ran 100,000 blocks including clamp values). Hardware
  (controls, audio) reported OK with the overall test.

### 6.7 Step 4 — fused voice post-chains (S0)

- **Files:** `BufferTools.h/.c` (`bufferTool_floatToInt16Store()`,
  `bufferTool_interpolatedGain()`), `distortion.h/.c`
  (`distortion_curveSample16()`, shared by `calcDistBlock()` and the fused
  loops), new `voicePostChain.h`, and the four voice files.
- **Rule:** per sample, the fused loop computes exactly the int16 value each
  old pass stored, with the same conversion or saturation, and feeds it on in
  a register.
  - Drum: `voicePost_drum()` = amp EG ramp → velocity gain → distortion.
    The velocity multiply always runs with `volumeMod ? velo : 1.0f`
    (bit-identical: an int16 value × 1.0f converts back unchanged). This
    removed the old conditional `if (voice->volumeMod)` stage.
  - Snare: `voicePost_mixAddGainDist()` = mix gain → saturating add of the
    oscillator/transient block → amp gain → distortion.
  - Cymbal/HiHat: `voicePost_addGainDist()` = saturating add → amp gain →
    distortion.
- **Kept:** the distortion division `(1+k)x/(1+k|x|)` (no reciprocal); no
  shape-0 bypass (it would make cost depend on a control and could change the
  LSB at shape 0). Non-default legacy configurations (`USE_AMP_FILTER`,
  `USE_FILTER_DRIVE`) keep their separate passes.
- **Proof:** the `postchain` target reports 0 differing samples (the
  schedule's pre-validation ran 148,608,000 samples per engine, 43 distortion
  shapes, all mix/EG/velocity/`volumeMod` values including EG 1.02, with FMA
  contraction on and off). ARM `MATCH` for Drum, Snare and Cymbal/HiHat. Per-sample instructions: Drum 38 → 32, Snare 35 → 30,
  Cymbal/HiHat 28 → 25; `mixer_calcNextSampleBlock` `vdiv` 7 → 7 and `strh`
  26 → 18 in the linked image.

### 6.8 Step 5 — mixer dry + send in one pass (S0); Step 6 — octave table

- **Step 5 (`mixer.c`):** `mixer_addVoiceInt16ToOutputAndFx()` replaces the
  send-only `mixer_addVoiceToFxBus()` (removed). It reads each decimated
  sample once and produces the dry expression (gain ramp, int16 truncation,
  `sampleMix_fromInt16()`, pan, saturating add) and the send expression
  (its own ramp, `float × 256` straight to `sample_mx_t` without the int16
  truncation, stereo-input types panned into L/R, mono-input types unpanned
  into L) with each expression's operand order. A `default` routing case
  still accumulates the send. It runs exactly when the old send ran
  (`fx_active` and a non-zero send ramp); otherwise the unchanged dry-only
  function runs. Both last-gain arrays still update every block.
  - Proof: the `mixer` target reports 0 differing samples (the schedule's
    pre-validation ran 11,491,200 cases: every routing including invalid,
    stereo/mono, pans, in-range gains, near-saturation buses). ARM `MATCH`
    for all four dry × send combinations and the default case.
  - Instructions per sample, old dry + send → combined: single-output dry
    55 → 44 (mono-input send) and 69 → 59 (stereo-input send); stereo dry
    73 → 63 and 87 → 78. `mixer_calcNextSampleBlock` +3,060 B flash (LTO
    inlines every case).
- **Step 6 (`Oscillator.c`):** `freqToTableIndex()` counts how many of ten
  constant edges `osc_octaveEdgeHz[k] = 440·2^(k − 5.75)` Hz (16.35 Hz …
  8,372 Hz) are ≤ f, with all ten compares always run. The old expression was
  `clamp((int)((69 + 12·log2f(f/440))/12), 0, 10)`. NaN and f ≤ 0 give 0;
  +inf gives 10, as before.
  - Proof: exhaustive over 217,902,482 floats from 0.001 to 65,536 Hz:
    16 mismatches, each within 2 ulps of an edge, at most 0.000404 cents.
    The target was ≤ 8 ulps. `log2f` left the image (−208 B).
  - Side effect: `osc_setFreq()` is now linked as its own ITCM function
    (+400 B ITCM, §1).

### 6.9 Implementation review and corrections (2026-09-29)

The review of `692abf8` against the schedule found the code correct and
every gate passing. Findings and corrections:

1. **Mixer instruction counts were mislabelled.** The audit and the schedule
   called the measured pair "DAC1-stereo"; it was a single-output routing
   with a mono-input send, and the ARM gate checked only that combination.
   `armcheck-mixer` now gates one loop of each combination plus the default
   case (6 `MATCH`) and names each loop index; audit item 21 has the
   corrected table.
2. **ITCM +400 B** was not recorded (the plan expected ITCM unchanged).
   Recorded in the plan, audit item 22 and the implementation notes;
   confirmed from the symbol tables.
3. **Indentation** at `mixer.c:966`: fixed.
4. **Tracked binaries:** the vendor image and `build/LXRV2_lxr02.img` were
   committed in `692abf8`; now deleted in the working tree by the user.
5. **`fpseq.py --shared-op`** was loose (it removed an operation whenever the
   reference had one). It now removes a shared operation only while another
   `--ref` loop still performs it; misuse exits with an error.
6. **Send-condition wording:** the plan named the removed
   `mixer_addVoiceToFxBus()` as the location of the per-voice send skip; it
   now names the slot loop in `mixer_calcNextSampleBlock()`.
7. A stale line number in the `freqToTableIndex()` comment was removed.

The firmware image stayed byte-identical through all corrections.

### 6.10 Hardware result (user, 2026-09-29)

"Hardware test seems ok." About **10 % less CPU use** on the user's
worst-case Scene with the dual-filter Effect (StereoFilter: two float ZDF
instances). The plan's estimate was 9–14 % of the whole CPU. The saving comes
from doing the same work with fewer operations, so it holds with every voice,
send and Effect active.

### 6.11 Checked and rejected (not recommended)

- Filter or distortion in ITCM (measured worse in Session 023).
- A write-back SRAM cache.
- Voice runtime slots in DTCM (the free DTCM is the reserved FX arena).
- A Morph "skip unchanged value" cache (Session 16 failed approach).
- A larger `OUTPUT_DMA_SIZE` (changes every envelope/LFO time).
- The decimator short-circuit at rate 1.0 (hides the real budget).
- Nonlinear integrators off, a half-rate LFO, or an "eco" filter mode
  (audible; any such idea goes to the user first).
- Skipping `SVF_recalcFreq()` when f is unchanged (about 0.1 %, not worth the
  state).

---

## 7. DSP test bench (`tools/dsp_test/`)

- Built in Step 0 as `tools/dsp_golden/`; renamed `tools/dsp_test/` at the
  close (user request) and kept for future DSP work (new oscillators,
  filters, Effects such as phasers, more distortion modes).
- Contents: `extract.py` (exact source extraction with renames),
  `prelude.h` (host stubs), `snapshot.sh` and `frozen/` (the 14 pre-S073
  files, byte-identical to `05bbd83`), `fpseq.py` (ARM loop operation
  multisets and instruction counts), `check_special_tags.py`, five host
  comparison programs, two ARM translation units, and the `Makefile`.
- Host flags include `-fno-vectorize -fno-slp-vectorize`: clang's vectoriser
  produced a false old/new difference on an out-of-range gain (undefined
  float→int conversion).
- All targets pass from the renamed directory (2026-09-29): `selftest`,
  `filter` (about 2.5 min), `armcheck-filter`, `special_tags`, `pack`,
  `postchain`, `armcheck-postchain`, `octave`, `mixer`, `armcheck-mixer`.
- Spec sheet: `tools/dsp_test/DSP_TEST.md` (purpose, limits, layout, how each
  part works, targets, how to use it for refactors, extensions and new DSP,
  measured reference costs, pitfalls).
- Source comments in `mixer.c`, `Oscillator.c`, `ResonantFilter.c` and
  `InstrumentManager.c/.h` point into it (updated to the new path).

---

## 8. Documentation changes

| File | Change |
|---|---|
| `CPU_USE_DSP_AUDIT.md` | Session 073 current-state audit (method, render structure, cost table, findings F1–F10, corrections, checked-and-rejected, sound classes); priority items 16–25; items 21 and 22 corrected |
| `STORAGE_SRAM_MANIFEST.md` | Renamed from `SRAM_MANIFEST.md`; now also covers flash (window, image layout, ASSERTs, image check, bootloader facts, growth history and paths) and sample flash; ledger updated to S073 (ITCM 4,168 B) |
| `INSTRUMENTS_DSP_REFERENCE.md` | New: instrument DSP, modulation and LFO; costs; how to modify |
| `EFFECTS_MIXER_DSP_REFERENCE.md` | New: mixer, FX bus, Effect DSP, output pipeline; costs; how to modify |
| `EFFECTS_BUS_REFERENCE.md` | Signal flow updated to the combined dry + send pass; flash pitfall and manifest name updated |
| `DEV_MODES.md` | The `s` digit on the `FxBf` diagnostic row |
| `MODULE_INTERCHANGE_SPEC.md` | Special-writer tags, `flashImage`, the combined mixer function |
| `FILESYSTEM_SPEC.md`, `AUTOSAVE.md`, `BANK_PRESET_ARCHITECTURE.md`, `PATTERN_DYNAMIC_STACK.md` | Manifest name; S073 status lines |
| `tools/dsp_test/DSP_TEST.md` | New |
| `knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md` | New (deferred Load-menu analysis) |
| `SCOPING_TARGETS.md` | §5.5 resolution, Phase 5 carried items, S073 carried items, the Effect-page underline bug, carried-debt status |
| `MEMORY.md`, `README.md`, `000_SESSION_INDEX.md`, volatile notes | Session 073 context |

---

## 9. Open items and carried debt

### 9.1 Carried to Session 074

- **Effect-page automation underlines (bug, user-reported):** parameter names
  on the Effect page are not underlined when the parameter is automated in
  the current Pattern, and they should also be underlined when the parameter
  has FX-sequencer locks. The VOICE pages have this (`va_scanService()` /
  `va_applyVoiceMarkers()` in `menu.c`); the Effect page only underlines held
  FX-lane values (`menu_applyEffectMarkers()`). Details and design questions:
  `S074_EFFECT_BUGS_BUFFER_USE.md`.
- **First buffer-using Effect type** (Phase 5 A8, plan §5.4/§5.6), with the
  as-built gaps it must close: the same-type Scene switch handoff refresh
  (`EFFECTS_BUS_REFERENCE.md` §13 item 1) and the FX return ramp while `off`
  (S072 debt 8).

### 9.2 Still open from this session

- The bootloader past `0x08080000` (first grown image is the test).
- Slow Load type switching (B), deferred with the trace logger.
- D-C1 (keep the boot image check): undecided, check kept.
- Phase 5 hardware acceptance matrices for S072 Steps 6–10
  (`072_SESSION_HANDOFF_LOG.md` §11): not reported as run.
- Cosmetic: the duplicated comment line in `ResonantFilter.c` (Step 1 guard
  comment).

### 9.3 S072 carried debt, status at S073 close

| # | Item | Status |
|---|---|---|
| 1 | `fxbuf_init()` handoff-reset order (diagnostic only) | Open |
| 2 | Makefile link-budget recipe comments echoed every build | Open |
| 3 | Bare `make` stops at `build/main.o` (no `.DEFAULT_GOAL := all`) | Open; use `make all` |
| 4 | `modNode_waveInterpGeneration` `INCCMZ` with `= 1u` | **Verified harmless:** `modNode_resetTargets()` increments it before the first render and skips 0, so it is 1 or more whenever an oscillator compares it |
| 5 | Linker comment said stack at top of SRAM1 | **Resolved** (S073 linker comment) |
| 6 | Duplicated comment line in `mixer_calcNextSampleBlock()` | Open (now around `mixer.c:853`) |
| 7 | Mixed indentation in `presetManager.c` comment blocks | Open |
| 8 | FX return ramp not reset while `off` | Open; revisit with the first buffer type |
| 9 | `tools/verify_bank_autosave.py` expects 129 HCNAMES rows | Open |
| 10 | Unreachable Scene Save phases 33–36 | Open |
| 11 | Blank/space-only name stems | Open (`SCOPING_TARGETS.md`) |
| 12 | The 40 KiB oversize-link ASSERT experiment | Superseded: the window is now 736 KiB; the ASSERTs are in place |
| 13 | AutoSave tracking state at the re-validation hook | Open |

---

## 10. Next session

See `S074_EFFECT_BUGS_BUFFER_USE.md` (root). Goals: the Effect-page
underline bug, then the first Effect type that uses the shared DTCM buffer.

---

## 11. Files changed (`05bbd83` → working tree)

| Area | Files |
|---|---|
| Flash layout and image check | `STM32F765VIHx_FLASH.ld`, `Core/Hardware/flashImage.c/h` (new), `tools/stamp_image_check.py` (new), `tools/link_budget.py`, `Makefile`, `main.c` |
| Sample flash | `Core/SampleRom/SampleMemory.h`, `Core/SampleRom/sampleFlash.c`, `Core/Hardware/memtest.c/h`, `Core/Hardware/SD/filesystem.c` (comment) |
| Menu | `Core/Menu/menu.c` |
| DSP refactor | `Core/DSPAudio/ResonantFilter.c/h`, `BufferTools.c/h`, `distortion.c/h`, `voicePostChain.h` (new), `Oscillator.c`, `mixer.c`; `Core/DSP/Instruments/InstrumentManager.c/h`, `Drum/DrumVoice.c`, `Drum/DrumParameters.c`, `Snare/Snare.c`, `Snare/SnareParameters.c`, `Cymbal/CymbalVoice.c`, `Cymbal/CymbalParameters.c`, `HiHat/HiHat.c`, `HiHat/HiHatParameters.c`; `Core/DSP/Effects/EffectParamRows.h`; `Core/Hardware/AudioCodecManager.c`, `Core/Hardware/clocks.c` |
| Config | `config.h` (drill knob added then removed; no net change) |
| Tools | `tools/dsp_test/` (new, 14 frozen files + 13 tool files + `DSP_TEST.md`), `.gitignore` (`tools/dsp_test/build/`) |
| Docs | see §8; plus the root S073 plan documents |

---

## 12. Architectural invariants introduced

- **Constant CPU.** No skip, bypass or early-out keyed on a control value or
  on silence in any render path. Only loop-invariant selects that cost the
  same either way are allowed, plus the existing kept paths (F-2).
- **Flash layout.** The `FLASH` region ends exactly at
  `__sample_flash_start`; `SAMPLE_ROM_START_ADDRESS` must equal it, and
  `sampleFlash.c` refuses to erase or write otherwise. `Reset_Handler` and
  `.image_check` stay in sector 1.
- **Image check.** The stamp tool and `flashImage.c` must compute the same
  CRC (reflected CRC-32, init/final `0xFFFFFFFF`) over the same sector
  shares; the sector table appears in both (`SECTOR_ENDS` /
  `flashImage_sectorEnd`).
- **DMA region.** `.dma_nocache` is Normal non-cacheable and ≤ 4 KB; the pack
  ISR ends with `DSB`; DMA buffers stay word-aligned.
- **Special tags.** Every instrument descriptor row carries the tag its key
  implies; `check_special_tags.py` and the diagnostic `s` digit prove it.
  Descriptor and binding sizes are pinned by `_Static_assert`.
- **Fused post-chains** keep every int16 truncation and saturation point of
  the old passes and the distortion division.
- **The batched ZDF solver** exists in two twins (int16 voices, float
  Effects) that must stay arithmetically identical; the `#error` guard
  rejects configurations it does not cover.
- **The octave edge table** must stay consistent with the 11-octave
  wavetable layout in `wavetable.c`.

---

## 13. Disposable documents

Superseded by this log, the specification updates and `DSP_TEST.md`; the
user may delete them:

- `S073_SESSION_STARTUP.md`
- `S073_FLASH_EXPANSION.md`
- `S073_POST_FLASH_MENU_BUGFIXES.md`
- `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`
- `S073_CPU_REDUCTION_IMPLEMENTATION.md`

Still disposable from S072: `S072_ST1_IMPLEMENTATION.md` …
`S072_ST11_IMPLEMENTATION.md`. `EFFECTS_BUS_FEATURE_PLAN.md` stays as the
Phase 5 design record.

---

## 14. Facts carried from `S073_SESSION_STARTUP.md`

- **Build:** use `make all`, not bare `make` (in an incremental tree the
  `-include $(OBJS:.o=.d)` line precedes `all:`, so bare `make` builds only
  `build/main.o`). Measure flash with
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`
  (`LINK_BUDGET_WARN_FLASH`, default 16,384, sets the warning).
- **Build-host quirk:** `lto1: internal compiler error: Bus error: 10` has
  twice left an empty `build/lxr02.elf`, likely when two builds share
  `build/`. Delete the ELF and relink; never run two builds at once.
- **Card state:** the firmware uses AutoSave HCPR v3 and 161-row
  `.hcnames`. An older card is rejected until root `.hcnames`, `.hcnamtmp`,
  `.hcprms1` and `.hcprms2` are deleted once.
- **Image format:** `LXRV2IMG` magic (8 B), payload size (4 B LE), an
  **8-bit** additive checksum (4 B LE), then the payload. The packer
  (`tools/build_lxrv2_img.py`) has no size check; the linker ASSERTs and the
  boot image check are the guards.
- **Largest flash consumers** (S072 measurement): `crashSample` 32,768 B;
  `transientData` 26,460 B; `sawTable`, `triTable`, `recTable` 22,528 B each;
  `sine_table` 8,194 B. Largest code: `main`, `filesystem_tick`,
  `menu_repaintGeneric`, the Scene/Bank load/save ticks (6.5–9 KB each).
- **Growth paths if flash ever runs short again** (S072 ranking):
  1. `-Os` for cold control modules (`menu.c`, `filesystem.c`,
     `presetManager.c`, `storageTypes.c`) through per-file rules;
  2. large constant tables as installed data in the sample region (up to
     about 124 KiB; needs an install path);
  3. removing dead legacy code (the Euklid/SOM generators are compiled in
     although `ENABLE_EUKLID_PAGE 0` hides the page).

---

## Appendix A. Bootloader test plan (designed, not executed)

Kept in case the bootloader ever fails on a grown image. Source:
`S073_FLASH_EXPANSION.md` §2–§10.

- **Questions:** Q1 does the bootloader accept a payload over `0x78000` B;
  Q2 does it program all of it; Q3 does it erase sector 6 on every update;
  Q4 does it start the app (reset-vector checks); Q5 does a normal image
  still install afterwards.
- **Phase A (read-only):** report screens (flash size, bank mode, raw
  `FLASH_OPTCR/OPTCR1/OPTCR2`, `_eflash_load`, sectors 6–11 blank/data); dump
  sector 0 to the card (never commit it) and disassemble it
  (`arm-none-eabi-objdump -D -b binary -m arm -M force-thumb
  --adjust-vma=0x08000000`) looking for `FLASH_KEYR`/`FLASH_CR` use, erase
  strategy (`SER`/`SNB`/`MER`), size limits (`0x78000`, `0x80000`,
  `0x08080000`), RAM buffering, read-back verify, SP/PC checks, file lookup
  (long name versus 8.3 alias), watchdog writes (`IWDG_KR`), and address
  masking.
- **Phase B (probe images, sector-6 data only, never code):** probe words
  `probe_word(addr, seed)` (a murmur-style mix of address and seed) written
  from `0x08080000`; boot-time classification OK / BK (blank) / AN
  (`new & old`, programmed without erase) / ST (old data) / OT (corrupt);
  tests T1 (4 KiB overflow, 495,616 B), T2 (full sector 6, 753,664 B, over
  T1), T3 (full sector over T2), T4 (known-good normal image), T4r (verify
  sector 6 untouched by a normal image). Preferred assembly: pad `lxr02.bin`
  with `0xFF` to `0x78000` on the host and append the probe words (no linker
  change).
- **If it fails:** use the growth paths in §14 (flash headroom is now large,
  so this is unlikely to be urgent); do not add a "prepare for update"
  command that erases sector 6 before updates.
- **Recovery:** power on holding the main encoder with a known-good
  `LXRV2_lxr02.img` on the card. Sector 0 must stay intact; without SWD a
  damaged bootloader is unrecoverable (decision E1 was never answered).
