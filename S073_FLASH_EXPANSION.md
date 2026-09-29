# S073: Program flash expansion test (plan)

- **Goal:** find out, safely and conclusively, whether the application can
  grow from 480 KiB to 736 KiB by giving it flash sector 6 (256 KiB) that
  sample storage uses today. If the test passes, make that change.
- **Why now:** free application flash fell from 34,356 B at S072 Step 1 to
  8,472 B after Step 9. At that rate the 480 KiB window runs out during
  Phase 5 or 6.
- **The only real unknown:** the closed LXRV2 bootloader in sector 0. The
  code side of the move is small. Nobody has tested what the bootloader
  does with an image bigger than 480 KiB. That gap is recorded in
  `S072_ST1_IMPLEMENTATION.md` §21 finding 4, and in Session 007's open item
  "the bootloader source still needs to be inspected".
- **Samples will be erased.** You have said installed samples are
  disposable. Keep the `/samples` and `/loops` WAV folders on the card so
  they can be reinstalled.
- **No product behaviour changes until Phase C.** Phases A and B use
  test-only builds.

---

## 1. What limits program flash today

| Fact | Where |
|---|---|
| The chip has 2 MiB of flash in single-bank mode: sectors 0–3 are 32 KiB, sector 4 is 128 KiB, sectors 5–11 are 256 KiB. | Session 007 memtest; `MEMORY.md` flash table |
| Sector 0 holds the LXRV2 bootloader (32 KiB). It is a closed binary and our only recovery path. | `README.md` Boot Process |
| The application window is `0x08008000–0x0807FFFF` (480 KiB). The build fails rather than spill into sector 6. | `STM32F765VIHx_FLASH.ld:86`, ASSERTs at `:201–204` |
| Sample audio starts at `0x08080000` (sector 6). The sample index (`SampleInfo[120]`) and names sit at the top of sector 11. | `SampleMemory.h:75`, `:89–93` |
| Sample erase and program code refuse anything below sector 6. | `sampleFlash.c:170`, `:202`, `:217`; `memtest.c:136` |
| The image is `"LXRV2IMG"` + payload size + an **8-bit** additive checksum + payload. The bootloader's own integrity check is therefore weak. | `tools/build_lxrv2_img.py` |
| Today's payload is 483,048 B. That fits inside the 512 KiB of contiguous RAM (DTCM + SRAM1 + SRAM2), so the bootloader *could* be buffering whole images in RAM. A 736 KiB payload cannot fit. | `link_budget.py`; memory map |
| `Reset_Handler` is at `0x0805392C`, in sector 5, below the rodata tables. | `arm-none-eabi-nm` |
| In Session 007, with a small app, sectors 5–11 read blank. That is consistent with the bootloader not writing past its image, but it proves nothing about larger images. | `007_SESSION_HANDOFF_LOG.md` |

**What the move costs samples:** today the sample audio region is
1,570,464 B, about 17.8 s of 44.1 kHz 16-bit mono. From sector 7 it is
1,308,320 B, about 14.8 s.

## 2. What the test must answer

| Q | Question | Why it matters |
|---|---|---|
| Q1 | Does the bootloader accept a payload over `0x78000` bytes at all? | A hard size check ends the idea. |
| Q2 | Does it program the whole payload, without truncating at 480 KiB, 512 KiB, or anywhere else? | A silent truncation would leave corrupt code. |
| Q3 | Does it **erase** sector 6 before programming it on *every* update? | Programming without an erase works only on blank flash. The next firmware update would then corrupt code. |
| Q4 | Does it still start the app, with no range check on the reset vector? | A grown image could move `Reset_Handler` out of any range the bootloader expects. |
| Q5 | After a large image, does a normal-size image still install and run? | You must always be able to go back. |

---

## 3. Safety and recovery

- **Recovery path:** if an app image fails, power on holding the main
  encoder with a known-good `LXRV2_lxr02.img` on the card. This depends only
  on sector 0 staying intact.
- **Our test code never erases below sector 6.** The prep erase uses
  `sampleFlash_eraseAllSamples()`, whose guard rejects sectors below 6.
- **Known-good image:** before starting, build the current production
  firmware and keep a copy of its `.img`. Keep it on your computer and on a
  second SD card that holds only that file.
- **Back up the working SD card.**
- **SWD (decision E1):** no debug header is documented. Check the PCB for
  SWD pads (PA13/PA14). With an ST-Link plus the Phase A dump of sector 0,
  even a damaged bootloader could be restored. Without SWD, a damaged
  bootloader is unrecoverable. That is why Phase A studies the bootloader
  first and Phase B starts with a small overflow.
- **During tests:**
  - Do not power off while the bootloader is writing. A 736 KiB image takes
    longer than usual.
  - Do not run Load:[Samples] while a probe image is installed; it would
    erase the probe evidence.

---

## 4. Test tooling

### 4.1 Build switches

- **`MEMTEST_FLASH_EXPANSION` in `config.h`** (new, default 0) is a sub-knob
  of the existing boot-time hardware test `MEMTEST_ENABLED` (already 1).
  - Its screens follow the memtest precedent: a hardware test with its own
    knob, not a third development mode (`DEV_MODES.md`).
  - Its one file write, the bootloader dump, also requires
    `DEV_MODE_LOGGING 1`, which is the current setting. It must fail with
    `#error` without it.
- **`make FLASH_PROBE=1 ...`** (new) links probe data into sector 6 (§4.4).
  The variables are `FLASH_PROBE_TAG`, `FLASH_PROBE_BYTES`,
  `FLASH_PROBE_SEED`, `FLASH_PROBE_PREV_SEED`, and `FLASH_PROBE_PREV_BYTES`.
  Run `make clean` before each probe build, because the header dependency
  files do not track command-line defines.
- **Two kinds of test build:**
  - **T0** = production code + `MEMTEST_FLASH_EXPANSION 1`, normal size.
  - **Probe builds** = T0 + `FLASH_PROBE=1`.

### 4.2 Report screens (every test build)

Add these to `Core/Hardware/memtest.c`, reusing its LCD, hex and sector
helpers. Call them from `main.c` right after `dsp_init()`, next to the
FxBf diagnostic. The screens auto-advance every 3 s.

1. Flash size register (expect 2048 K) and bank mode (expect SINGLE).
2. Raw `FLASH_OPTCR` (`0x40023C14`) and `FLASH_OPTCR1` (`0x40023C18`).
3. Raw `FLASH_OPTCR2` (`0x40023C1C`, PCROP on F76x) and `_eflash_load` (the
   image end).
4. Sectors 6–11: BLANK or DATA for each, using a full-sector blank scan.

Decode the raw option bytes afterwards against RM0410 (write protection per
sector, RDP level, PCROP, boot address). The firmware does not decode them.

### 4.3 T0-only actions (normal-size build)

Both run after the SD mount in `main.c` and before any other boot I/O.

- **Bootloader dump:** a new `FS_INTERNAL_OP_DEV_FLASH_DUMP`, cloned from
  `filesystem_writeBootLog_tick()` (`filesystem.c:5223`). It uses the same
  `afatfs_fopen_lfn(..., "w", ...)`, write, close and sync path, so no new
  file-creation logic is introduced.
  - It writes `/s0dump.bin` (32,768 B read directly from `0x08000000`) and
    `/flashopt.bin` (the 12 raw bytes of `OPTCR`, `OPTCR1`, `OPTCR2`).
  - The source is memory-mapped flash, so no RAM buffer is needed.
  - Screen shows `Dump OK` or `Dump ERR`.
- **Erase prompt:** the screen shows `Erase S6-S11? / Hold BAR1 3s` for
  10 s. BAR1 is read directly from PB7, as memtest does.
  - If held, call `sampleFlash_eraseAllSamples()` and then
    `sampleMemory_refresh()`.
  - Then full-scan sectors 6–11 and show `S6-11 BLANK OK` or the failing
    sector.
  - Expect about 1–2 s per sector with interrupts off. This is a boot-only
    pause, and audio is not yet running.

### 4.4 Probe builds (sector-6 test data, never code)

**Design rule:** sector 6 holds **only probe data**, and all code stays in
sectors 1–5. If the bootloader truncates or corrupts sector 6, the firmware
still boots normally and reports exactly what it finds.

**Pattern:** one address-dependent word per 4 bytes. The Python generator
and this C function were checked to produce identical words.

```c
static uint32_t probe_word(uint32_t addr, uint32_t seed)
{
    uint32_t x = addr ^ (seed * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}
```

If the bootloader programs over un-erased data, a word reads back as
`new & old`. Across a full sector, only 6 of 65,536 words from seed `0x33`
programmed over seed `0x22` would hide that.

**New files:**

- `tools/flash_probe_gen.py --seed S --bytes N --base 0x08080000 --out build/flash_probe.bin`:
  little-endian `probe_word()` words.
- `Core/Hardware/flashProbeData.S`: `.section .flash_probe,"a"`, with
  symbols `__sflash_probe` and `__eflash_probe` around
  `.incbin "build/flash_probe.bin"`.

**Linker (test-only, removed in Phase C):**

```ld
PROBE (r) : ORIGIN = 0x08080000, LENGTH = 0x40000   /* sector 6, test data only */
.flash_probe : { __sflash_probe = .; KEEP(*(.flash_probe)) __eflash_probe = .; } >PROBE
ASSERT(__eflash_probe == __sflash_probe || DEFINED(FLASH_PROBE_BUILD),
       "Flash probe data linked into a non-probe build")
```

**Makefile:** only when `FLASH_PROBE=1`:

- add the probe object;
- pass `-Wl,--defsym=FLASH_PROBE_BUILD=1`;
- pass the five `-D` values;
- run `objcopy -O binary --gap-fill 0xFF`, so the gap between the image end
  and `0x08080000` is written as erased-state bytes.

Normal builds keep today's objcopy command.

**Gate:** with `FLASH_PROBE=0`, `lxr02.bin` must stay exactly
`_eflash_load − 0x08008000` bytes after these additions.

**Verification at boot:** after the report screens, read every probe word
with a volatile read at its absolute address and classify it.

| Class | Condition | Meaning |
|---|---|---|
| OK | `w == probe_word(a, SEED)` | Programmed correctly. |
| BK | `w == 0xFFFFFFFF` | Never programmed: truncated or ignored. |
| AN | `w == new & prev` (in the previous probe's range) | Programmed **without erase**. |
| ST | `w == probe_word(a, PREV)` | Old data left untouched. |
| OT | anything else | Corrupt. |

**Verdict screen:** it holds until BAR1 is pressed, then boot continues
normally.

- `PASS`: all words OK.
- `TRUNC @addr`: BK words form one contiguous tail. The address gives the
  bootloader's effective payload limit.
- `NO ERASE`: any AN or ST words.
- `CORRUPT`: any OT words, or BK words that are not one tail.

A second screen shows the five counts and the first failing address.

### 4.5 RAM and flash

- **RAM:** no new RAM in any build. Verification counters live on the stack
  (about 24 B). The dump reuses the facade's existing operation state
  (`op_phase`, `op_file`, `op_bytes_done`).
- **RAM gate:** `bss` and `data` unchanged against the same configuration
  without the knob.
- **Flash:** the test code adds about 2–3 KB inside the window, which fits
  the 8,472 B free. Probe data is outside the window by design.

---

## 5. Phase A — read-only evidence (no risk to anything)

**A0.** Before any build, answer from experience: do installed samples
survive a normal firmware update today? Yes means the bootloader does not
mass-erase, and does not erase sectors 6–11, for a normal-size image.

**A1.** Build and flash T0. Photograph the four report screens. Expected:
2048 K, SINGLE, sectors 6–11 DATA (your installed samples).

**A2.** On the same boot, check that the dump screen shows `Dump OK`. Copy
`/s0dump.bin` and `/flashopt.bin` to your computer.

- Do **not** commit the dump. It is the vendor's closed binary. Keep it
  local as analysis input and, if SWD exists, as a restore image.
- If it reads as all zeros or all `0xFF`, sector 0 is PCROP-protected
  (confirm with `OPTCR2`). In that case, skip A3 and rely on Phase B.

**A3. Host analysis.**

```
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
    --adjust-vma=0x08000000 s0dump.bin > s0dump.dis
```

Start from the reset vector (word 1 of the dump) and look for:

- **Flash writes.** Literals for `FLASH_KEYR` `0x40023C04` (keys
  `0x45670123` / `0xCDEF89AB`) and `FLASH_CR` `0x40023C10`. Look at how it
  builds `SER` (bit 1), `SNB` (bits 3–6) and `STRT` (bit 16), and whether it
  ever sets `MER` (bit 2).
- **Erase strategy.** Is the sector range computed from the payload size, a
  fixed list (1–5), or a sector-address table? Is sector 0 ever addressable?
- **Size limits.** Constants `0x78000`, `0x80000`, `0x08080000`, and
  `0x08200000`, and every comparison against the header's size field.
- **Buffering.** Does it read the whole file into RAM before erasing (limited
  by 512 KiB), or program as it reads?
- **Integrity.** Does it only check the 8-bit sum, or does it read back after
  programming?
- **Jump to the app.** Does it range-check the stack pointer or reset
  vector at `0x08008000` before jumping?

Record the findings in §9. Predict the Phase B outcome from them.

**A4.** Answer decision E1 (SWD).

**Gate A:** option bytes recorded and decoded; single bank confirmed; dump
analysed or confirmed unreadable; a written Phase B prediction. If the
analysis shows the bootloader can erase or write sector 0, or mass-erases,
**stop** and review before Phase B.

---

## 6. Phase B — bootloader probe tests

**B0. Wipe sample flash.** Boot T0, hold BAR1 at the erase prompt, and
confirm `S6-11 BLANK OK`. Every later result is then measured against
known-blank flash.

| Test | Build | Payload (B) | Probe | Seed / prev (prev range) | What it proves |
|---|---|---|---|---|---|
| T1 | `FLASH_PROBE_TAG=T1 FLASH_PROBE_BYTES=4096 FLASH_PROBE_SEED=0x11 FLASH_PROBE_PREV_SEED=0` | 495,616 (`0x79000`) | 4 KiB at sector 6 start | `11` / none | Q1 and Q2 with a minimal overflow. Still under 512 KiB, so it passes even if the bootloader buffers in RAM. |
| T2 | `...TAG=T2 BYTES=262144 SEED=0x22 PREV_SEED=0x11 PREV_BYTES=4096` | 753,664 (`0xB8000`) | all of sector 6 | `22` / `11` (first 4 KiB) | Q2 at the full target size; Q3 over T1's 4 KiB; Q1 above 512 KiB. |
| T3 | `...TAG=T3 BYTES=262144 SEED=0x33 PREV_SEED=0x22 PREV_BYTES=262144` | 753,664 | all of sector 6 | `33` / `22` (whole sector) | Q3 over the whole sector: erase on every update. |
| T4 | Known-good production image | 483,048 | none | — | Q5: going back to a normal image works. |
| T4r | T0 again (optional) | normal | none | — | Reports whether sector 6 still holds T3's data, i.e. whether a smaller image leaves sector 6 alone (expected). |

**Per test:**

1. Copy that test's `.img` to the card root as `LXRV2_lxr02.img`. Keep an
   archived copy named with its tag.
2. Power on holding the encoder. Note any bootloader messages and how long
   flashing takes.
3. Let it boot. Photograph the verdict and count screens.
4. Record everything in §9.

**Stop rules:**

- **Any image fails to boot:** recover with the known-good image, record
  what happened, and stop Phase B.
- **T1 = TRUNC or not accepted:** the limit is 480 KiB. Stop; see §8.
- **T2 = TRUNC at an address:** record that limit. T3 is still useful only
  if the limit leaves room worth having.

**Afterwards:** after T4 (and optional T4r), run Load:[Samples] to reinstall
samples. It erases sectors 6–11 and puts the sample area back to normal.

**Decision table (Gate B):**

| Result | Meaning | Action |
|---|---|---|
| T1, T2, T3 all PASS; T4 boots | Bootloader erases by size and handles 736 KiB. | **Phase C.** |
| T2's first 4 KiB or T3 shows AN/ST | It programs sector 6 without erasing it. | **Do not put program code in sector 6.** Use §8 fallbacks. |
| T1 TRUNC or refused | Hard 480 KiB limit. | §8 fallbacks. |
| T1 PASS, T2 TRUNC at L | Limit L, possibly the 512 KiB RAM-buffer case. | Phase C only if (L − 480 KiB) is worth losing a whole 256 KiB sample sector (decision E4). |
| Any CORRUPT | Unexplained. | Stop and analyse with the Phase A findings. |

---

## 7. Phase C — production expansion (only after Gate B passes)

### 7.1 Changes

- **`STM32F765VIHx_FLASH.ld`:**
  - `FLASH` length `0x78000` → `0xB8000` (window
    `0x08008000–0x080BFFFF`).
  - `_etext` ASSERT limit → `0x080C0000`.
  - Add `__sample_flash_start = 0x080C0000;` and
    `ASSERT(_eflash_load <= __sample_flash_start, ...)`.
  - Place `KEEP(*(.text.Reset_Handler))` first in `.text`, so the reset
    vector target stays in sector 1 however the image grows (answers Q4 by
    construction).
  - Update the header comment (`:41–42`).
  - Remove the Phase B `PROBE` region and `.flash_probe` section.
- **`Core/SampleRom/SampleMemory.h`:**
  - `SAMPLE_ROM_START_ADDRESS` → `0x080C0000` (sector 7).
  - Add `#define SAMPLE_FIRST_SECTOR 7u`.
  - Update comments (`:72–75`, `:92`). The index and name tables at the top
    of sector 11 do not move.
- **`Core/SampleRom/sampleFlash.c`:**
  - Replace the hard-coded 6 (`:170`, `:202`, `:217`) with
    `SAMPLE_FIRST_SECTOR`.
  - **Interlock:** at the top of every erase and write, refuse if
    `(uint32_t)__sample_flash_start != SAMPLE_ROM_START_ADDRESS`. This is a
    runtime compare of a linker symbol and a macro, needs no RAM, and fails
    safe if the two ever drift apart.
  - Add `_Static_assert`s that the sector-7 table base equals
    `SAMPLE_ROM_START_ADDRESS`.
- **`Core/Hardware/memtest.c` and `memtest.h`:**
  - `ERASE_SECTOR_FLOOR` → `SAMPLE_FIRST_SECTOR`.
  - The scope check watches sector 6 (now the last app sector) instead of 5.
  - Update the sector labels (`:124`).
- **`Core/Hardware/SD/filesystem.c:21251`:** comment "six 256 KB sectors" →
  five. The installer uses the address macros and needs no other change.
- **`tools/link_budget.py`:** `FLASH_LIMIT` → `0x080C0000`, plus the
  docstring. Update the `Makefile:166` comment.
- **Old sample installs need no migration code.**
  `sampleMemory_infoValid()` already rejects offsets below
  `SAMPLE_ROM_START_ADDRESS + 4`. An old install's first entry is at
  `0x08080004`, so the device boots with zero samples until you reinstall.
  Gate C3 proves this.

### 7.2 Gates

1. **Build:** it links; `link_budget.py` reports the 736 KiB window; the
   normal payload size is unchanged.
2. **Floor:** a sector-6 erase or write through `sampleFlash` returns an
   error. Check with a one-off T0-style call, then remove it.
3. **First boot on an old sample install, at LOW volume:** zero samples
   listed; sample waveforms are silent. A failure here would play program
   bytes as full-scale audio, hence the low volume.
4. **Reinstall with Load:[Samples]:**
   - sectors 7–11 are erased and sector 6 is untouched;
   - samples and loops play;
   - bytes free are about 1,308,320 B.
5. **Growth drill (temporary):**
   - Add a referenced 64 KiB `const` table whose checksum is shown on the
     boot screen. The real image (rodata) then crosses `0x08080000`.
   - Build, flash through the bootloader, and boot: checksum OK, audio and
     samples OK.
   - Reinstall samples and reboot: still OK.
   - Remove the table, flash the normal image, and confirm it boots.
6. **Normal regression pass:** Scene/Bank load and save, AutoSave, and an
   audio check.

### 7.3 Closeout documents

- `MEMORY.md`: the flash sector table and the "Erase floor: sector 6" rule →
  7; the Sample Flash Loading section and map; the layout comment for
  `sampleFlash.c`.
- `README.md:153`.
- `SCOPING_TARGETS.md` §5.5: record the resolution.
- `S072_ST1_IMPLEMENTATION.md` §21: note that finding 4 is resolved here.
- `DEV_MODES.md`: add the dump files and the memtest sub-knob if the tooling
  is kept.
- `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`: update the flash note.

---

## 8. If the bootloader fails the test

These are the growth paths already ranked in `S072_ST1_IMPLEMENTATION.md`
§21. None of them depends on the bootloader.

1. **`-Os` for cold control modules** (`menu.c`, `filesystem.c`,
   `presetManager.c`, `storageTypes.c`) through per-file Makefile rules.
   Likely several KiB. Needs UI and filesystem timing checks.
2. **Large constant tables as installed data.** `crashSample` (32 KiB), the
   three wavetables (66 KiB) and `transientData` (26 KiB) move into the
   sample region. They are installed from the card like samples. Up to about
   124 KiB. Needs an install path and a reserved span.
3. **Not recommended:** a "prepare for update" command that erases sector 6
   before each firmware update, which would work around a bootloader that
   does not erase. It only works while sector 6 holds data rather than code,
   and the unit is silent or glitchy until the update.

---

## 9. Decisions and results

### Decisions for you

| ID | Question | Recommendation |
|---|---|---|
| E1 | Does the PCB expose SWD, and do you have an ST-Link? | Check before Phase B. It turns the worst case from "unrecoverable" into "restore sector 0 from the dump". |
| E2 | Where does the bootloader dump live? | Local only, never committed. |
| E3 | Is 17.8 s → 14.8 s of sample memory acceptable? | Yes, if Gate B passes. It buys 256 KiB of program space. |
| E4 | If the bootloader has a limit between 480 and 736 KiB, is a partial gain worth a whole sample sector? | Decide from the measured limit. |

### Phase A record

| Item | Value |
|---|---|
| A0: samples survive a normal update? | |
| Flash size / bank mode | |
| OPTCR / OPTCR1 / OPTCR2 (raw, then decoded) | |
| Dump readable? | |
| Max accepted payload (from code) | |
| Erase strategy (from code) | |
| Buffering / read-back verify / reset-vector check | |
| Predicted Phase B outcome | |

### Phase B record

| Test | Date | Payload | Bootloader messages / time | Boots? | Verdict | OK / BK / AN / ST / OT | First bad | Notes |
|---|---|---|---|---|---|---|---|---|
| B0 | | — | — | | S6-11 blank? | — | — | |
| T1 | | 495,616 | | | | | | |
| T2 | | 753,664 | | | | | | |
| T3 | | 753,664 | | | | | | |
| T4 | | 483,048 | | | — | — | — | |
| T4r | | normal | | | S6 DATA/BLANK | — | — | |

---

## 10. Pre-implementation review (S073)

This section reviews the plan against the tree at `05bbd83` (clean) and the
current `build/lxr02.elf`. Each item needs a decision or a plan edit before
the tooling is written. Nothing here is implemented yet.

### 10.1 Stale or incorrect facts in this plan

| Where | Plan says | Current tree |
|---|---|---|
| §1, §4.5, T4 | Payload 483,048 B; 8,472 B free | 483,440 B; 8,080 B free (`_eflash_load` = `0x0807E070`) |
| §1 | `Reset_Handler` at `0x0805392C` | `0x08053AB0` (sector 5) |
| Header, §7.3, §8 | Cites `S072_ST1_IMPLEMENTATION.md` §21 | Now in `S073_SESSION_STARTUP.md` §2 and `072_SESSION_HANDOFF_LOG.md` §5. Drop the §7.3 edit if the ST1 file is deleted. |
| Startup §1; `MEMORY.md` Quick Start | HEAD `58569ae`; Steps 9–11 uncommitted | HEAD `05bbd83`; tree clean. Fix `MEMORY.md` at closeout. |
| §4.1 | "Sub-knob of the existing boot-time test `MEMTEST_ENABLED` (already 1)" | `memtest_run()` is commented out (`main.c:550`), so `MEMTEST_ENABLED 1` only compiles code that nothing calls. The new screens need their own call site, as §4.2 already implies. |
| §4.2 | "Next to the FxBf diagnostic" | `boot_showFxBufDiagnostic()` sits inside `#if DEV_MODE_DIAGNOSTIC` (`main.c:536–539`), which is 0. Put the new call after that block, under its own `#if`. |
| §5 A1 | Sectors 6–11 read DATA | Only sectors that hold sample bytes read DATA. With the `SD_CARD` fixture (about 416 KB of loops): S6–S7 DATA, S8–S10 BLANK, S11 DATA (index at the top). |
| memtest helpers | Reuse memtest's helpers | `app_highest_flash_addr()` ignores the `.itcm`/`.dtcm` load images. It reports `0x0807DE70`, which is 512 B short of the image end. Use `_eflash_load`, as §4.2 does; do not reuse that helper. |

### 10.2 Checked during review; these hold

- **`--gap-fill 0xFF` works.**
  - Test: a copy of the current ELF with a 4 KiB loadable section added at
    `0x08080000`, converted by `objcopy` 2.43.1.
  - Result: exactly 495,616 B (T1's size). The 8,080 B gap is all `0xFF`,
    and the first 483,440 B are byte-identical to `lxr02.bin`.
  - Without `--gap-fill`, the gap is `0x00`.
  - The NOLOAD `.bss` has a flash LMA (`0x0807DE70`, 292 KB long) that
    overlaps `PROBE`. It does not disturb the output.
- **Gate C3 holds** (an old install shows zero samples).
  `sampleMemory_refresh()` stops at the first invalid entry, and entry 0 is
  always at `SAMPLE_ROM_START_ADDRESS + 4`, for both fresh and append
  installs.
- **The floor is enforced only at** `SampleMemory.h:75`, `sampleFlash.c:170`,
  `:202`, `:217` and `memtest.c:136`.
  - Playback uses the absolute `SampleInfo.offset`.
  - The installer uses the address macros.
  - `sampleMemory_setNumSamples()`, which programs the count word at the
    floor, has no callers.
- **Boot-logging deadline.** The mount disarms its 10 s deadline when it
  completes, so CPU-only pauses between operations (the prompt, the erase)
  cannot trip it. The dump is its own operation with its own deadline.
- **`afatfs_fwrite()` copies synchronously** (`memcpy` into the sector
  buffer). Memory-mapped flash and a stack buffer are both valid sources.
- **A diagnostic build fits today.** `DEV_MODE_DIAGNOSTIC 1` links at
  485,508 B, leaving 6,012 B. This was built in a scratch copy; see §10.7.

### 10.3 Decisions needed before implementation

**E5. How is the probe image assembled?**

- **(a) As planned:** `flashProbeData.S` plus `.incbin`, and a `PROBE` region
  and section in the production linker script.
- **(b) On the host:**
  - compile T0 with the five `-D` values;
  - a script pads `lxr02.bin` with `0xFF` to `0x78000`, appends the probe
    words, and packs the result with `build_lxrv2_img.build()`;
  - the firmware verifies `0x08080000 … + FLASH_PROBE_BYTES` using the `-D`
    values.
- **Recommendation: (b).**
  - The Makefile has no `%.S` rule, only `%.s`, which is not preprocessed.
    `.incbin` would also need an explicit prerequisite on
    `build/flash_probe.bin`. Option (a) therefore needs new build rules.
  - Option (a) edits the production linker script, and Phase C must then
    undo it.
  - Option (b) leaves the linker script and compile rules untouched, and
    Phase C's "remove `PROBE`" step disappears.
  - Option (b) also allows a **verify-only T4r**: seed `0x33`, nothing
    appended. It reports, word for word, whether a normal image left
    sector 6 alone.
  - One target, `make probe-img TAG=… SEED=… BYTES=…`, passes the same values
    to the compile and to the script.

**E6. One dump file or two (`/s0dump.bin` and `/flashopt.bin`)?**

- **Recommendation: one file,** `/s0dump.bin`:
  - 32,768 B of sector 0;
  - then a 16 B trailer: `OPTCR`, `OPTCR1`, `OPTCR2`, and the flash-size
    register zero-extended.
- `filesystem_writeBootLog_tick()` handles exactly one file, so the dump
  stays a straight clone of it.
- Copy the registers into a 16 B local buffer on each tick. Never pass a
  peripheral address to `afatfs_fwrite()`: newlib's `memcpy` may use byte
  loads.

**E7. Where does the erase prompt run?**

- **Recommendation: before the SD block,** directly after the report
  screens:
  - it needs no card;
  - it stays outside the boot-logging window;
  - it runs even if the mount fails.
- Only the dump needs the mount.
- In either position it must come before `menu_setNumSamples()`
  (`main.c:598`).

**E8. Which tooling is kept after Phase C?**

- **Recommendation:**
  - keep the report screens and the dump behind `MEMTEST_FLASH_EXPANSION`
    (default 0), and document them in `DEV_MODES.md`;
  - remove the probe verifier, the erase prompt, and the probe build target.

**E9. Fix the Makefile default goal in the same edit?** (Startup §6.)

- **Recommendation: yes.** E5 (b) adds a Makefile target anyway, and the fix
  is one line: `.DEFAULT_GOAL := all`.

### 10.4 Additions to the implementation

- **K0, before A1: flash the known-good image through the bootloader and
  boot it.**
  - "Known-good" must mean an image that has booted on this unit, not just a
    build.
  - If samples are installed at that moment, this normal-size update also
    answers A0: check whether they still list and play. A1 then repeats the
    check with T0.
- **Production image gate.**
  - Keep every `main.c` edit inside `#if MEMTEST_FLASH_EXPANSION`.
  - With the knob at 0 and no probe variables, `lxr02.bin` should have the
    same SHA-256 as before the tooling landed.
  - If it differs, explain why before continuing. The `bss`/`data` gate
    remains the minimum.
- **Probe-image gate** (applies to either E5 option):
  - the `.img` length is 16 + `0x78000` + `BYTES`;
  - bytes `_eflash_load − 0x08008000` up to `0x78000` are all `0xFF`;
  - the first and last probe words match the generator.
- **Every photo identifies its image.**
  - T0 and the probe builds show `TAG`, `SEED`, `PREV_SEED` and the expected
    word at `0x08080000` on the count screen.
  - The generator prints the same word, which cross-checks the C and Python
    `probe_word()` on hardware.
- **Generator self-checks:**
  - assert that no word for the chosen seeds equals `0xFFFFFFFF` (that would
    be a false BK);
  - print how many T3 words satisfy `new & prev == new` (hidden AN words;
    about 6 expected).
- **Seeds.** `PREV_SEED = 0` means "none", so real seeds must be non-zero.
- **Classification order:** OK → BK → AN → ST → OT.
- **`#error` rules:**
  - `MEMTEST_FLASH_EXPANSION && !MEMTEST_ENABLED`;
  - `MEMTEST_FLASH_EXPANSION && !DEV_MODE_LOGGING` (already planned);
  - any `FLASH_PROBE_*` define without the knob.
- **B0 audio guard.** A probe build holds on `B0 NOT DONE` when
  `sampleMemory_getNumSamples() != 0`. See R5.
- **LCD during the erase.**
  - Call `lcd_waitForIdle()` before each blocking erase, so that
    `Erasing S6..S11` is on screen. The TIM7 LCD driver stops while
    interrupts are off.
  - Allow up to about 24 s: the datasheet maximum is 4 s per 256 KiB
    sector.
- **Option bytes.** No code path may write `FLASH_OPTKEYR` or `OPTCR*`. Make
  this an explicit review check on the diff.
- **Phase C details:**
  - `tools/link_budget.py` should read `__sample_flash_start` from the ELF,
    not hard-code a second copy of the limit.
  - `Reset_Handler` is `.weak` in `.text.Reset_Handler`
    (`startup_stm32f765xx.s:47`). The planned `KEEP` line therefore places it
    at `0x080081C8`, directly after the `0x1C8`-byte vector table.

### 10.5 Additions to the A3 checklist

- **File lookup.**
  - Does the bootloader match the long name `LXRV2_lxr02.img`, the 8.3 alias
    (`LXRV2_~1.IMG`), or the first `*.IMG` it finds?
  - Does it rename or delete the file after flashing?
  - See R1.
- **Order of operations.**
  - Does it check the magic and checksum *before* erasing?
  - If it erases first, a truncated read (the RAM-buffer case) leaves no
    app. That is still recoverable, but it changes what a T2 failure looks
    like.
- **Size source:** the header's size field, or the FAT file size?
- **Destination-address arithmetic.**
  - Look for masks, modulo or wrap on the write address (for example
    `& 0x7FFFF`).
  - Extend Gate A's stop rule to any path that can produce a destination
    below `0x08008000`.
- **Watchdog.**
  - Look for writes to `IWDG_KR` (`0x40003000`: `0xCCCC`, `0xAAAA`,
    `0x5555`) and for time budgets in the programming loop.
  - A 736 KiB image takes about 1.5× today's write time.
  - Partial evidence against a watchdog on the normal boot path: the
    `DEV_LOGGING_IWDG` work found the LSI not running at app start.
- **Jump checks.** Record the exact SP/PC validation masks. SP `0x20080000`
  passes whatever check exists today.

### 10.6 Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Wrong image flashed.** Tagged archive copies in the card root can take the `~1` 8.3 alias. macOS writes `._LXRV2_lxr02.img` (AppleDouble), which also ends in `.img`. A bootloader that matches the 8.3 alias or the first `*.IMG` could flash the wrong image, and the results would be attributed to the wrong test. | Keep exactly one `.img` in the card root, and archive copies on the computer only. Remove `._*` files after each copy (`dot_clean`, or delete them by hand). Record each image's SHA-256. The on-screen `TAG` proves which image ran. |
| R2 | **A bootloader watchdog or timeout fires during the longer write**, leaving a half-programmed app. | Recover by holding the encoder with the known-good image. A3 looks for IWDG writes. |
| R3 | **Address wrap in the bootloader.** The overflow data is written over the start of the app. For T1 that is the vector table: the app does not boot, which is recoverable. In the worst case it is written toward sector 0. | A3 checks the address arithmetic. T1's minimal 4 KiB overflow limits the damage. E1 (SWD) is the only recovery for sector 0. |
| R4 | **PCROP on sector 0.** A D-bus read of a PCROP sector sets `RDERR`. Check in RM0410 whether it also faults. | The report screens (with `OPTCR2`) run before the dump, so photograph them first. If T0 hangs at the dump, re-flash the known-good image and treat the result as PCROP. |
| R5 | **Probe data played as audio.** If B0 is skipped, the old index in S11 still points into S6. The probe builds overwrite S6 with pseudo-random words, which would play at full scale. | The `B0 NOT DONE` hold (§10.4). Keep the volume low throughout Phase B. |
| R6 | **Rolling back past Phase C.** A pre-C sample install, then a grown Phase C image (code in S6), then a pre-C image without reinstalling: the old firmware accepts the old index and plays program code as audio. The other two directions are safe. A pre-C install under Phase C shows 0 samples. A Phase C install under pre-C firmware plays correctly. | Phase C already reinstalls samples (C4) before the growth drill (C5). Rule: never roll back past Phase C onto a pre-C sample install without first running Load:[Samples] at low volume. |
| R7 | **Accidental commit of the vendor binary.** `*.bin` is ignored, but `*.dis` is not. `!build/*.img` un-ignores images, so tagged probe images in `build/` appear as untracked files. | Keep `s0dump.*` and its disassembly outside the repository. Write probe images outside `build/`, or add an ignore rule for them. |

### 10.7 Cross-plan considerations (CPU refactor)

- **Diagnostic-build headroom.**
  - CPU Step 0's profiler (D1) exists only in `DEV_MODE_DIAGNOSTIC` builds,
    which have 6,012 B free today.
  - CPU Steps 1 and 5 (about +1.7 KB), plus the profiler and its widget,
    would leave about 3 KB in diagnostic builds if Gate B fails.
  - Keep the session order (flash plan first). If Gate B fails, apply §8
    fallback 1 (`-Os` for cold modules) before the CPU plan.
- **Take the CPU baseline profile on the post-Phase-C image.**
  - Phase C moves `Reset_Handler` and re-lays out `.text`.
  - Wait states are the same in every flash sector, but alignment and
    I-cache effects can shift cycle counts slightly.
  - A baseline taken before Phase C would blame that shift on Step 1.
- **The two plans touch disjoint files.**
  - Phase C: the linker script, `SampleMemory`, `sampleFlash`, `memtest`,
    and tools.
  - CPU steps: the DSP sources.
  - They share the Makefile only if E5 is (a).

### 10.8 Open questions for you

1. **A0.** Do you already know whether installed samples survive a normal
   firmware update? If not, K0 and A1 answer it.
2. **Factory image.** Do you have Erica's factory LXR-02 `.img`? Its header
   size field (bytes 8–11), and any sample-flash addresses in its code, are
   free evidence for Q1 and Q2. They also show the sector plan the
   bootloader was designed around.
3. **E1.** Does the PCB have SWD pads, and do you have an ST-Link?
4. **E5–E9** in §10.3.

---

## 11. Implementation record (S073)

### 11.1 Decision and evidence

- **Phases A and B were skipped by user decision.** The goal went straight
  to Phase C: build with `make clean` + `make img` and test on the device.
- **A0 is answered by the user:** installed samples generally survive a
  normal firmware update. So the bootloader does not erase sectors 6–11 for
  a normal-size image.
- **Factory image (`LXRV2_update_v1.70.img`):**
  - payload 275,832 B (ends at `0x0804B578`);
  - SP `0x20080000`;
  - no `FLASH_KEYR`, `FLASH_CR` or key literals, so it has no flash writing
    and no sample storage.
  - It gives no evidence about payloads over 480 KiB. Our 483 KB images
    are the largest the bootloader has handled.
  - Keep it out of commits: `*.img` is not git-ignored.
- **Consequence:** it is still unknown whether the bootloader erases by
  size or always erases a fixed range (sectors 1–5). The first image that
  reaches sector 6 settles it. §11.2 item 2 makes that result visible on
  the device.

### 11.2 What changed

1. **Phase C as planned (§7.1).**
   - Linker: `FLASH` is 736 KiB, and `__sample_flash_start = 0x080C0000`
     has ASSERTs on `_etext`, `_eflash_load` and the region end.
     `Reset_Handler` is `KEEP`-first in `.text` (now `0x080081E8`), with an
     ASSERT that it stays in sector 1.
   - `SampleMemory.h`: `SAMPLE_ROM_START_ADDRESS 0x080C0000` and
     `SAMPLE_FIRST_SECTOR 7u`.
   - `sampleFlash.c`:
     - the floor is now `SAMPLE_FIRST_SECTOR`;
     - `_Static_assert`s tie the address to the sector;
     - a runtime interlock refuses every erase and write if the macro and
       the linker symbol differ.
   - `memtest.c/h`: the floor, the labels, and the scope check (now
     sector 6).
   - `filesystem.c`: the "five sectors" comment.
   - `link_budget.py` reads the limit from `__sample_flash_start`.
2. **New: boot-time image check** (`Core/Hardware/flashImage.c/h`), in
   production.
   - A 32-byte `.image_check` block sits in sector 1, directly after the
     vector table. It holds the magic `IMCK`, a CRC32 for each of sectors
     1–6 (skipping the block itself), and the image length.
   - `tools/stamp_image_check.py` stamps it into `lxr02.bin` in the
     Makefile `.bin` rule. It deletes the `.bin` if the layout is wrong.
   - At boot, after `din_init()`/`time_initTimer()`, the firmware recomputes
     the CRCs (about 20 ms, no RAM).
   - On a mismatch it shows `Img BAD s:<bad sectors>` / `Reflash. BAR1=go`,
     or `Img unstamped`, and waits for a BAR1 press. A checker fault can
     therefore never brick a good image.
   - The block is in sector 1, which every update rewrites, so a bad
     sector 6 cannot corrupt the words that report it.
3. **New: `FLASH_GROWTH_DRILL_KB`** in `config.h` (default 0). A nonzero
   value links a constant table of that many KiB and shows
   `Img OK <end>` / `drill <addr>` for 3 s when the check passes.
4. **Makefile:** `flashImage.c` is added to `SRCS`; the linker script is now
   a prerequisite of the ELF; the stamp step is added.
5. **Comments:** the linker stack comment is corrected (SRAM2).

### 11.3 Gates run on the host

| Gate | Result |
|---|---|
| Clean build (`make clean`, `make img`) | Links; no new warnings. `text=483,744` (+720 over S072), `data=416`, `bss=426,336` unchanged. ITCM 3,768 B, DTCM statics 4,448 B and FXBUF 126,624 B unchanged. |
| Link budget | 484,160 / 753,664 B; headroom **269,504 B** |
| Image | `LXRV2_lxr02.img`: 484,176 B, checksum OK, header size = `.bin` = `_eflash_load − origin`; reset vector `0x080081E9`. SHA-256 `71612518d14c…11ecd`. |
| Check code vs stamp | `flashImage.c`'s own CRC and sector code, compiled on the host and run on the `.bin`, matches all six stamped words. |
| Negative cases (host) | One flipped byte in S5 → `s:....5.`. Placeholder magic → `Img unstamped`. |
| 64 KiB drill (scratch build) | Links; image ends at `0x0808E3E0` (sector 6); `_etext` `0x0808D188` (code/rodata in sector 6); table at `0x08056324`; `Reset_Handler` still `0x080081E8`; passes. |
| Drill, S6 programmed without erase (simulated AND with old data) | `Img BAD s:.....6` |
| Drill, truncated at 480 KiB (simulated) | `Img BAD s:.....6` |
| Gate C3 logic | `sampleMemory_refresh()` stops at entry 0 (`0x08080004` < new floor), so an old install shows 0 samples. |

Not run: the §7.2 C2 one-off hardware floor call (the guards are compile-time
and interlock-checked), and anything on hardware.

### 11.4 Hardware test (you)

1. **Card:** keep exactly one `.img` in the card root. Delete any `._*` files
   macOS created. Copy `build/LXRV2_lxr02.img` and flash it as usual
   (hold the encoder at power-on).
2. **Boot.**
   - Expect a normal boot; the check is silent when it passes.
   - `Img BAD s:…` or `Img unstamped`: note the text exactly, then press
     BAR1 to continue.
3. **At low volume:** the old sample install should show 0 samples, and
   sample waveforms should be silent (Gate C3).
4. **Load:[Samples]:**
   - it erases sectors 7–11 (five sectors now);
   - samples and loops install and play;
   - bytes free is about 1,308,320 B minus what was installed.
5. **Regression:**
   - Scene/Bank load and save, AutoSave restore, and an audio check;
   - the Phase 5 FX checks from `S073_SESSION_STARTUP.md` §3.
6. **Optional: the growth drill.** This is the first image over 480 KiB the
   bootloader will ever see, and the only step that tests the bootloader.
   1. Set `FLASH_GROWTH_DRILL_KB 64`, then `make clean` + `make img`, and
      flash.
      - Sector 6 still holds old sample data, because the new firmware
        never erases it. The drill therefore also tests whether the
        bootloader erases sector 6.
   2. **Pass:** `Img OK  0808E3E0` / `drill   08056324` for 3 s, then a
      normal boot.
   3. **Fail:** `Img BAD s:.....6`, or a hang before any screen. Reflash the
      normal image, which does not use sector 6.
   4. Either way, set the knob back to 0 and reflash the normal image.

### 11.5 Hardware results

| Step | Date | Result | Notes |
|---|---|---|---|
| 1–2 Boot / image check | 2026-09-28 | **PASS** | The S073 image loads through the bootloader and boots. No image-check screen was reported. |
| 3 Old install → 0 samples | 2026-09-28 | Not reported | |
| 4 Load:[Samples] | 2026-09-28 | **PASS** (after fix) | At first the item was missing from the Load menu: `SAVE_TYPE_SAMPLES` had not been in `menu_loadSaveLoadTypes[]` since July (`a62221f`). Restored in `S073_POST_FLASH_MENU_BUGFIXES.md` §2. Sample loading now works, which is the first install at the sector-7 floor (Gate C4, functional). |
| 5 Regression | 2026-09-28 | **FAIL** | Switching between Load menu types takes several seconds. The screen is often blank for many seconds while the `.hcindex` loads. The cause is not yet determined, and it is not yet known whether S073 or an earlier change introduced it. |
| 6 Growth drill | — | Not run | The user did not ask for this step. The `FLASH_GROWTH_DRILL_KB` knob was added without a request and is scheduled for removal. |

Both defects are handled in `S073_POST_FLASH_MENU_BUGFIXES.md`.
