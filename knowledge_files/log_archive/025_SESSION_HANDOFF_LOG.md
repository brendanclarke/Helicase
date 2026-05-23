# Session 025 Handoff Log

## How to start a new session

**Project**: LXR-02 firmware port (STM32F765VIH6)

**Session goal**: Continue from Session 025 after SD/FAT compatibility, globals/.all load-policy cleanup, and CLK/RST/jack-detect stabilization.

**Last session summary**:
```
DATE: 2026-05-23
SESSION GOAL: Fix load/save reported issues with ALL/SD card handling; add unsupported-card warning; implement safe globals compatibility; debug CLK/RST and OUT jack detect.
COMPLETED: Added unsupported-card boot warning for FAT12/exFAT; implemented raw/unversioned 22-byte legacy, 23-byte current, and stale-fallback globals policies for glo.cfg and ALL; removed obsolete PAR_FETCH and phantom hardware-control params; corrected CLK IN/RST IN to PD4/PD5 rising-edge inputs; moved PD6/PD7 jack detect to retained 500Hz foreground polling with pull-ups; updated README, MEMORY, hardware docs, audits, and session index.
VERIFIED ON HARDWARE: Partially yes. User testing verified CLK IN and RST IN read and trigger correctly after PD4/PD5 pull-up/rising-edge config, and PD6/PD7 jack detect checks out after pull-ups and retained polling. Final code build was verified with make -j4 && make img. SD/globals file edge cases still deserve a structured regression pass with actual FAT12/exFAT/legacy/current/stale files.

CHANGES THIS SESSION:
- Core/Hardware/SD/filesystem.c/h: unsupported card detection; boot unsupported-card flag; globals length gate/fallback/warning state; legacy 22-byte compatibility; current 23-byte globals load; stale .glo/.all safe fallback; global MIDI channel sanitize; PERF v1 meta handling fix.
- main.c: boot-time Unsupported card / use MBR-FAT32 modal warning held for 5 seconds.
- Core/Preset/ParameterArray.h: removed PAR_FETCH and phantom PAR_QUAD_ENC*/PAR_SLIDER_RV* entries; documented raw globals span hazards; final NUM_PARAMS=275.
- Core/Menu/menu.c and Core/Menu/menuPages.h: stale-settings warning display/defer path; global menu condensation; manual warning about TEXT_EMPTY/PAR_NONE blocking later globals pages.
- Core/Sequencer/sequencer.c/h: seq_resetToPatternStart() for rising-edge RST IN without transport toggle.
- Core/Hardware/triggerJacks.c: PD4 CLK IN and PD5 RST IN input pull-ups, rising-edge EXTI; PD6/PD7 input pull-ups with EXTI masked; comments documenting current hardware behavior.
- Core/Hardware/timebase.c: foreground 500Hz retained-state jack-detect sampling for PD6/PD7/PB4/PB6.
- Core/DSPAudio/mixer.c: comments updated for retained jack-detect state consumption.
- Core/compat/stm32f4xx.h: shim comment updated for current jack-detect ownership.
- README.md, MEMORY.md, FAT_AUDIT.md, SAVE_ALL_AUDIT.md, ST1_JACK_DET_AUDIT.md, knowledge_files/hardware_archive/*.md, knowledge_files/log_archive/000_SESSION_INDEX.md: current behavior and handoff context updated.

KNOWN ISSUES INTRODUCED: None known. Build products and audit files are dirty/untracked in the working tree by design for this session.
KNOWN ISSUES RESOLVED: Unsupported FAT12/exFAT cards no longer silently fail as generic load problems; bad/stale globals no longer blindly apply shifted values; legacy 22-byte globals load safely; global MIDI channel values outside 1..16 are sanitized; PERF v1 BPM load no longer seeks phantom padding; CLK/RST pins and OUT1 jack detect behavior corrected.

NEXT SESSION RECOMMENDED GOAL: Structured regression pass on SD/file behavior with actual cards/files, plus a compact hardware routing matrix for OUT1/OUT2 jack-detect audio behavior.
BLOCKERS: Requires hardware tests with FAT12, exFAT, MBR-FAT32, 22-byte legacy glo.cfg/ALL, 23-byte current glo.cfg/ALL, and intentionally stale globals lengths.

CRITICAL REMINDERS FOR NEXT SESSION:
- FAT16/FAT32 supported; FAT12/exFAT unsupported. Unsupported-card screen is Unsupported card / use MBR-FAT32 for 5 seconds, and the filesystem should not mount/load from that card.
- glo.cfg and ALL globals remain raw/unversioned. Do not add versioning unless the user explicitly changes that decision.
- Globals policy is only 22 bytes legacy, 23 bytes current, or stale fallback/warning. Any other length must not apply shifted/stale values.
- 22-byte legacy globals load silently, then force PAR_EXT_SYNC=auto and PAR_OSC_WAVE_INTERP=1.
- Stale globals fallback loads only the safe prefix through PAR_PRESCALER_CLOCK_OUT1, then forces PAR_EXT_SYNC=auto, PAR_BAR_RESET_MODE=0, PAR_MIDI_CHAN_GLOBAL=1, PAR_OSC_WAVE_INTERP=1.
- ALL stale warning must appear after the pattern/loading UI finishes, not while it is still loading.
- menuPages.h globals rows are fragile: early TEXT_EMPTY/PAR_NONE can block later entries. Reordered globals need manual verification.
- CLK IN is PD4 rising edge with GPIO input pull-up. RST IN is PD5 rising edge with GPIO input pull-up. Do not revert to falling edge or active-low level-gate assumptions.
- RST IN is an edge reset-to-pattern-start, not a run/stop gate.
- PD6/PD7 are OUT1 L/R jack detect, use pull-ups, are retained state sampled at 500Hz with PB4/PB6, and should not fire EXTI.
```

**Working repository**: `/Users/bc/LXR02Open/LXR02-current/lxr02-037_port` on branch `LXR02Open-prime` if git metadata is available. The tree is dirty from session source/docs/build outputs and untracked audit/support files.

**Constraints today**: Treat `knowledge_files/LXR-master/` as read-only. Preserve the no-versioning decision for globals unless explicitly reversed. Trust hardware observations over older background notes for CLK/RST and jack detect.

Key files to be aware of:
- `README.md`
- `MEMORY.md`
- `FAT_AUDIT.md`
- `SAVE_ALL_AUDIT.md`
- `ST1_JACK_DET_AUDIT.md`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/triggerJacks.c`
- `Core/Hardware/timebase.c`
- `Core/DSPAudio/mixer.c`
- `Core/Menu/menu.c`
- `Core/Menu/menuPages.h`
- `Core/Preset/ParameterArray.h`
- `Core/Sequencer/sequencer.c`
- `knowledge_files/hardware_archive/HARDWARE_MAP.md`
- `knowledge_files/hardware_archive/XP_CONNECTOR_MAPS.md`
- `knowledge_files/log_archive/000_SESSION_INDEX.md`

---

## End of session block

```
DATE: 2026-05-23
SESSION GOAL: Fix load/save reported issues with ALL/SD card handling; add unsupported-card warning; implement safe globals compatibility; debug CLK/RST and OUT jack detect.
COMPLETED: Added unsupported-card boot warning for FAT12/exFAT; implemented raw/unversioned 22-byte legacy, 23-byte current, and stale-fallback globals policies for glo.cfg and ALL; removed obsolete PAR_FETCH and phantom hardware-control params; corrected CLK IN/RST IN to PD4/PD5 rising-edge inputs; moved PD6/PD7 jack detect to retained 500Hz foreground polling with pull-ups; updated README, MEMORY, hardware docs, audits, and session index.
VERIFIED ON HARDWARE: Partially yes. User testing verified CLK IN and RST IN read and trigger correctly after PD4/PD5 pull-up/rising-edge config, and PD6/PD7 jack detect checks out after pull-ups and retained polling. Final code build was verified with make -j4 && make img. SD/globals file edge cases still deserve a structured regression pass with actual FAT12/exFAT/legacy/current/stale files.

CHANGES THIS SESSION:
- Core/Hardware/SD/filesystem.c/h: unsupported card detection; boot unsupported-card flag; globals length gate/fallback/warning state; legacy 22-byte compatibility; current 23-byte globals load; stale .glo/.all safe fallback; global MIDI channel sanitize; PERF v1 meta handling fix.
- main.c: boot-time Unsupported card / use MBR-FAT32 modal warning held for 5 seconds.
- Core/Preset/ParameterArray.h: removed PAR_FETCH and phantom PAR_QUAD_ENC*/PAR_SLIDER_RV* entries; documented raw globals span hazards; final NUM_PARAMS=275.
- Core/Menu/menu.c and Core/Menu/menuPages.h: stale-settings warning display/defer path; global menu condensation; manual warning about TEXT_EMPTY/PAR_NONE blocking later globals pages.
- Core/Sequencer/sequencer.c/h: seq_resetToPatternStart() for rising-edge RST IN without transport toggle.
- Core/Hardware/triggerJacks.c: PD4 CLK IN and PD5 RST IN input pull-ups, rising-edge EXTI; PD6/PD7 input pull-ups with EXTI masked; comments documenting current hardware behavior.
- Core/Hardware/timebase.c: foreground 500Hz retained-state jack-detect sampling for PD6/PD7/PB4/PB6.
- Core/DSPAudio/mixer.c: comments updated for retained jack-detect state consumption.
- Core/compat/stm32f4xx.h: shim comment updated for current jack-detect ownership.
- README.md, MEMORY.md, FAT_AUDIT.md, SAVE_ALL_AUDIT.md, ST1_JACK_DET_AUDIT.md, knowledge_files/hardware_archive/*.md, knowledge_files/log_archive/000_SESSION_INDEX.md: current behavior and handoff context updated.

KNOWN ISSUES INTRODUCED: None known. Build products and audit files are dirty/untracked in the working tree by design for this session.
KNOWN ISSUES RESOLVED: Unsupported FAT12/exFAT cards no longer silently fail as generic load problems; bad/stale globals no longer blindly apply shifted values; legacy 22-byte globals load safely; global MIDI channel values outside 1..16 are sanitized; PERF v1 BPM load no longer seeks phantom padding; CLK/RST pins and OUT1 jack detect behavior corrected.

NEXT SESSION RECOMMENDED GOAL: Structured regression pass on SD/file behavior with actual cards/files, plus a compact hardware routing matrix for OUT1/OUT2 jack-detect audio behavior.
BLOCKERS: Requires hardware tests with FAT12, exFAT, MBR-FAT32, 22-byte legacy glo.cfg/ALL, 23-byte current glo.cfg/ALL, and intentionally stale globals lengths.

CRITICAL REMINDERS FOR NEXT SESSION:
- FAT16/FAT32 supported; FAT12/exFAT unsupported. Unsupported-card screen is Unsupported card / use MBR-FAT32 for 5 seconds, and the filesystem should not mount/load from that card.
- glo.cfg and ALL globals remain raw/unversioned. Do not add versioning unless the user explicitly changes that decision.
- Globals policy is only 22 bytes legacy, 23 bytes current, or stale fallback/warning. Any other length must not apply shifted/stale values.
- 22-byte legacy globals load silently, then force PAR_EXT_SYNC=auto and PAR_OSC_WAVE_INTERP=1.
- Stale globals fallback loads only the safe prefix through PAR_PRESCALER_CLOCK_OUT1, then forces PAR_EXT_SYNC=auto, PAR_BAR_RESET_MODE=0, PAR_MIDI_CHAN_GLOBAL=1, PAR_OSC_WAVE_INTERP=1.
- ALL stale warning must appear after the pattern/loading UI finishes, not while it is still loading.
- menuPages.h globals rows are fragile: early TEXT_EMPTY/PAR_NONE can block later entries. Reordered globals need manual verification.
- CLK IN is PD4 rising edge with GPIO input pull-up. RST IN is PD5 rising edge with GPIO input pull-up. Do not revert to falling edge or active-low level-gate assumptions.
- RST IN is an edge reset-to-pattern-start, not a run/stop gate.
- PD6/PD7 are OUT1 L/R jack detect, use pull-ups, are retained state sampled at 500Hz with PB4/PB6, and should not fire EXTI.
```

---

## Detailed Session Notes

## 1. Session Scope

Session 025 started as an SD/load-save cleanup session and then moved into hardware debugging for CLK/RST and OUT1 jack detect. The SD side focused on unsupported cards, `glo.cfg`, `.all`, stale globals, legacy globals, and global menu cleanup. The hardware side focused on `Core/DSPAudio/mixer.c` symptoms but eventually traced the actual issue into PD4/PD5/PD6/PD7 configuration and retained-state ownership.

The session included several course corrections. Early assumptions inherited from older notes were wrong for this hardware. The final docs and code now reflect the hardware behavior observed during this session rather than the older background claims.

## 2. Unsupported SD Card Handling

Supported SD card filesystems are FAT16 and FAT32. The recommended format is MBR-FAT32 because it is the practical cross-compatible format for this project and for original LXR workflows.

Unsupported formats are FAT12 and exFAT. When an unsupported card is detected at boot, the firmware displays:

```text
Unsupported card
use MBR-FAT32
```

The warning is held for 5 seconds. The important behavior is that unsupported cards are not mounted and boot load should not proceed from that card.

Implementation notes:

- `Core/Hardware/SD/filesystem.c` added sector-0/card-layout probing after asyncfatfs mount failure.
- exFAT detection checks the OEM field and MBR partition type `0x07` where applicable.
- FAT12 detection uses FAT volume geometry/cluster count and treats FAT volumes at or below the FAT12 cluster threshold as unsupported.
- `filesystem_bootDetectedUnsupportedCard()` exposes a boot flag.
- `main.c` latches the flag and shows the 5-second modal warning after `menu_start()`.
- Comments were added in `main.c` and `filesystem.c` explaining why this is a boot-only modal path and why FAT12/exFAT must stop the load path.

Remaining validation needed:

- Actual FAT12 card/image.
- Actual exFAT card/image.
- MBR-FAT32 card to confirm no false unsupported warning.
- FAT16 card to confirm support still works.

## 3. Globals and ALL Load/Save Policy

The final policy deliberately does not add versioning.

`glo.cfg` and `.all` globals remain raw/unversioned byte spans. This was a user decision during Session 025 after earlier audit proposals considered `glo.cfg` headers and `.all` version bumps.

Final accepted globals lengths:

- 22 bytes: legacy LXR/LXR-master globals span. Load silently, then force compatibility defaults for moved/new fields.
- 23 bytes: current LXR-02 globals span. Load normally.
- Any other length: stale/unknown. Do not apply arbitrary shifted values. Use safe fallback and show warning.

Final constants:

- `NUM_PARAMS = 275`
- `PAR_BEGINNING_OF_GLOBALS = 252`
- Current globals span = 23 bytes
- Legacy globals span = 22 bytes
- Fixed `.all` meta field remains 64 bytes
- Current `.all` meta padding = 41 bytes

Legacy 22-byte behavior:

- Load all overlapping legacy bytes.
- Set `PAR_EXT_SYNC = SEQ_EXT_SYNC_AUTO`.
- Set `PAR_OSC_WAVE_INTERP = 1`.
- Do not show a warning.

Current 23-byte behavior:

- Load all current globals normally.
- Sanitize global MIDI channel after load.
- Do not show a warning.

Stale/other-length behavior:

- Reset globals to safe defaults.
- Overlay only the trusted prefix through `PAR_PRESCALER_CLOCK_OUT1`.
- Set `PAR_EXT_SYNC = SEQ_EXT_SYNC_AUTO`.
- Set `PAR_BAR_RESET_MODE = 0`.
- Set `PAR_MIDI_CHAN_GLOBAL = 1`.
- Set `PAR_OSC_WAVE_INTERP = 1`.
- Sanitize `PAR_MIDI_CHAN_GLOBAL` to 1 if it is 0 or greater than 16.
- Show `old settings` / `check&save .glo` for stale `glo.cfg`.
- Show `old settings` / `check&save .all` for stale ALL globals.

Warning timing:

- `glo.cfg` stale warning is shown after the globals load result is processed.
- `.all` stale warning is deferred until after pattern/global apply finishes so it appears after the loading UI, per user request.

Important bug fixed during this area:

- PERF v1 files are BPM-only. The loader previously risked treating v1 metadata like it had more padding/fields. The final policy keeps v1 BPM-only and v2+ BPM plus bar-reset mode.

## 4. Parameter Layout and Global Menu Cleanup

`PAR_FETCH` was removed because the parameter-fetch setting is not a real current feature.

The phantom hardware-control enum entries were removed:

- `PAR_QUAD_ENC1`
- `PAR_QUAD_ENC2`
- `PAR_QUAD_ENC3`
- `PAR_QUAD_ENC4`
- `PAR_SLIDER_RV5`
- `PAR_SLIDER_RV6`
- `PAR_SLIDER_RV7`
- `PAR_SLIDER_RV8`
- `PAR_SLIDER_RV9`
- `PAR_SLIDER_RV10`

Rationale:

- Endless pots are delta input devices, not persisted state.
- Sliders are physical hardware truth and continuously update `slider_vol[]` from ADC.
- Saving these as global parameters only stored zeros or meaningless values.

Final `NUM_PARAMS` after removals is 275.

Critical menu warning from the user:

- `Core/Menu/menuPages.h` uses `TEXT_EMPTY`/`PAR_NONE` in ways that can terminate traversal.
- Placing `TEXT_EMPTY`/`PAR_NONE` in the wrong globals slot can block access to the rest of the global menu pages.
- This has burned the project multiple times and should be manually verified any time globals are reordered.
- A code comment was added directly above the globals menu table.

## 5. Save/ALL Audit State

`SAVE_ALL_AUDIT.md` now has a final Session 025 status section at the top. Older sections below remain as the investigation trail, but they are explicitly marked as superseded where needed.

Final recommendations followed from `SAVE_ALL_AUDIT.md`:

- Remove phantom hardware params.
- Remove `PAR_FETCH`.
- Keep no versioning.
- Support only legacy 22-byte and current 23-byte globals as normal paths.
- Treat every other globals length as stale and warn.
- Apply policy to both boot `glo.cfg` and `.all` globals.
- Sanitize global MIDI channel values.

Recommendations deliberately not followed:

- No `glo.cfg` header/version byte.
- No `.all` container version bump.

## 6. CLK IN / RST IN Correction

The old background information was incorrect. The working/current model is:

- CLK IN = PD4.
- RST IN = PD5.
- Both are GPIO inputs with internal pull-ups.
- Both use rising-edge EXTI, meaning low-to-high voltage transition at the MCU pin.
- CLK IN uses EXTI4 and `EXTI4_IRQHandler`.
- RST IN uses EXTI5 through `EXTI9_5_IRQHandler`.
- TIM2 is a free-running 1 MHz timestamp counter and is not reset on pulses.

The hardware evidence came from the `lxr02_2` diagnostic firmware:

- No cable read `GPIOD_IDR = 0x35`, binary `00110101`.
- RST IN insert/remove changed `0x35 -> 0x15`, binary `00110101 -> 00010101`, identifying bit 5, PD5.
- CLK IN insert/remove changed `0x35 -> 0x25`, binary `00110101 -> 00100101`, identifying bit 4, PD4.

The working diagnostic in the current firmware showed:

```text
PD45:11 M00 P11
D0000 C000 R000
```

Interpretation:

- `PD45:11` means PD4 and PD5 read high at idle in the diagnostic state.
- `M00` means both are GPIO input mode.
- `P11` means both have internal pull-ups.
- Feeding CLK OUT to RST IN toggled the second PD45 bit and incremented reset counters.
- Feeding CLK OUT to CLK IN showed PD4 changes and occasional clock counter increments, though self-clocking the sequencer this way can lock or behave unclearly.

A failed configuration was explicitly setting PD4/PD5 to input with no pull. Hardware stopped reading the inputs. The final code explicitly sets input plus pull-up.

RST behavior:

- RST IN is not a level-gated run/reset input.
- RST IN rising edge calls `seq_resetToPatternStart()`.
- It resets pattern position without toggling transport or sending MIDI stop/start.

## 7. OUT Jack Detect Final Behavior

Confirmed mapping:

- OUT1 L detect = PD6.
- OUT1 R detect = PD7.
- OUT2 L detect = PB4.
- OUT2 R detect = PB6.

Final runtime model:

- All four jack-detect pins are retained state, not edge events.
- PB4/PB6 and PD6/PD7 are sampled by `timebase_serviceFrontPanel()` at 500Hz from the foreground loop.
- `mixer_setOutJackDetectPB()` and `mixer_setOutJackDetectPD()` update retained mixer cache state.
- `mixer_checkOutJackAvailable()` consumes cached retained state.
- PD6/PD7 EXTI is intentionally masked.
- PD6/PD7 have internal pull-ups enabled.

Why PD6/PD7 needed pull-ups:

- User observed audio briefly route correctly when plugging L1, then cut out about half a second later.
- That indicated an insertion transient was visible, then a later poll overwrote the retained mixer state.
- The jack detect contact appears to ground the pin with no plug and open the pin with plug inserted.
- Therefore inserted state needs a weak pull-up to remain high between polls.
- User later confirmed this behavior checks out.

Important routing caveat from `ST1_JACK_DET_AUDIT.md`:

- Mixer fallback rules can make jack-detect state changes hard to hear in some routing scenarios.
- If no jacks appear connected, some routes fall back to DAC1/headphone behavior.
- For future debugging, use a routing preset where PD6/PD7 state must affect destination, or temporarily display mixer cache state.
- Do not re-enable PD6/PD7 EXTI just to debug state; the state model is correct.

## 8. Code Comment Coverage

Session 025 source changes include comments in the risky places:

- `main.c`: unsupported-card modal warning and boot-only hold.
- `Core/Hardware/SD/filesystem.c`: raw/unversioned globals policy, legacy/current/stale decisions, `.all` meta inference, defaults-first stale fallback, one-byte-past read for oversize `glo.cfg`, unsupported card probing.
- `Core/Hardware/SD/filesystem.h`: stale globals one-shot warning source.
- `Core/Preset/ParameterArray.h`: raw globals span is saved directly; do not add hardware controls to globals.
- `Core/Menu/menu.c`: stale warning UI and deferred `.all` warning timing.
- `Core/Menu/menuPages.h`: `TEXT_EMPTY`/`PAR_NONE` can block later globals pages.
- `Core/Sequencer/sequencer.c`: RST edge resets to pattern start without transport toggle.
- `Core/Hardware/triggerJacks.c`: PD4/PD5 rising-edge input pull-ups; PD6/PD7 pull-ups and no EXTI; tiny EXTI event handlers.
- `Core/Hardware/timebase.c`: 500Hz foreground jack-detect state sampling.
- `Core/DSPAudio/mixer.c`: retained jack-detect state consumption.
- `Core/compat/stm32f4xx.h`: shim warning updated for current jack-detect owner.

## 9. Documentation Updated

Updated during session closeout:

- `README.md`: FAT policy, unsupported-card behavior, less absolute file-compatibility wording, log list updated.
- `MEMORY.md`: Session 025 summary, FAT policy, globals compatibility policy, log list, IRQ section current marker.
- `knowledge_files/hardware_archive/HARDWARE_MAP.md`: PD4/PD5 rising-edge pull-up, PD6/PD7 pull-up retained polling, IRQ notes.
- `knowledge_files/hardware_archive/XP_CONNECTOR_MAPS.md`: RST/CLK neutral jack input labels and rising-edge pull-up status.
- `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md`: no longer says PD4/PD5 are stubbed or TIM2 uninitialized; PD6/PD7 no longer unknown.
- `SAVE_ALL_AUDIT.md`: final implementation status and superseded historical notes.
- `ST1_JACK_DET_AUDIT.md`: final jack-detect assessment and current CLK/RST correction.
- `knowledge_files/log_archive/000_SESSION_INDEX.md`: Session 025 quick row, summary, and corrected cross-session facts.

Historical logs in `knowledge_files/log_archive/001..024` were intentionally not rewritten.

## 10. Verification Performed

Build verification during the final implementation pass:

```sh
make -j4 && make img
```

This succeeded. The generated image was:

```text
build/LXRV2_lxr02.img
```

Expected warnings remain from the nano syscall stubs (`_close`, `_lseek`, `_read`, `_write`). No final behavior-changing compile errors were reported.

Hardware/user verification during session:

- CLK IN and RST IN read correctly after PD4/PD5 input pull-up/rising-edge config.
- RST IN and CLK IN behavior was confirmed working before final jack-detect cleanup.
- PD6/PD7 registered raw state/edges during diagnostics.
- After PD6/PD7 pull-ups and retained polling, user reported the jack-detect checks out.

Not yet exhaustively verified:

- Unsupported FAT12 card.
- Unsupported exFAT card.
- Supported FAT16 card.
- Supported MBR-FAT32 card no false warning.
- Legacy 22-byte `glo.cfg` on boot.
- Current 23-byte `glo.cfg` on boot.
- Stale/invalid `glo.cfg` warning and safe fallback.
- Legacy 22-byte ALL globals.
- Current 23-byte ALL globals.
- Stale/invalid ALL warning after pattern loading screen.
- Global MIDI channel sanitize from `glo.cfg` and ALL.

## 11. Dirty Tree Notes

The working tree is expected to be dirty at the end of Session 025. Source, docs, generated build files, and untracked audit/support files exist in the workspace.

Do not revert unrelated user work. Do not delete untracked files unless explicitly requested.

Important untracked files from this session/context:

- `FAT_AUDIT.md`
- `SAVE_ALL_AUDIT.md`
- `ST1_JACK_DET_AUDIT.md`
- `GLO.CFG`
- `lxr02_2/`

## 12. Recommended Next Steps

Most valuable next session path:

1. Prepare a small SD regression matrix with card/files:
   - MBR-FAT32 current files.
   - FAT16 card.
   - FAT12 card or image.
   - exFAT card.
   - 22-byte legacy `glo.cfg`.
   - 23-byte current `glo.cfg`.
   - bad-length `glo.cfg`.
   - ALL with 22-byte globals.
   - ALL with 23-byte globals.
   - ALL with bad-length/stale globals.

2. Verify observed UI/audio behavior:
   - Unsupported-card screen appears and holds 5 seconds.
   - Unsupported card does not load globals/kits.
   - Legacy globals silently load with correct compatibility defaults.
   - Current globals load normally.
   - Bad globals preserve only safe values and show `check&save` warning.
   - ALL stale warning appears after loading UI.

3. If OUT routing still feels odd, test with a strict routing matrix rather than GPIO diagnostics:
   - No plugs.
   - OUT1 L only.
   - OUT1 R only.
   - OUT2 L only.
   - OUT2 R only.
   - Stereo/mono destinations that avoid fallback ambiguity.

