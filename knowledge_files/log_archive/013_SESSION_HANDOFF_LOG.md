# Session 13 Handoff Log — DSP Performance Audit + LFO Kit Load Fix

**Date**: 2026-05-08
**Tarball at end**: `lxr02-037_port-02.tar.gz` (unchanged — no new tarball)
**Previous session**: 012

---

## What Was Done

### 1. DSP Performance Audit (DSP_AUDIT.md)

Full audit of the DSP pipeline to identify underrun causes. Ten questions answered:

1. DSP render should NOT move to DMA ISR — confirmed failed approach (Session 10)
2. NVIC priority audit — priorities already correct, no ISR starvation
3. UI tasks in main loop — menu/button/LCD work is low-cost, not the bottleneck
4. I-Cache and D-Cache — **not enabled**, identified as high-impact fix
5. MPU for DMA coherency — not configured, needed before D-cache enable
6. ITCM for code — flash prefetch sufficient at 216MHz, ITCM not needed
7. DTCM for hot data — audioOutBuffer identified as highest-value target
8. CMSIS-DSP — not useful for LXR voice architecture (interleaved per-sample)
9. DMA circular mode — already correct
10. FPU + trig — FPU enabled, but double-literal constants in ResonantFilter.c (lines 141, 167) cause software double emulation in the hottest per-sample SVF loop

Produced 15-item prioritised action list in DSP_AUDIT.md (renamed from AUDIT.md by user).

### 2. Performance Improvements Implemented

**By Claude:**
- I-Cache enable (ICIALLU invalidate + SCB->CCR IC bit) — `clocks.c:92-100`
- D-Cache + MPU configuration — `clocks.c:102-177`:
  - Region 0: All SRAM (0x20000000, 1MB) — Normal, Write-Through, No-Write-Allocate
  - Region 1: DMA buffers (0x20020000, 4KB) — Strongly-Ordered (non-cacheable)
  - Full D-cache invalidate before enable (4-way × 128 sets)
- .dma_nocache NOLOAD section in linker script — `STM32F765VIHx_FLASH.ld:47-55`
- DMA buffers tagged `__attribute__((section(".dma_nocache")))` — `AudioCodecManager.c:175-176`, `endlessPots.c` adc_dma_buf
- audioOutBuffer/audioOutBuffer2 placed in DTCM via INDTCMZ — `AudioCodecManager.c:178-179`

**By user (points 1-8, confirmed done before Claude's work):**
- Various performance items from the audit list
- -Ofast for DSP source files — `Makefile:119` (`CFLAGS_DSP` substitution) and `Makefile:134-137` (pattern rule for `Core/DSPAudio/*.c`)
- -flto added to CFLAGS and LDFLAGS

### 3. PAR_VOICE_LFO Kit Load Bug

**Bug**: When loading a kit, PAR_VOICE_LFO1-6 values didn't reach the DSP modulation targets.

**Root cause identified by user**: `frontPanel_sendData()` for CC_VELO_TARGET and CC_LFO_TARGET was routing through the MIDI CC encode/decode path (`frontPanelParser.c` → `midiParser_ccHandler`), which was designed for the two-MCU architecture. In the merged LXR-02, the modulation target destinations need to be set directly.

**Claude's attempted fix** (incomplete — did not solve the problem):
- Populated `parameterArray[]` entries for PAR_VOICE_LFO1-6 and PAR_TARGET_LFO1-6 pointing to `&parameter_values[]` — `ParameterArray.c:530-554`
- Added `parameter_values[paramNr] = msg.data2` to CC2_VOICE_LFO and CC2_TARGET_LFO cases — `MidiParser.c:1019, 1028`

**User's actual fix**:
- Created `preset_sendModTarget()` in `presetManager.c:132-159` — directly decodes upper/lower and calls `modNode_setDestination()` for both CC_VELO_TARGET and CC_LFO_TARGET, bypassing the `frontPanel_sendData()` → `midiParser_ccHandler()` roundabout
- Replaced `frontPanel_sendData(CC_VELO_TARGET, ...)` and `frontPanel_sendData(CC_LFO_TARGET, ...)` calls in `preset_sendDrumsetParameters()` with `preset_sendModTarget()` calls

---

## Potential Issue Noted

**Missing `break` in `preset_sendModTarget()`**: The `case CC_VELO_TARGET:` block (line 136-141) has no `break` before `case CC_LFO_TARGET:` (line 142). This causes fall-through — every CC_VELO_TARGET call also executes the CC_LFO_TARGET code with the same upper/lower values, potentially setting an incorrect LFO modulation target. Compare with the original `frontPanel_sendData()` in `frontPanelParser.c` which has explicit `break` for each case.

---

## Files Modified This Session

| File | Change |
|------|--------|
| `DSP_AUDIT.md` (new) | 10-question audit + 15-item priority list |
| `Core/Hardware/clocks.c` | I-Cache enable, MPU config (2 regions), D-Cache enable |
| `STM32F765VIHx_FLASH.ld` | .dma_nocache NOLOAD section, 4KB size assertion |
| `Core/Hardware/AudioCodecManager.c` | DMA buffers → .dma_nocache, audioOutBuffer → INDTCMZ |
| `Core/Hardware/frontPanel/IO/endlessPots.c` | adc_dma_buf → .dma_nocache |
| `Core/Preset/ParameterArray.c` | PAR_VOICE_LFO1-6, PAR_TARGET_LFO1-6 entries populated |
| `Core/MIDI/MidiParser.c` | CC2_VOICE_LFO/CC2_TARGET_LFO write parameter_values[] |
| `Core/Preset/presetManager.c` | preset_sendModTarget() added (user fix); old preset_sendDrumsetParameters commented out |
| `Core/Hardware/SD/sd_fsm.c` | Kit save writes PAR_MIDI_NOTE1 (221) params instead of END_OF_SOUND_PARAMETERS (228) |
| `Makefile` | -Ofast for DSP files, -flto (done by user) |

---

## Architectural Notes

### Why frontPanel_sendData Doesn't Work for Mod Targets During Kit Load

In the original two-MCU LXR:
- AVR owns parameter_values[] and the menu
- AVR sends CC_VELO_TARGET / CC_LFO_TARGET over UART to mainboard
- Mainboard decodes and calls modNode_setDestination()

In the merged LXR-02 port:
- `frontPanel_sendData()` was adapted to call `midiParser_ccHandler()` directly for MIDI_CC and CC_2 messages
- CC_VELO_TARGET and CC_LFO_TARGET were handled separately in frontPanelParser.c and DID call modNode_setDestination()
- But the actual problem was that the LFO voice-to-target mapping wasn't being established correctly through this path — the values in parameter_values[] weren't being converted into actual modNode destinations

The correct fix: `preset_sendModTarget()` does the decode+dispatch directly in the preset loading context, ensuring modNode_setDestination() receives the correct resolved target parameter numbers.

### parameterArray Entries for PAR_VOICE_LFO / PAR_TARGET_LFO

These were commented out in the original mainboard code too (both LXR-master and port). They were AVR-only parameters with no DSP struct backing. Claude's fix populated them pointing to `&parameter_values[PAR_VOICE_LFO1]` etc. with TYPE_UINT8. This is not wrong — it makes parameterArray[].ptr valid for these indices — but it was insufficient to solve the kit load bug.

### 4. Kit Save File Size Fix

**Bug**: Kit files loaded as 229 bytes (8 name + 221 params) were re-saved as 236 bytes (8 name + 228 params) with 7 trailing zero bytes. The save used `END_OF_SOUND_PARAMETERS` (228) unconditionally, but the last 7 params (PAR_MIDI_NOTE1-7, indices 221-227) are not used by this firmware.

**Fix**: Changed `sd_fsm.c` save kit phase 0 to write `PAR_MIDI_NOTE1` (221) parameter bytes instead of `END_OF_SOUND_PARAMETERS` (228). All kit saves now produce exactly 229 bytes (`8 + PAR_MIDI_NOTE1`).

| File | Change |
|------|--------|
| `Core/Hardware/SD/sd_fsm.c` | Save kit stages `PAR_MIDI_NOTE1` params instead of `END_OF_SOUND_PARAMETERS` |

---

## What's Next (priorities for Session 14)

1. **Check `preset_sendModTarget()` fall-through** — missing `break` between CC_VELO_TARGET and CC_LFO_TARGET cases
2. **No hi-hat at startup** — boot kit hi-hat still silent; DSP init ordering issue (identified Session 12, not yet investigated)
3. **buttonHandler/ledHandler audit** — BUT_MODE1=31 offset, processPress() case-by-case audit
4. **BAR1/BAR2 race condition** — voiceControl_noteOn/Off from main loop touching DSP state
5. **ResonantFilter.c double literals** — lines 141, 167: `0.5*in` and `1.0 - f_lp2` cause software double emulation in the hottest per-sample loop. Change to `0.5f*in` and `1.0f - f_lp2`
6. **DrumVoice.c VLA** — line 228: `int16_t modBuf[size]` still present, should be static
7. **BufferTools.c float division** — line 120: `i/(size-1.f)` per sample in hot loop
