# Enhanced Firmware Features (Repo 2)

These features were implemented on top of LXR firmware 0.34 by Brendan Clarke.
They target the second repository (enhanced firmware), built on top of the clean 0.37 port.

## Feature List

### Pattern/Track Operations
- **Copy individual tracks between patterns**: hold Copy, press source track button, press destination pattern button
- **Pattern scaling**: second page under 'click' (transient voicing sub-page). Number = binary exponent of pattern multiplier (e.g. 2 → 2^2 = 4 bars, quarter note steps, 1/32nd substeps). Max exponent = 7 (128 bars). Note: required changes to avoid using track 1 step counter as reference; dummy step counter introduced.
- **Realign pattern shortcut**: in PERF mode, press the current pattern button to align all tracks to master clock and reset pattern rotation. Shift+current pattern also does this.

### Sequencer Behaviour
- **Instant pattern switching** (global menu option): switching patterns happens on next sub-step (preserving sequencer position) rather than end of bar
- **Velocity=0 steps**: step does not re-trigger envelopes — functions as 'automation only' (like Elektron trigless locks)
- **Multiple voice global channel note matching**: when record is on and active track has a note assignment other than 'any', Note Ons on the global channel match ALL tracks with non-'any' note assignments
- **Track rotation quick access**: in PERF mode, shift+voice buttons quickly switches active voice for pattern rotation

### MIDI
- **MIDI CC per voice channel** (not global channel): see separate CC assignment document
- **Morph assignment**: Mod Wheel on global channel controls morph
- **MIDI CC recorded to automation** when record is on (rather than updating voice parameters directly)
- **Drumkit change via Bank MSB (CC0)** on global MIDI channel
- **Individual drum voice change via Bank MSB (CC0)** on voice MIDI channel
- **Performance/All save** loads kit by Bank MSB on global channel (new global option), or drum kit (original behaviour)
- **MIDI channel select '0' option**: disables MIDI input for that voice
- **Shift button toggle mode** (global menu option): shift button acts as toggle rather than momentary

### LFOs
- **One-shot LFOs**: additional waveforms "si1", "tr1", etc. (one-shot versions of all standard shapes)
- **Exponential triangle LFO**: "xtr" (normal) and "xt1" (one-shot) — exponential up then exponential down
- **One-shot offset = start delay**: with one-shot selected, 'offset' control sets delay before LFO fires (delay scales with rate)
- **Noise LFO**: holds single random value on each retrigger
- **Rect LFO inverted in one-shot mode**: instantly on, then off; offset sets off-time at beginning

### Load/Save Menu
- **Individual drum voice load/save**: Load menu includes entries to load individual drum voices
- **Drum voice naming**: name and number reflects original drum kit even after MIDI bank change
- **Load menu knob assignments** (knobs 1-4):
  - Knob 1: change load type (kit, drum 1, pattern, etc.)
  - Knob 2: change preset number (auto-loads for kit/voice types)
  - Knob 3: no function (disables auto-load if turned)
  - Knob 4: disables auto-load, moves cursor between load type and 'ok'
- **Save menu knob assignments** (knobs 1-4):
  - Knob 1: change save type
  - Knob 2: change cursor position (type, preset, alphanums, 'ok')
  - Knob 3: change cursor value
  - Knob 4: for alphanumeric values, cycles through more characters
  - Character order: capitals (far left knob 3), numbers (middle knob 3), lowercase (far left knob 4)

### Preset System
- **Performance/All save type** now saves morph target parameters. File version incremented to 3. Previous versions load with empty morph target, re-saved as new type.

### Morph
- **Quick morph target parameter access**: when viewing a single parameter (click in + adjust encoder), press Shift to edit/view the morph target version of that parameter

## Implementation Notes for Port

- Pattern scaling required refactoring step counter references — track 1 step counter can no longer be used as a universal reference
- Instant pattern switching requires a flag checked at the sub-step boundary rather than bar end
- One-shot LFOs are additional enum values in the LFO waveform type; phase tracking differs from looping LFOs
- CC-per-voice-channel mapping is documented in a separate CC assignment file (not yet in this project)
- Version 3 Performance files: loading version 2 files must not crash; morph target parameters initialize to defaults
