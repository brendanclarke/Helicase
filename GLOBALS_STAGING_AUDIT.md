# Globals Staging / Duplication Audit

Purpose: isolate the current global-parameter path and identify what can be
eliminated later. The specific question is whether globals need to live
permanently in multiple places, or whether file load/save can read/write
owner-owned state directly while still chunking runtime apply.

This is documentation only. No code changes are proposed for this pass.

## Current Global Span

Defined in `Core/Preset/ParameterArray.h`:

- `PAR_BEGINNING_OF_GLOBALS` starts the raw global byte span.
- Current span is 23 bytes: `PAR_BPM` through `PAR_OSC_WAVE_INTERP`.
- Current IDs are 252..274, because `NUM_PARAMS` is currently 275.
- `glo.cfg` stores exactly this span.
- `.all` stores this span in the 64-byte container meta field, padded with
  `0xff`.
- `.prf` stores only `PAR_BPM` and `PAR_BAR_RESET_MODE` in the container meta
  field.

Current globals:

| Offset | Param | Current permanent byte | Runtime owner / mirror | Apply path |
|---:|---|---|---|---|
| 0 | `PAR_BPM` | `parameter_values[PAR_BPM]` | `seq_tempo` in `sequencer.c` | `seq_setBpm()` |
| 1 | `PAR_MIDI_CHAN_1` | `parameter_values[]` | `midi_MidiChannels[0]` | `midiParser_setChannel(0, value-1)` |
| 2 | `PAR_MIDI_CHAN_2` | `parameter_values[]` | `midi_MidiChannels[1]` | `midiParser_setChannel(1, value-1)` |
| 3 | `PAR_MIDI_CHAN_3` | `parameter_values[]` | `midi_MidiChannels[2]` | `midiParser_setChannel(2, value-1)` |
| 4 | `PAR_MIDI_CHAN_4` | `parameter_values[]` | `midi_MidiChannels[3]` | `midiParser_setChannel(3, value-1)` |
| 5 | `PAR_MIDI_CHAN_5` | `parameter_values[]` | `midi_MidiChannels[4]` | `midiParser_setChannel(4, value-1)` |
| 6 | `PAR_MIDI_CHAN_6` | `parameter_values[]` | `midi_MidiChannels[5]` | `midiParser_setChannel(5, value-1)` |
| 7 | `PAR_EXT_SYNC` | `parameter_values[PAR_EXT_SYNC]` | `seq_isSyncExternal`, `seq_autoSyncActiveSource` | `seq_setExtSyncSource()` |
| 8 | `PAR_FOLLOW` | `parameter_values[PAR_FOLLOW]` | none; read directly by UI/LED paths | no owner apply today |
| 9 | `PAR_QUANTISATION` | `parameter_values[PAR_QUANTISATION]` | `seq_quantisation` | `seq_setQuantisation()` |
| 10 | `PAR_SCREENSAVER_ON_OFF` | `parameter_values[]` | none; `screensaver.c` reads `parameter_values[]` | no owner apply today |
| 11 | `PAR_MIDI_MODE` | `parameter_values[]` | no runtime owner found in current code | no-op/default |
| 12 | `PAR_MIDI_CHAN_7` | `parameter_values[]` | `midi_MidiChannels[6]` | remapped then `midiParser_setChannel(6, value-1)` |
| 13 | `PAR_MIDI_ROUTING` | `parameter_values[]` | `midiParser_routing` | `midiParser_setRouting()` |
| 14 | `PAR_MIDI_FILT_TX` | `parameter_values[]` | high nibble of `midiParser_txRxFilter` | `midiParser_setFilter(1, value)` |
| 15 | `PAR_MIDI_FILT_RX` | `parameter_values[]` | low nibble of `midiParser_txRxFilter` | `midiParser_setFilter(0, value)` |
| 16 | `PAR_PRESCALER_CLOCK_IN` | `parameter_values[]` | `trigger_prescalerClockInput` | `triggerJacks_setClockInputPpq()` |
| 17 | `PAR_PRESCALER_CLOCK_OUT1` | `parameter_values[]` | `trigger_dividerClockOut1` | `triggerJacks_setClockOut1Ppq()` |
| 18 | `PAR_PRESCALER_CLOCK_OUT2` | `parameter_values[]` | compatibility no-op; `trigger_dividerClockOut2` exists but setter ignores value | `triggerJacks_setClockOut2Ppq()` |
| 19 | `PAR_TRIG_GATE_MODE` | `parameter_values[]` | `trigger_gateMode` | `trigger_setGatemode()` |
| 20 | `PAR_BAR_RESET_MODE` | `parameter_values[]` | `seq_resetBarOnPatternChange` | direct assignment in `menu_parseGlobalParam()` |
| 21 | `PAR_MIDI_CHAN_GLOBAL` | `parameter_values[]` | `midi_MidiChannels[7]` | `midiParser_setChannel(7, value-1)` |
| 22 | `PAR_OSC_WAVE_INTERP` | `parameter_values[]` | `modNode_waveInterpEnabled` plus oscillator generation state | `modNode_setWaveInterpEnabled()` |

## Current Load/Save Paths

### `glo.cfg` Save

- Function: `filesystem_saveGlobals_tick()`.
- Current behavior:
  - Phase 0 copies `parameter_values + PAR_BEGINNING_OF_GLOBALS` into
    `staging_buf`.
  - Later phases write `staging_buf` asynchronously.
- Duplicate involved:
  - `parameter_values[]` is the persistent menu/global byte source.
  - `staging_buf` is a temporary save snapshot.
- Keep for now:
  - `filesystem.c` is new and should not be touched in this cleanup thread.

### `glo.cfg` Load

- Function: `filesystem_loadGlobals_tick()`.
- Current behavior:
  - Reads bytes into `staging_buf`.
  - Validates exact current length, exact legacy 22-byte length, or stale length.
  - Applies the trusted prefix into `parameter_values[]`.
  - `menu_pollPresetStatus()` later starts `menu_startGlobalApply()`.
- Duplicate involved:
  - `staging_buf` is a temporary validation buffer.
  - `parameter_values[]` becomes the permanent byte copy.
  - Owner modules receive runtime mirrors during `menu_tickGlobalApply()`.
- Why validation buffer exists:
  - Loader reads one byte past the expected current length so oversized globals
    files are detected as stale rather than silently truncated.

### `.all` Save

- Function: `filesystem_saveContainer_tick()`.
- Current behavior:
  - Meta phase writes the full global span one byte at a time from
    `parameter_values[PAR_BEGINNING_OF_GLOBALS + op_stream_index]`.
  - The 64-byte meta field is padded with `0xff`.
- Duplicate involved:
  - No full save snapshot for meta; it streams from `parameter_values[]`.
  - Still duplicates owner runtime state if the owner module is canonical.

### `.all` Load

- Function: `filesystem_loadContainer_tick()`.
- Current behavior:
  - Reads the full 64-byte meta field into `staging_buf`.
  - Infers current, legacy-22, or stale prefix layout.
  - Applies globals into `parameter_values[]`.
  - Later menu completion chains sound apply, global apply, pattern refresh, UI
    reset/repaint, and stale-warning display.
- Duplicate involved:
  - Temporary `staging_buf` for the fixed 64-byte meta field.
  - Permanent `parameter_values[]` copy.
  - Owner module mirrors after chunked apply.

### `.prf` Save/Load

- Functions:
  - `filesystem_saveContainer_tick()`
  - `filesystem_loadContainer_tick()`
- Current behavior:
  - Save writes only `PAR_BPM` and `PAR_BAR_RESET_MODE` into container meta.
  - Load reads only `PAR_BPM` for version 1, and `PAR_BPM` plus
    `PAR_BAR_RESET_MODE` for version 2.
  - Runtime apply is done by `menu_startSoundApply(... applyPerformanceGlobals=1 ...)`,
    which later calls `menu_parseGlobalParam(PAR_BPM, ...)` and
    `menu_parseGlobalParam(PAR_BAR_RESET_MODE, ...)`.
- Duplicate involved:
  - `parameter_values[]` stores the loaded bytes until menu apply reaches the
    performance-global step.

## Current Permanent Duplication

Today every global with a runtime owner generally exists in two places:

- `parameter_values[PAR_*]`: menu/file/UI-facing byte.
- Owner runtime state: sequencer, MIDI parser, trigger jacks, modulation node,
  or direct UI reader.

The permanent duplicate is not `staging_buf`; that is temporary and scoped to
filesystem operations. The permanent duplicate is `parameter_values[]` plus
owner state.

Globals with clear owner mirrors:

- Sequencer:
  - `PAR_BPM` -> `seq_tempo`
  - `PAR_EXT_SYNC` -> `seq_isSyncExternal`, `seq_autoSyncActiveSource`
  - `PAR_QUANTISATION` -> `seq_quantisation`
  - `PAR_BAR_RESET_MODE` -> `seq_resetBarOnPatternChange`
- MIDI:
  - `PAR_MIDI_CHAN_1..7`, `PAR_MIDI_CHAN_GLOBAL` -> `midi_MidiChannels[0..7]`
  - `PAR_MIDI_ROUTING` -> `midiParser_routing`
  - `PAR_MIDI_FILT_TX/RX` -> `midiParser_txRxFilter`
- Trigger jacks:
  - `PAR_PRESCALER_CLOCK_IN` -> `trigger_prescalerClockInput`
  - `PAR_PRESCALER_CLOCK_OUT1` -> `trigger_dividerClockOut1`
  - `PAR_PRESCALER_CLOCK_OUT2` -> compatibility no-op in setter today
  - `PAR_TRIG_GATE_MODE` -> `trigger_gateMode`
- Modulation/oscillator runtime:
  - `PAR_OSC_WAVE_INTERP` -> `modNode_waveInterpEnabled`

Globals currently using `parameter_values[]` as the only apparent owner:

- `PAR_FOLLOW`
  - Read by `ledHandler.c` and `buttonHandler.c`.
  - No separate owner state found.
- `PAR_SCREENSAVER_ON_OFF`
  - Read by `screensaver.c`.
  - No separate owner state found.
- `PAR_MIDI_MODE`
  - Present in menu/global span.
  - No runtime owner found in current code; likely compatibility/legacy UI
    state unless future MIDI behavior uses it.

## Why Chunking Still Matters

Even if permanent duplication is removed, chunking remains useful:

- `PAR_BPM` recalculates synced LFO rates through `seq_setBpm()`.
- MIDI channel changes can send note-off for affected voices.
- Trigger jack prescaler changes reset pulse timing state.
- ALL/performance load completion already chains sound apply, global apply,
  pattern refresh, UI reset/repaint, and stale warnings.

So the cleanup target is not "apply globals all at once." The target is "make
each chunk apply directly to its owner and avoid keeping a permanent second copy
in Menu when the owner can answer reads for save/display."

## Cleanup Shape

### Option A: Low-Risk Wrapper, Same Storage

Keep `parameter_values[]` for now, but stop exposing direct global writes.

Possible API shape:

```c
uint8_t globals_get(uint16_t param);
void globals_setStored(uint16_t param, uint8_t value);
uint8_t globals_applyOne(uint16_t param);
uint8_t globals_serialize(uint16_t offset);
void globals_deserializePrefix(const uint8_t *src, uint16_t len);
```

Pros:

- Minimal behavioral risk.
- Lets filesystem/menu stop knowing compatibility/default rules.
- Does not solve permanent duplication yet.

### Option B: Owner-Canonical Globals With Menu Cache Removed

Move each global to its owner module and provide `get/set/apply` APIs.

Possible map:

- Sequencer owns BPM, ext sync, quantization, bar-reset.
- MIDI parser owns MIDI channels, routing, TX/RX filters, MIDI mode if it still
  matters.
- Trigger jacks owns clock-in/out prescalers and trigger gate mode.
- UI owns follow and screensaver, or those become small `menu_global_state`
  fields with getters.
- Modulation node owns oscillator wave interpolation.

Filesystem save reads through getters instead of `parameter_values[]`.
Filesystem load writes to a temporary load buffer only until validation
completes, then schedules owner applies in chunks.

Pros:

- Removes the permanent "menu byte plus owner mirror" pattern.
- Makes ownership clearer for the future Scene file split.

Risks:

- Menu display currently expects `parameter_values[param]` for visible values.
  It would need getter-backed reads for globals.
- MIDI/external control paths currently update `parameter_values[]` directly for
  some parameters. Those paths need owner APIs too.
- Existing file compatibility defaults still need one centralized table.

### Option C: New Settings Structs

Decision: this is the preferred direction.

Create canonical settings structs outside Menu. The current 23-byte globals span
is too broad semantically for one permanent "menu globals" array; it should
eventually split into scene-level, bank-level, and system-level settings. Exact
membership is TBD during the Scene/file redesign.

Likely ownership split:

- Scene-level settings: values that travel with the active scene/performance
  context, such as transport/performance behavior that should change when the
  scene changes.
- Bank-level settings: values that apply across the 16 scenes inside a loaded
  bank, including future bank `settings.cfg` state.
- System-level settings: hardware/user-device preferences that should persist
  independently of any bank or scene, such as MIDI/trigger/display behavior
  where appropriate.

Temporary bridge shape:

```c
typedef struct SystemSettings {
    uint8_t midi_channel[8];
    uint8_t midi_mode;
    uint8_t midi_routing;
    uint8_t midi_filter_tx;
    uint8_t midi_filter_rx;
    uint8_t clock_in_ppq;
    uint8_t clock_out1_ppq;
    uint8_t clock_out2_ppq;
    uint8_t trig_gate_mode;
    uint8_t screensaver;
    uint8_t osc_wave_interp;
} SystemSettings;

typedef struct BankSettings {
    uint8_t reserved;
} BankSettings;

typedef struct SceneSettings {
    uint8_t bpm;
    uint8_t ext_sync;
    uint8_t follow;
    uint8_t quantisation;
    uint8_t bar_reset_mode;
} SceneSettings;
```

Then global apply chunks from these structs into owner modules.

Pros:

- Good bridge between the current flat globals file and future bank
  `settings.cfg`.
- Centralizes compatibility/defaults.
- Makes the future `.cfg`, scene, and system preference file boundaries visible
  in the data model instead of hiding them in one raw enum tail.

Risks:

- Still duplicates owner runtime state unless the struct becomes the true owner
  and runtime modules query it, which may be wrong for hot paths.
- Requires a translation table to preserve the current raw byte order.
- The example split above is provisional; several current globals could move
  between scene/bank/system once the live-performance workflow is finalized.

## Recommended Direction

Near term:

- Do not touch filesystem staging buffers.
- Keep chunking.
- Add a documented global table in code or docs that maps raw offset -> param ->
  owner -> default -> file compatibility behavior.
- If cleanup is done before the instrument redesign, keep it compatible with
  Option C and avoid hardening Menu as the permanent owner.

Future Scene/file redesign:

- Move `parameter_values[]` and `parameters2[]` into `/Core/Preset/`.
- Remove Menu as permanent owner of full parameter arrays.
- Treat globals/settings as scene-level, bank-level, and system-level storage
  objects, not as a tail slice of the kit/menu parameter array.
- Save/load `settings.cfg`, performance globals, and bank/scene settings through
  explicit owner-aware serializers instead of raw `parameter_values[]` spans.

## Per-Global Notes

### `PAR_BPM`

- Stored byte: `parameter_values[PAR_BPM]`.
- Runtime mirror: `seq_tempo`.
- File locations: `.glo`, `.all`, `.prf`.
- Side effects: `seq_setBpm()` clamps 0 to 1 and calls `lfo_recalcSync()`.
- Cleanup note: owner should be Sequencer or a transport/global settings object.

### `PAR_MIDI_CHAN_1..6`, `PAR_MIDI_CHAN_7`, `PAR_MIDI_CHAN_GLOBAL`

- Stored bytes: eight menu/global channel values, 1..16 for display/file.
- Runtime mirror: `midi_MidiChannels[0..7]`, zero-based.
- Special case: `PAR_MIDI_CHAN_7` is not contiguous with channel 1..6. Menu
  subtracts 5 before the shared channel case so it targets voice index 6.
- Side effects: changing a per-voice channel sends note-off before updating the
  channel.
- Cleanup note: owner should be MidiParser, with getters returning 1..16 for UI
  and file serialization.

### `PAR_EXT_SYNC`

- Stored byte: `parameter_values[PAR_EXT_SYNC]`.
- Runtime mirror: `seq_isSyncExternal`; auto mode also uses
  `seq_autoSyncActiveSource` and `seq_autoSyncLastUs`.
- File locations: `.glo`, `.all`.
- Compatibility defaults: legacy/stale globals force `SEQ_EXT_SYNC_AUTO`.
- Cleanup note: owner should be Sequencer/transport.

### `PAR_FOLLOW`

- Stored byte: `parameter_values[PAR_FOLLOW]`.
- Runtime users: `ledHandler.c` and `buttonHandler.c` read it directly.
- No separate owner mirror found.
- Cleanup note: either leave as UI global state or move into a small globals
  settings owner with `menu_getFollowEnabled()`.

### `PAR_QUANTISATION`

- Stored byte: `parameter_values[PAR_QUANTISATION]`.
- Runtime mirror: `seq_quantisation`.
- Cleanup note: Sequencer should own the runtime value; storage can be serialized
  through a getter.

### `PAR_SCREENSAVER_ON_OFF`

- Stored byte: `parameter_values[PAR_SCREENSAVER_ON_OFF]`.
- Runtime users: `screensaver.c` reads `parameter_values[]` directly.
- Cleanup note: likely UI settings owner, not Preset voice/kit state.

### `PAR_MIDI_MODE`

- Stored byte: `parameter_values[PAR_MIDI_MODE]`.
- Runtime owner: none found in current code.
- Cleanup note: review whether this is still meaningful. If not, it may remain
  only for file compatibility until the globals file format changes.

### `PAR_MIDI_ROUTING`

- Stored byte: `parameter_values[PAR_MIDI_ROUTING]`.
- Runtime mirror: `midiParser_routing`.
- Cleanup note: MidiParser should own route state and expose serialize/display
  getter.

### `PAR_MIDI_FILT_TX`, `PAR_MIDI_FILT_RX`

- Stored bytes: separate menu/global values.
- Runtime mirror: packed byte `midiParser_txRxFilter`.
- Cleanup note: MidiParser already stores them in packed runtime form; future
  getters should unpack high/low nibble for save/display.

### `PAR_PRESCALER_CLOCK_IN`, `PAR_PRESCALER_CLOCK_OUT1`,
`PAR_PRESCALER_CLOCK_OUT2`

- Stored bytes: menu/global PPQ menu values.
- Runtime mirror:
  - input -> `trigger_prescalerClockInput`
  - output 1 -> `trigger_dividerClockOut1`
  - output 2 -> setter is currently a compatibility no-op on LXR-02 hardware.
- Cleanup note: TriggerJacks should own input/out1. Out2 should be flagged as
  compatibility/global-file baggage unless the UI gives it a new meaning.

### `PAR_TRIG_GATE_MODE`

- Stored byte: `parameter_values[PAR_TRIG_GATE_MODE]`.
- Runtime mirror: `trigger_gateMode`.
- Current comments call it a stub/not used in `triggerJacks.h`.
- Cleanup note: verify whether the mode is actually consumed. If not, treat as
  compatibility UI/file state until trigger behavior is redesigned.

### `PAR_BAR_RESET_MODE`

- Stored byte: `parameter_values[PAR_BAR_RESET_MODE]`.
- Runtime mirror: `seq_resetBarOnPatternChange`.
- File locations: `.glo`, `.all`, `.prf`.
- Cleanup note: Sequencer/transport should own it.

### `PAR_OSC_WAVE_INTERP`

- Stored byte: `parameter_values[PAR_OSC_WAVE_INTERP]`.
- Runtime mirror: `modNode_waveInterpEnabled`; oscillator instances also carry
  per-transition wave interpolation generation/state.
- Compatibility defaults:
  - menu init sets 0.
  - legacy/stale globals force 1.
- Cleanup note: ModulationNode or a sound-engine global owner should own it.

## What Might Be Eliminated

Can likely eliminate later:

- Permanent Menu ownership of global bytes that already have a clear runtime
  owner.
- Direct `parameter_values[]` reads from non-menu modules for globals.
- `PAR_MIDI_MODE`, `PAR_PRESCALER_CLOCK_OUT2`, and `PAR_TRIG_GATE_MODE` as active
  runtime state if they remain unused/compatibility-only after review.

Should not eliminate now:

- Chunked apply.
- Filesystem validation buffers.
- Legacy 22-byte and stale-prefix compatibility behavior.
- The ability to serialize the current raw global byte order until the file
  format changes.
