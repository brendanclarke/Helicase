# Proposed revision to `SCOPING_TARGETS.md`

**Status:** proposal for discussion. This document describes a change to the
roadmap; it does not change the existing formats or allocate memory.

The current roadmap puts most menu and performance work in Phase 5 and delays
the FX bus and FX sequencer until the DSP phase. Effects will need files,
Load/Save controls, AutoSave support, parameter editing, and Pattern automation.
Those connections should be established together with a basic buffer-using
Effect before developing the more complex Effect types.

## Roadmap changes

| Current roadmap | Proposed roadmap |
| --- | --- |
| Phase 5: MIDI, UI and performance workflow | Phase 6, with the Effect-specific menu and Load/Save work moved forward |
| Phase 6.5: FX bus and send routing | New Phase 5 |
| Phase 6.6: FX sequencer | New Phase 5 |
| Phase 6.1: DTCM and voice-tier plan | Move the buffer and sine-table work to Phase 5; retain voice tiers in Phase 7 |
| Phase 6.2–6.4 and 6.7: voice, oscillator and processing work | Phase 7, revised to use the Phase 5 shared buffer |

Update the introduction and phase index from six phases to seven. Keep the
completed Phase 1–4 history intact, but correct forward-looking references to
the moved work. The present Phase 5's general Load/Save redesign, automation
view, MIDI work, PERF work, and looper remain in Phase 6; new Phase 5 includes
the particular controls required to make Effects usable. The existing 8-bit
tech-demo stack moves to Phase 7; Phase 5 includes a basic template Effect
that exercises the buffer. Rewrite the old Phase 6.1/6.7 descriptions of
separate, fixed instrument and delay buffers around the shared allocation
described below. Their older memory totals do not describe this plan.

## New Phase 5: Effects foundation and Scene fixes

### 5.1 Define the Scene-owned Effect and its file format

The Effect slot and its type belong solely to the Scene. A Kit does not store
an Effect type, and Kit Load does not change the Scene's Effect. The slot is
flexible and can hold different Effect types, as an Instrument slot can hold
different Instrument types. Correct current roadmap section 6.5 wherever it
says a Kit remembers an FX stack type.

`FILESYSTEM_SPEC.md` already places an `effects.fx` file in each Scene and an
`Effect/` library at the card root. The Scene file is currently a validated
placeholder with no live Effect settings. Replace it with a versioned file
that stores the Effect type, that type's parameter set and normal/Morph
values, and its own 16-step sequence and settings. The sequence is static
Effect data; it is unrelated to the Pattern stack. Decide where the planned
Effect output assignment and level are saved. Per-voice FX send amount and
fader mode are already stored as Scene settings in `sceneset.scg`.

Define each Effect type under `Core/DSP/Effects/<type>/` in
`<type>Parameters.c/.h` and `<type>Effect.c/.h`, following the Instrument
type/parameter split. Put reusable processing components needed by Effect
types in `Core/DSPAudio/`, as Instruments already do. Phase 5 establishes
the flexible slot and type dispatch with a basic template Effect that really
uses the audio buffer.

Use the same Effect data rules for Scene/Bank Load and Save and for the new
standalone Effect Load and Save item. Define what loading an existing
placeholder means, how an invalid Effect file fails without a partial load,
and how an Effect's name and source are tracked. Add these rules to the
filesystem specification when the choices below are settled.


### 5.2 Store one VOICE edit mask for each Scene in the Bank

Today the Bank has one 16-bit mask controlling which Scenes receive a VOICE
parameter edit. Change that to sixteen 16-bit masks indexed by Scene slot:
**32 bytes of Bank state**, replacing the current two-byte mask. Switching
Scenes then selects that Scene's saved edit mask. Preserve the present rule
that the active Scene is included in its edit mask. The retained Bank state
therefore grows by at least 30 bytes even though the AutoSave file has room.

The Bank portion of the current AutoSave record is 128 bytes. Its defined
fields occupy the first 15 bytes, leaving **113 unused bytes**, so all 32
mask bytes fit there without enlarging that portion. This is space in the
AutoSave file, not free working RAM. `bankset.bcg`, the explicit Bank save
file, is text and has no padding to use: its format must change to save all
sixteen masks.

Specify how an old Bank with one mask becomes a Bank with sixteen masks. A
reasonable migration is to assign the old mask to its active Scene and give
each other Scene a mask containing only itself. Also specify how partial Bank
Load and Save preserve masks for Scenes outside the selected set, and what a
standalone Scene Load does with its destination Bank mask. Test those cases
along with Scene switching and boot restore.

### 5.3 Connect Effects to AutoSave and explicit Load/Save

AutoSave currently reserves 512 bytes per Scene for Effect data but saves no
live Effect values. The planned fixed FX sequence alone is **384 bytes**
(16 steps × 24 values). Two sets of 64 parameter values add **128 bytes**,
exactly filling the reserved 512 bytes before a stack type or settings are
stored. The Effect portion of AutoSave therefore needs more space or another
specified storage arrangement. Set its size and compatibility rules when the
Effect file format is designed.

Add Effect changes to AutoSave, including changes caused by explicit Effect,
Scene and Bank loads. Restore them on boot, and keep AutoSave's existing
interrupted-write protection. Add the new Effect item to the current Load/Save
menu and support the root `Effect/` library. A Scene Save/Load must include its
Effect, and a selected Scene in a Bank Save/Load must carry its Effect through
the same file rules. Keep the existing partial-Bank behavior for unselected
Scenes.

### 5.4 Build the FX bus and its controls

Current roadmap section 6.5 specifies a stereo send per voice, the Pre-FX,
Post-FX and FX fader modes, output assignment, and output level. The Scene
already stores each voice's FX send amount and fader mode; connect those
values to the audio path and provide the needed controls in the menu. Define
how the dry voice signal and Effect return are routed and mixed, including
what happens when no processing Effect is selected. Verify routing and Scene
switching with the Phase 5 template Effect, including its use of the buffer.

### 5.5 Check program flash growth and move the sine table

The linker gives the application `0x08008000..0x0807FFFF`, a 480 KiB region;
sample flash begins at `0x08080000`. Investigate what happens as the linked
program and packaged update image approach or exceed that application limit.
Check the linker, image packaging, bootloader/update path, and sample-flash
boundary together. Establish a tested way forward before the growing program
reaches the limit; do not assume the sample region can hold application code.

Move the 8,194-byte `sine_table` from DTCM into application flash during
Phase 5. It is accessed at audio rate, so test multiple simultaneous
sine-based voices at differing high pitches for timing and audio regressions.
Measure both the DTCM recovered and the additional flash use in a clean linked
build. `transientData` is already in flash and needs no second move.

### 5.6 Allocate and share the DTCM audio buffer

After the sine-table move, target one DTCM buffer of about **126 kB** for the
active Effect and instrument DSP. The current linked ledger reports 118,792
bytes free before the move; adding the sine table's 8,194 bytes projects an
upper bound of 126,986 bytes before alignment or other changes. Fix the exact
safe allocation from the new link rather than treating that projection as an
approved buffer size.

Partition this shared buffer between the Effect and instruments. An
instrument allocation is one 100 ms, mono, 16-bit unit of **8,820 bytes**;
allow at most two units per instrument and twelve units across six
instruments. With twelve units assigned to instruments (105,840 bytes), the
Effect still has roughly 20 kB; with none assigned, it can use the full
buffer allotment. Effect types must operate across that range and may use
their share as 8-bit or 16-bit, mono or stereo. Define how the partition and
buffer contents change when the Scene, Kit, Instrument types, or Effect type
change. Exercise both the minimum and maximum Effect shares with the Phase 5
template Effect.

This replaces the current roadmap's assumption that an advanced Instrument
and the FX delay each have a separate, fixed-size DTCM buffer. Account for
the shared buffer and any control state in the SRAM manifest before
implementation, under its allocation-acknowledgement rule.

### 5.7 Add the dedicated FX sequencer and finish automation routing

Move the existing section 6.6 design into this phase: a fixed 16-step,
24-value FX sequence with Off, Fwd, Rnd, FirstX and LastX modes, plus scale
and length. Its settings and steps must survive Effect, Scene and Bank
Load/Save and AutoSave. Add the controls needed to edit it. Decide how its
24 values address a stack with up to 64 parameters and what a stack change
does to the sequence.

The current Pattern format already stores parameter automation targets, and
the Scene target list includes six per-voice Morph amounts, Scene decimation,
and the generated slot-6/track-7 decay control. Pattern playback currently
applies voice-parameter automation but does not apply these Scene targets.
Finish that playback path so Pattern steps can automate the per-voice Morph
amounts and the generated VOICE7 alternate decay (`7dc`), then include Effect
parameters in the same established Pattern automation system. The generated
decay belongs to the Scene's Kit when slot 6 has a non-Choke instrument; a
Choke instrument's own `_choke` decay remains a voice parameter. Define
the value conversion and priority when Pattern and FX-sequencer steps address
the same parameter; do not silently discard either event.

### 5.8 Complete the necessary menu, copy and verification work

Add a focused Effect parameter menu, the FX-sequencer editor, and the new
Effect Load/Save item. Specify how Effect state behaves when a Scene is copied
or cleared. The broad copy/clear and menu redesign can remain in Phase 6.

Verify old and new files, explicit Load/Save, AutoSave on/off, reboot, partial
Bank operations, and failed or interrupted writes. Confirm that a change to
one Scene's Effect or edit mask does not change another Scene's state. Test
the FX bus, template Effect, buffer sharing, sine-table relocation, and
automation on hardware before treating Phase 5 as complete. Record the
application-flash capacity finding and the tested response to growth beyond
the current 480 KiB region.
Update the existing filesystem and AutoSave specifications with the final
formats and test results. Any new working-RAM allocation still needs the
byte count, region, lifetime, owner, and user acknowledgement required by
`SRAM_MANIFEST.md`.

## Other improvements worth discussing

1. **Pattern timing:** Pattern track scale and shuffle are stored but are not
   yet used by playback. Define how the FX sequencer's scale relates to the
   Pattern clock, and consider completing the deferred Pattern timing work
   in this phase.
2. **Scene automation:** Test recording, playback, display, and reset behavior
   together for per-voice Morph and the generated VOICE7 decay. They are
   already offered as targets, so their playback gap should be closed before
   adding more targets.
3. **Effect loading:** Give standalone Effect Load the same clear success and
   failure behavior as the existing Scene/Bank operations. This includes
   deciding what a placeholder or invalid file does to the selected Scene.
4. **Memory budget:** The planned fixed FX sequence is 384 bytes for each
   Scene that keeps one resident. Account separately for Scene-resident
   Effect state, the shared DTCM buffer, and its partition controls. Use a
   clean link after the sine-table move rather than the old memory estimates
   in roadmap section 6.

## Questions to settle during Phase 5

- Are all sixteen FX sequences kept in working memory, and how do their
  24 values select from up to 64 stack parameters?
- How should existing Bank saves, Effect placeholders, and AutoSave records
  migrate to the new formats?
- What is the priority and value range when Pattern and FX-sequencer automation
  set the same Effect parameter?
- What exact shared-buffer size does the post-relocation link permit, and how
  are Instrument and Effect shares reassigned without interrupting audio?
- What tested change will allow the program to grow after it fills the current
  480 KiB application-flash region while preserving the sample region?
