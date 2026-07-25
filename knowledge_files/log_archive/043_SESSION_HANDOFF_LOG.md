# Session 043 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: Dispose obsolete Pattern/runtime storage, reduce the slider LUT, relocate transient PCM to FLASH, and document the resulting RAM contract.  
**Last session summary**: Session 042 completed HCNAMES authority, cache/staging separation, and Instrument Load closure.  
**Working repository**: `/Users/bc/Helicase Project/Helicase-check-fs/Helicase`, development worktree with Session 043 and prior filesystem changes dirty.  
**Constraints today**: no general-purpose use of free RAM, no slider-LUT interpolation, and no transient RAM cache.

## End of session

```
DATE: 2026-07-25
SESSION GOAL: Reduce Pattern, LUT, tagged-runtime, and transient storage while preserving the exact real-code behavior required today.
COMPLETED: Every Scene owns exactly one 112-byte 7 x 128 on/off bitmap; v3 pattern.pat persists it; slider_lut is 1,024 native floats (4,096 B); six 1,176-B tagged slots replace every engine pool; transientData is a 26,460-B FLASH ROM. Current DTCM use is 12,280 B and SRAM1 use is 66,780 B. DTCM remaining capacity is delay-line-only and SRAM1 remaining capacity is Pattern-only.
VERIFIED ON HARDWARE: User reported successful Kit loading and step editing after the Pattern change and accepted the tagged-runtime SRAM result. Full target audio stress validation of FLASH-resident transients remains pending.

CHANGES THIS SESSION:
- PatternData/Sequencer/UI/generators/filesystem: bitmap-only Pattern representation and v3 persistence.
- adcPots: 1,024-float raw >> 2 LUT with no LUT interpolation.
- InstrumentManager/engine/modulation/MIDI/Preset lifecycle: six tagged runtime slots, no fixed engine pools/globals.
- transientTables.c/h: transient PCM FLASH placement and adjacent C/H storage-contract comments.
- SD_CARD/Bank/000 Full: all child pattern files converted to v3.
- Specifications, SRAM manifest, MEMORY, roadmap, index, and this handoff updated.

KNOWN ISSUES INTRODUCED: No known firmware regression from the build. Pattern intentionally retains only trigger bits; stale menu compatibility setters are storage-free no-ops.
KNOWN ISSUES RESOLVED: Step/timing/automation Pattern payload, excess slider LUT nodes, per-engine runtime growth, DTCM transient copy, and ambiguous free-RAM ownership.

NEXT SESSION RECOMMENDED GOAL: Run the transient FLASH hardware stress matrix, then propose the first delay line only with its exact DTCM byte allocation for approval.
BLOCKERS: Hardware audio validation is required for transient ROM acceptance. No new SRAM1/DTCM allocation may be made without user acknowledgement.

CRITICAL REMINDERS FOR NEXT SESSION:
- Free DTCM is delay-line-only; free normal SRAM1 is Pattern-only.
- Before any RAM increase, disclose exact byte count, region, lifetime, and owner, then obtain acknowledgement.
- Do not restore Step arrays, Pattern staging, slider interpolation, transient RAM cache, per-engine pools, or fixed engine globals.
- transientData remains FLASH-resident; sine_table remains DTCM-resident.
- The #if 0 direct MIDI engine switch is historical coverage only; descriptor-keyed tagged writes are authoritative.
```

## Working-plan archival coverage

The three Session 043 working plans (`PAT_LUT_STORAGE_CLEANUP.md`, `TAGGED_UNION_INSTRUMENT_SLOT.md`, and `TRANSIENTS_IN_FLASH.md`) may be deleted after this handoff is retained. Their final details are preserved below.

### Pattern and slider-LUT result

- `PatternSet` is `uint8_t step_on[7][16]`: 112 B per Scene and 1,792 B across sixteen Scenes. `scene_t` is 1,312 B and `scenes` is 20,992 B in the ELF.
- Removed retained `Step`, `PatternSetting`, `LengthRotate`, main-step shadow, Pattern timing/note/velocity/probability/automation arrays, and `automationNode.c/.h`. Playback and recording only read/write one on-bit. Compatibility APIs retain no Pattern state.
- `pat_patternSetGetStep`/`pat_patternSetSetStep` own staged/final representation. Scene wrappers serve playback/UI. Track/bar/pattern copies move exactly 16/2/112 bytes.
- v3 pattern files have a format line, `version=3`, and seven `trackN=` 32-hex-character rows. v1 stays empty; v2 imports only its final 128-bit field; binary bridge payloads are rejected. All sixteen Full Bank child patterns were converted to v3. Bank masks now emit bare four-digit hex and still accept `0x` input.
- The LUT was reduced from 4,096 to 2,048 then 1,024 floats. Raw ADC `>> 2` selects one float for four codes. Downstream slider/mixer arithmetic remains float. Existing 32-frame mixer gain smoothing is not LUT interpolation.

### Tagged instrument runtime result

- `InstrumentManager.c` is the sole persistent engine owner: `runtime_slots[6]`, a tagged union reserve of 1,176 B per slot, 7,056 B total. Compile-time fit assertions cover current Drum/Snare/Cymbal/HiHat types.
- Retired `voiceArray`, `snareVoice`, `cymbalVoice`, `hatVoice`, `runtime_drum_extra`, `runtime_snare_slots`, `runtime_cymbal_slots`, and `runtime_hihat_slots`. None link. The old 13,188-B cumulative ownership is a fixed 7,056 B.
- Engine operations take explicit instance pointers; manager dispatch checks type before casting. ModulationNode uses a tagged-LFO visitor. Deferred Scene/Instrument replacement clears outgoing modulation owners before union replacement.
- Active MIDI CC/CC2 resolves descriptor keys into current tagged runtime. The direct fixed-object switch is `#if 0` coverage text only and should be removed after dedicated MIDI regression coverage.
- Future engine types must fit the 1,176-B reserve. Enlarging it multiplies across all six slots and therefore requires explicit SRAM approval.

### Transient FLASH result

- `transientData` remains 12 x 2,205 signed bytes (`0x675c`, 26,460 B); only its `INCCM` placement was removed. Row order, selectors, and renderer arithmetic remain unchanged.
- Adjacent C/H comments make the permanent contract explicit: ordinary internal-FLASH `const`, direct renderer read, no RAM shadow, selectors 2..13 map to rows 0..11, selectors 0/1 are special modes, and released DTCM is delay-line-reserved.
- ELF result: `transientData` is `t` at `0x08053264`, size 26,460 B. `.dtcm` fell exactly from 35,168 B (`0x8960`) to 8,708 B (`0x2204`); `.dtcmz` remains 3,572 B. The startup DTCM copy no longer includes the table.
- DTCM static use is 12,280 B and `_eflash_load` is `0x0805df90`, below the `0x08080000` sample boundary. The 352,144-B binary is four bytes smaller than the baseline, so the table is not duplicated in FLASH.
- `make -j2`, `make img`, section/symbol/program-header inspection, and `git diff --check` passed. Existing newlib `_close`, `_lseek`, `_read`, `_write` stub warnings and LTO serial notice remain expected.
- `transient_calc` and `transientVolumeTable` are dead-stripped. Their old scalar mapping differs from live block rendering, so removal/repair is separate reviewed work.

## Current linked-memory contract

`knowledge_files/specification_reference/SRAM_MANIFEST.md` is the current ELF inventory.

| Region | Static use | Remaining capacity | Reservation |
| --- | ---: | ---: | --- |
| DTCM | 12,280 B / 131,072 B | 118,792 B | future delay lines only |
| SRAM1 | 66,780 B / 376,832 B | 310,052 B | future Pattern data only |
| All static allocated RAM | 79,060 B | — | no general headroom |

Major owners are `scenes` 20,992 B, `fs_list_cache_name` 9,000 B, `afatfs` 7,344 B, `runtime_slots` 7,056 B, `slider_lut` 4,096 B, and the 2,048-B typed filesystem stage. `sine_table` remains 8,194 B and `squareRootLut` 512 B in DTCM; DTCM CPU output buffers are 3,072 B and DMA buffers stay in SRAM1.

Before any new/enlarged global, static, linker, pool, DMA, or material stack allocation, state exact bytes, region, lifetime, and owner and obtain user acknowledgement. Releasing RAM never authorizes a different subsystem to use it.

## Remaining transient target test

1. On Drum, Snare, Cymbal, and HiHat, verify selector 0 snap-pitch and selector 1 trigger-time offset behavior.
2. Audition PCM selectors 2..13 at low/high transient pitch and volume, including retrigger before one-shot completion.
3. Exercise transient parameters from panel, Scene/Kit load, and active MIDI descriptor CC paths; repeat across legal tagged slots.
4. Stress six simultaneous high-level PCM transients with diverse rows, extreme pitch, and rapid retriggering: no glitch, deadline miss, or CPU regression.

## Permanent documentation updated

- `specification_reference/SRAM_MANIFEST.md`: current ELF placement, owners, and reservation policy.
- `FILESYSTEM_SPEC.md`: v3 bitmap Pattern persistence and controlled v2 import.
- `MODULE_INTERCHANGE_SPEC.md`: current bitmap PatternData contract; old API roster marked historical.
- `CPU_USE_DSP_AUDIT.md`: current cache/MPU/ITCM/LTO/audio-buffer/tagged/LUT/transient supersession.
- `SCOPING_TARGETS.md`: completed storage baseline and allocation rules.
- `MEMORY.md`: v3 Pattern fact, Session 043 pointer, manifest name, and policy.

## End-of-session recommendation

Do not reopen disposed storage for stale menus or historical file formats. Future Pattern work must explicitly design inside reserved SRAM1; future delay work must first propose exact DTCM geometry and obtain approval. Keep root and specification SRAM manifests synchronized after every approved memory-changing build.
