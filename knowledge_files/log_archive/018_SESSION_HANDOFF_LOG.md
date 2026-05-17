# Session 018 Handoff Log - Sample Flash Loading

```
DATE: 2026-05-12
SESSION GOAL: Implement sample loading and use: read user WAV samples from SD, install them into reserved flash, and use them in voices in the same general way as original LXR.
COMPLETED: Linker sample-region reserve; guarded F765 flash primitives; real SampleMemory runtime/cache/install API; modal /samples full installer; modal /loops append-loop installer; sample waveform menu integration; filename display names; LFN sorting; audio suspend/resume around flash writes; boot-time sample metadata refresh.
VERIFIED ON HARDWARE: Yes for normal Load:[Samples ] sample install/playback and audio restart after sample load. User reported sample load works and sounds good; initial audio resume failure was fixed and user confirmed audio restart works. Latest /loops append, LFN abbreviation/order, and long-sample-size caveat still need hardware soak.

CHANGES THIS SESSION:
- STM32F765VIHx_FLASH.ld: app flash capped before sector 6; sectors 6-11 reserved for sample storage; linker assertions protect against app/sample overlap.
- Makefile: added SampleRom/sampleFlash.c.
- main.c: initializes SampleMemory before menu/audio use and seeds menu sample count from flash metadata at boot.
- Core/SampleRom/sampleFlash.c/h: F765 bare-register flash erase/program helper for sample sectors only, with erase floor, write bounds, verify, and cache invalidation.
- Core/SampleRom/SampleMemory.c/h: replaced no-op stub with 120-entry metadata cache, display-name table, loop flags, full reinstall and append install helpers, guarded silence fallback, and 32-bit SampleInfo.size.
- Core/Hardware/SD/filesystem.c/h: added blocking modal sample installers for /samples and /loops; WAV validation; manifest scan/sort; LFN decode; 8-char display-name generation; append-loop fitting/skipping.
- Core/Menu/menu.c/h: wired Load:[Samples ] and Load:[SampLoop]; suspends audio around install; waveform names are s01-s99 then sA0 upward; full parameter display shows filename-derived 8-char name.
- Core/Hardware/AudioCodecManager.c/h: added audioCodec_suspend()/audioCodec_resume(); final version fully stops/resets/restarts DMA, I2S, and PLLI2S so audio returns after flash writes.
- Core/DSPAudio/Oscillator.c: user-sample playback reads installed SampleInfo, honors loop flag, and wraps looped samples.
- AUDIT-SAMPLES.md: planning audit plus final Session 018 implementation status and long-sample warning.
- README.md, MEMORY.md: updated project state, SampleRom status, sample flash map, known issues, and repository/reference boundary.
- knowledge_files/log_archive/000_SESSION_INDEX.md: append Session 018 summary and cross-session facts.

KNOWN ISSUES INTRODUCED: Long-sample playback is not 32-bit clean despite uint32_t metadata. LFN support is minimal ASCII-only with non-ASCII mapped to '_'. /loops append skips unsupported/oversize files silently by design. Sample install is intentionally blocking/modal.
KNOWN ISSUES RESOLVED: SampleMemory no-op stub; unreserved sample flash region; Load:[Samples] no-op; audio not resuming after sample flash writes.

NEXT SESSION RECOMMENDED GOAL: Session 019 - MIDI and clock sync.
BLOCKERS: Hardware retest for Load:[SampLoop], LFN abbreviation/order, and any large-sample edge cases. Long-sample oscillator phase/index spike must happen before claiming long-sample support.

CRITICAL REMINDERS FOR NEXT SESSION:
- !!! LONG SAMPLE PLAYBACK IS NOT FINISHED !!! SampleInfo.size is uint32_t, but Oscillator.c still indexes with the legacy phase >> 17 path. Practical direct addressing is still around 32768 samples. Metadata is wider; playback math is not.
- This folder is the repository/codebase. knowledge_files/LXR-master is reference-only and must not be modified. Session logs under knowledge_files/log_archive are the allowed knowledge_files changes.
- Application flash must remain capped at 0x0807FFFF. Sectors 6-11, 0x08080000-0x081FFFFF, are sample storage.
- sampleFlash.c must never erase below sector 6.
- Load:[Samples ] wipes/reinstalls from /samples. Load:[SampLoop] appends looped samples from /loops and preserves existing normal samples.
- Sample install is modal by design: stop sequencer, suspend audio, wait briefly, write flash, refresh SampleMemory/menu, resume audio.
- afatfs_poll() remains one-context-only.
- Next session is MIDI and clock sync; TIM2 is still reserved for CLK IN BPM measurement and MIDI RX timestamping.
```

## Session Narrative

This session started as an implementation audit against the original LXR sample path. The original split system had the AVR menu ask the STM32 mainboard to upload samples; the mainboard stopped sequencing, scanned `/samples`, erased sample flash, wrote WAV payloads, committed a `SampleInfo` table, and reported a count back to the front panel. The port kept the important behavioral shape: sample install is not an async user-flow operation. It is explicitly modal and audio is stopped because the STM32F765 is single-bank flash and erase/program stalls instruction fetches from flash.

The initial audit was written in `AUDIT-SAMPLES.md`. It identified the main risks:

- The linker still allowed the app image to grow into the planned sample region.
- `SampleMemory.c` was only a no-op stub.
- Real flash erase/program needed hard sector guards, D-cache invalidation, and interrupt handling.
- SD sample scanning had to live behind the existing `filesystem.c/h` facade.
- Audio had to be suspended around flash writes rather than trying to keep rendering.
- The original oscillator path already existed, but any larger-than-original sample support needed careful review.

The user then made the architecture decisions:

- Use canonical FAT directory ordering initially, then later changed to sorted filename order.
- Silently skip unsupported files.
- Accept only mono PCM 16-bit 44.1kHz WAV.
- Full `Load:[Samples ]` install should erase all dedicated sample sectors.
- Widen `SampleInfo.size` to 32 bits, but defer true long-sample playback until after the first implementation works.
- Build the audio suspend/init framework around sample load and test whether audio returns.
- Keep samples in flash, not SRAM/ITCM.
- Stop/suspend everything for flash writes. Sample loading does not need to be async.
- Follow the original blind wipe/reinstall model rather than tracking install state.

## Implemented Shape

### Flash Map

`STM32F765VIHx_FLASH.ld` now reserves sectors 6-11 for sample storage by ending application flash before sector 6:

| Region | Range | Purpose |
|--------|-------|---------|
| Bootloader | `0x08000000-0x08007FFF` | LXRV2 bootloader |
| Application | `0x08008000-0x0807FFFF` | firmware image |
| Sample storage | `0x08080000-0x081FFFFF` | user sample payload, metadata, names |

The Session 018 build after implementation produced:

- `text`: 277360 bytes
- `data`: 332 bytes
- `bss`: 95948 bytes
- Packaged image payload: 277692 bytes

With the app region capped at `0x78000` bytes from `0x08008000`, the current payload leaves roughly 213 KB of reserved app-flash headroom before the sample region. The sample region remains about 1.5 MB total before metadata/name tables.

Current sample top-of-flash layout:

| Item | Address / size |
|------|----------------|
| Sample start | `0x08080000` |
| `SAMPLE_MAX_COUNT` | 120 |
| `SampleInfo[120]` size | 1440 bytes |
| Display names size | 960 bytes |
| `SAMPLE_INFO_START_ADDRESS` | `0x081FF6A0` |
| `SAMPLE_NAME_START_ADDRESS` | `0x081FFC40` |
| Top of flash | `0x08200000` |

### Flash Writer

`Core/SampleRom/sampleFlash.c/h` owns the F765 sample flash operations. It is intentionally small and conservative:

- Unlocks/locks F765 flash registers.
- Erases only sectors 6-11.
- Rejects writes outside `0x08080000-0x081FFFFF`.
- Verifies programmed words.
- Invalidates D-cache after erase/program ranges.
- Does not touch option bytes.

This code should remain sample-specific unless a broader flash subsystem is intentionally designed later.

### SampleMemory

`Core/SampleRom/SampleMemory.c/h` is now a real runtime layer instead of a stub.

Key facts:

- `SAMPLE_MAX_COUNT` is 120.
- `SampleInfo.size` is `uint32_t`.
- `SAMPLE_INFO_LOOP_FLAG` is the high bit of `size`.
- `SAMPLE_INFO_SIZE_MASK` leaves 31 bits for frame count.
- `sampleMemory_refresh()` scans contiguous valid metadata entries from flash.
- Invalid metadata returns a small silence sample, not a raw flash pointer.
- The old count word at the start of sample flash is no longer trusted as the active table authority; scanning valid metadata is more robust for append behavior.
- Display names are stored separately from the original 3-char `SampleInfo.name`.
- Append install copies existing entries into a staging cache, finds the end of payload data, and writes only blank new table/name slots.

The append behavior matters because flash can only program 1 bits to 0 bits without erase. The `/loops` path preserves existing entries and writes new metadata/name entries into still-erased slots.

### SD Installer

The sample installer lives in `Core/Hardware/SD/filesystem.c/h`, not directly in menu code and not in raw SD/SPI code. It uses asyncfatfs synchronously inside the modal operation by polling until each open/read/seek/close step completes.

Public entry points:

- `filesystem_installSamplesBlocking()`
- `filesystem_installLoopsBlocking()`

Behavior:

- `/samples`: full wipe/reinstall.
- `/loops`: append looped samples to the same waveform list.
- Accepts `.wav`/`.WAV` names.
- Parses RIFF/WAVE.
- Requires `fmt` chunk to be PCM, mono, 44100 Hz, 16-bit.
- Requires a non-empty even-sized `data` chunk.
- Unsupported files are skipped silently.
- Files are sorted by the full long filename when LFN data exists; the sort is lexicographic with ASCII case folded, not natural numeric sort.
- Non-ASCII LFN characters decode as `_`.
- The actual file open still uses the FAT 8.3 filename, because the current asyncfatfs open path operates on short names.

Filename display rules:

- Display names preserve case.
- If the filename stem is 8 characters or shorter, the stem is padded to 8 chars.
- If longer than 8 characters, the display name is `first4 + 0x00 + last3`.
- `0x00` is the LCD CGRAM ellipsis character that the user defined.
- This applies to both `/samples` and `/loops`.

### Menu Integration

The Load page now has:

- `Load:[Samples ]`
- `Load:[SampLoop]`

The sample load menu path:

1. Stops sequencing.
2. Calls `audioCodec_suspend()`.
3. Waits 1 second.
4. Runs the blocking sample install.
5. Calls `sampleMemory_refresh()` / updates `menu_numSamples`.
6. Calls `audioCodec_resume()`.
7. Repaints the menu.

Compact sample waveform names are generated as:

- `s01` through `s99`
- `sA0`, `sA1`, ... upward after that

The full parameter display now shows the 8-character filename-derived display name for the current sample waveform while keeping compact labels for normal menu scanning.

### Audio Suspend/Resume

The first audio restart attempt did not bring audio back after sample load and required reboot. The final implementation fixed this by making suspend/resume more complete:

- Stop DMA streams.
- Disable/reset I2S paths.
- Reset relevant audio peripheral state.
- Stop/restart PLLI2S as part of the resume sequence.
- Reinitialize GPIO/DMA/I2S cleanly.

The user confirmed audio restart works after sample loading.

### Oscillator Use

`Core/DSPAudio/Oscillator.c` now:

- Reads installed user sample metadata from `sampleMemory_getSampleInfo()`.
- Checks `sampleMemory_isLooped()`.
- Wraps looped samples.
- Leaves one-shot samples in the original stop-at-end shape.

This gives working sample use, but does not complete long-sample playback.

## Major Caveat: Long Samples

!!! FLASHING SIGN FOR FUTURE SESSION !!!

Do not forget this:

`SampleInfo.size` is now 32-bit, and code that stores/validates/returns sample metadata treats size as 32-bit. That does not mean playback can address arbitrarily long samples yet.

The oscillator still derives the direct sample index from the legacy 32-bit phase path:

```c
itg = oscPhase >> 17;
```

That gives roughly 32768 directly addressable sample positions in the current playback math. Larger metadata may be present, and looped samples may appear to "work" in some sense, but true long-sample playback requires a separate oscillator phase/index design and hardware validation.

Recommended later spike:

- Audit both user-sample oscillator paths in `Oscillator.c`.
- Decide whether to use fixed-point phase with wider integer part, explicit sample cursor, or a per-sample increment model.
- Preserve pitch behavior for short samples.
- Check interpolation boundary rules for one-shot and looped playback.
- Confirm with >32768-frame samples from both `/samples` and `/loops`.

## Hardware Verification During Session

Confirmed by user:

- Normal sample load works.
- Installed samples sound good.
- Audio restart failed initially.
- After the audio resume rewrite, audio restart works.

Needs hardware retest:

- `Load:[SampLoop]` append from `/loops`.
- Preservation of normal `/samples` entries after `/loops` append.
- Long filename display abbreviation on the full parameter display.
- Whole-filename lexicographic sort behavior with long names.
- Edge cases near sample count / flash capacity.

## Architectural Notes And Risks

### Modal Flash Writes Are Intentional

The F765 here is single-bank flash. Erase/program operations stall flash reads. The audio/DSP code is not fully executing from ITCM/RAM and should not be expected to keep running during sample writes. Keeping sample install modal is the right repo-current behavior.

### Audio Restart Is Sensitive

The working restart path now fully tears down and brings back audio. Be careful when touching `AudioCodecManager.c`; partial suspend/resume can leave I2S/DMA/PLLI2S in a state that compiles but requires a reboot after flash writes.

### LFN Support Is Purpose-Built

The LFN decoder is just enough for sample scan display/sort. It is ASCII-oriented and currently supports names up to the configured buffer length. Do not assume it is a complete generic long-filename API for the rest of the filesystem.

### Append Install Depends On Blank Slots

`Load:[SampLoop]` appends by writing new data and new metadata/name slots. It intentionally does not erase. Existing metadata slots cannot be rewritten in-place. If table state ever becomes non-contiguous or corrupted, the append path may stop earlier than expected because `sampleMemory_refresh()` scans contiguous valid entries.

### Sample Count Labels

The user requested up to 120 samples and labels after `s99` as `sA0` to `sB9`. That range only adds 20 labels, which reaches 119 total if starting from s01. The current label generator continues upward (`sC0` as needed) to cover all 120 entries rather than reducing the sample limit.

## Files Worth Reading First Next Time

- `knowledge_files/log_archive/018_SESSION_HANDOFF_LOG.md`
- `AUDIT-SAMPLES.md`
- `STM32F765VIHx_FLASH.ld`
- `Core/SampleRom/SampleMemory.h`
- `Core/SampleRom/SampleMemory.c`
- `Core/SampleRom/sampleFlash.c`
- `Core/Hardware/SD/filesystem.c` around the sample installer section
- `Core/Menu/menu.c` around sample load and waveform display
- `Core/Hardware/AudioCodecManager.c`
- `Core/DSPAudio/Oscillator.c` around user sample playback

## End Of Session State

The sample-loading feature is usable in first-implementation form. The normal `/samples` path has been hardware validated by the user; audio resume after loading has been fixed and hardware confirmed. The `/loops` append path and LFN display/order refinements build, but should be retested on hardware.

Next session should move to MIDI and clock sync as requested. Do not let the sample long-playback caveat vanish from memory; it is the main unfinished sample-system issue.
