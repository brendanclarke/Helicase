# Session 016 Handoff Log

DATE: 2026-05-10

SESSION GOAL: Bring up authentic original-LXR parameter morph behavior, then fix RV1-RV4 endless-pot ghost/drift behavior found while testing the morph/performance controls.

COMPLETED:
- Audited original AVR `preset_morph()` behavior and ported the type-agnostic parameter dump model into the single-MCU firmware.
- Restored original fixed-point interpolation math for morph values.
- Implemented a rate-limited morph worker: `preset_morph()` schedules/coalesces work, `preset_morphTick()` applies one sound-parameter slot per main-loop pass.
- Preserved authentic "front panel sends CCs" behavior: morph applies parameters through `frontPanel_sendData()`, so sequencer automation recording remains possible when record is armed.
- Explicitly protected mod-target ranges during morph and fixed the missing `break` in `preset_sendModTarget()` after `CC_VELO_TARGET`.
- Left `parameters2[]` zero-initialized for this session; real morph-kit load/save targeting is deferred to Session 017.
- Removed the morph send skip cache and replaced the dirty flag with request/pass generation tracking so the latest morph value always receives one complete final pass before the worker goes idle.
- Investigated `PAR_TRANS1_WAVE` returning to `Snp` instead of kit value `Ofs`; found it was a general final-pass/cache class of bug, not transient-specific.
- Audited and fixed RV1-RV4 analog endless-pot ghost edits by adding raw A/B snapshots, page-change rebaselining, deadzone state, and post-delta rebaselining.
- Added endless-pot behavior defines in `config.h`: `ENDLESS_POT_DEADZONE`, `ENDLESS_POT_TIMEOUT_MS`, and `ENDLESS_POT_DELTA_TIMEOUT_MS`.
- Made `PAR_MORPH` move at double angular speed at the driver level while still allowing +/-1 increments.
- Reverted the broader `DTYPE_0B255` double-speed rule after hardware testing showed `PAR_BPM` drifting; only `PAR_MORPH` now uses double speed.
- Fixed the remaining BPM/noise drift root cause: the deadzone was only an activation latch. Active pre-delta false starts now cancel if raw A/B falls back inside the deadzone.
- Doubled the endless-pot deadzone from 10 to 20 raw ADC counts.
- Updated `AUDIT_MORPH.md` and `AUDIT_ENDLESS_ENC.md` during the session, then consolidated them here.

VERIFIED ON HARDWARE:
- Partially by user feedback during the session.
- User reported the initial morph implementation was clean.
- User reported the broad `DTYPE_0B255` double-speed rule caused global BPM drift; narrowed to `PAR_MORPH`.
- User then observed BPM could still float infinitely, leading to the active-state deadzone fix.
- Final build with `ENDLESS_POT_DEADZONE = 20` and pre-delta false-start cancellation was built successfully; long idle hardware soak should be the first Session 017 smoke test.

CHANGES THIS SESSION:
- `Core/Preset/presetManager.c`: added original AVR interpolation helper and real `preset_getMorphValue()`.
- `Core/Preset/presetManager.c`: implemented morph scheduler state (`morph_active`, target/pass values, request/pass generations, index).
- `Core/Preset/presetManager.c`: added `preset_morphShouldSkip()` for index 127 and mod-target ranges.
- `Core/Preset/presetManager.c`: added `preset_morphSendParameter()` to route `<128` as `MIDI_CC` and `>=128` as `CC_2`.
- `Core/Preset/presetManager.c`: fixed `preset_sendModTarget()` fall-through by adding `break` after `CC_VELO_TARGET`.
- `Core/Preset/presetManager.h`: declared `preset_morphTick()`.
- `main.c`: calls `preset_morphTick()` once per main-loop pass.
- `Core/Menu/menu.c`: keeps `parameters2[END_OF_SOUND_PARAMETERS]` zero-initialized as the temporary non-loaded morph target.
- `Core/Menu/menu.c`: added endless-pot mapping-change hook that updates per-pot scale and snapshots all RV1-RV4 baselines.
- `Core/Menu/menu.c`: calls the endless-pot mapping-change hook from page/subpage changes.
- `Core/Menu/menu.c`: changed RV1-RV4 parameter add path to saturating int16 arithmetic before dtype clamps.
- `Core/Menu/menu.c`: enables endless-pot double angular scale only when the mapped parameter is `PAR_MORPH`.
- `Core/Hardware/frontPanel/IO/endlessPots.c`: added raw A/B baselines, active/idle state, baseline timers, post-delta timer, and per-pot double-speed scale.
- `Core/Hardware/frontPanel/IO/endlessPots.c`: added snapshot/rebaseline helpers and false-start cancellation when an active pre-delta pot falls back inside the deadzone.
- `Core/Hardware/frontPanel/IO/endlessPots.h`: added `endlessPots_snapshot()`, `endlessPots_snapshotAll()`, and `endlessPots_setDouble()`.
- `config.h`: added endless-pot behavior defines. Current values: `ENDLESS_POT_DEADZONE = 20`, `ENDLESS_POT_TIMEOUT_MS = 5000`, `ENDLESS_POT_DELTA_TIMEOUT_MS = 20`.
- `AUDIT_MORPH.md`: root audit written during session; consolidated here.
- `AUDIT_ENDLESS_ENC.md`: root audit written during session; consolidated here.

KNOWN ISSUES INTRODUCED:
- No intentional known issues introduced.
- The final endless-pot noise fix needs a long idle hardware soak, especially on the global BPM page.

KNOWN ISSUES RESOLVED:
- `preset_morph()` was a stub and did not implement original AVR morph behavior.
- `preset_sendModTarget()` velocity target sends fell through into LFO-target handling.
- Morph could fail to restore late parameters such as `PAR_TRANS1_WAVE` after returning to `morph == 0` because cache/dirty scheduling could let hidden DSP state remain stale.
- RV1-RV4 page changes could apply stale/noisy deltas to the newly mapped parameter.
- Very slow RV movement could be swallowed by immediate rebaselining; solved by separating long timeout and post-delta timeout behavior.
- `PAR_MORPH` speed-up originally applied to all `DTYPE_0B255` parameters and made `PAR_BPM` drift more visibly.
- Endless-pot deadzone was only a one-shot activation gate; after activation it integrated angle noise indefinitely until timeout.

NEXT SESSION RECOMMENDED GOAL:
- Session 017 should implement the remaining save/load types needed for morph: load a real morph kit into `parameters2[]`, save morph kits with original mod-target preservation behavior, and test morph between two actual kits.

BLOCKERS:
- Need hardware idle soak for RV1-RV4 after the final deadzone/fallback fix.
- Need a design for SD/load destination support so normal kit loads target `parameter_values[]` and morph loads target `parameters2[]`.
- Need to decide the exact UI/save flow for `SAVE_TYPE_MORPH` and related original-LXR behavior.

CRITICAL REMINDERS FOR NEXT SESSION:
- Do not reintroduce a morph skip cache. Morph must send a complete final pass at the latest requested value before going idle.
- Keep `preset_morph()` type-agnostic and CC-dump-like for authenticity. It should use `frontPanel_sendData()` and therefore records automation if record is armed.
- Keep index 127 skipped in morph sends; `MIDI_CC` index 127 underflows `midiParser_ccHandler()` to parameter 65535.
- Mod-target ranges are not morphed: `PAR_VEL_DEST_1..6`, `PAR_VOICE_LFO1..6`, and `PAR_TARGET_LFO1..6`.
- `parameters2[]` is still only a zero-filled placeholder. Session 017 must add a real morph load destination.
- `PAR_MORPH` is the only endless-pot target using double angular speed. Do not apply it to all `DTYPE_0B255`; BPM drift exposed that as too broad.
- RV1-RV4 are analog endless pots, not the digital Gray-code encoder. Avoid "quad/quadrature encoder" naming for this driver.
- Endless-pot deadzone must be treated as an ongoing trust condition before the first emitted delta, not only as an activation latch.

---

## End Of Session Block

```
DATE: 2026-05-10
SESSION GOAL: Implement original-LXR parameter morph and fix RV1-RV4 endless-pot ghost/drift behavior.
COMPLETED: Morph interpolation, rate-limited full-pass scheduler, mod-target protections, velo-target fallthrough fix, no-cache final pass guarantee, transient waveform bug trace, endless-pot raw baseline/deadzone/page snapshot logic, PAR_MORPH-only double angular speed, BPM drift/noise false-start fix, audit consolidation.
VERIFIED ON HARDWARE: Partially. User reported morph clean and identified BPM drift behavior; final deadzone=20/false-start-cancel image built and needs long idle soak.

CHANGES THIS SESSION:
- Core/Preset/presetManager.c/h: real morph interpolation/scheduler/tick path, mod-target skip rules, no-cache generation final pass, velo-target break fix.
- main.c: preset_morphTick() serviced from the main loop.
- Core/Menu/menu.c: parameters2[] placeholder retained, endless-pot mapping hook, page-change snapshots, saturating RV delta add, PAR_MORPH-only double scale.
- Core/Hardware/frontPanel/IO/endlessPots.c/h: raw baseline/deadzone state, snapshot APIs, double-speed API, post-delta rebaseline, pre-delta false-start cancellation.
- config.h: ENDLESS_POT_DEADZONE=20, ENDLESS_POT_TIMEOUT_MS=5000, ENDLESS_POT_DELTA_TIMEOUT_MS=20.
- AUDIT_MORPH.md + AUDIT_ENDLESS_ENC.md: consolidated into this log.

KNOWN ISSUES INTRODUCED: none intentional; final endless-pot drift fix needs long idle hardware soak.
KNOWN ISSUES RESOLVED: morph stub, preset_sendModTarget fallthrough, morph final-pass/cache restore bug, RV page-change ghost deltas, morph speed granularity, BPM double-speed drift, endless-pot active-state noise random walk.

NEXT SESSION RECOMMENDED GOAL: Implement morph-kit load/save targeting for parameters2[] and test morphing between actual kits.
BLOCKERS: Need SD FSM/load destination design and hardware soak for final endless-pot noise behavior.

CRITICAL REMINDERS FOR NEXT SESSION:
- No morph skip cache; full final pass at newest morph value is required.
- Morph remains a front-panel CC dump and may record automation when recording is armed.
- Morph skips index 127 and mod-target ranges.
- parameters2[] is still zero-filled until real morph load/save lands.
- PAR_MORPH only gets endless-pot double speed; not all DTYPE_0B255.
- RV1-RV4 deadzone false-start cancellation is intentional; do not collapse it back to a one-shot activation latch.
```

---

## Consolidated Morph Audit

### Reference Behavior

Original morph lived on the front-panel AVR:

- `knowledge_files/LXR-master/front/LxrAvr/Preset/presetManager.c`
- `preset_readDrumsetData(isMorph)` loaded `.snd` data into either `parameter_values[]` or `parameters2[]`.
- `preset_morph(morph)` scanned sound parameters, interpolated `parameter_values[]` toward `parameters2[]`, and sent CC traffic to the STM32 mainboard.
- `preset_getMorphValue(index, morph)` used the same interpolation helper.

Original interpolation:

```c
static uint8_t interpolate(uint8_t a, uint8_t b, uint8_t x)
{
    uint16_t fixedPointValue = (uint16_t)(((a * 256) + (b - a) * x));
    uint8_t result = (uint8_t)(fixedPointValue / 256);
    return (uint8_t)((fixedPointValue & 0xff) < 0x7f ? result : result + 1);
}
```

The original AVR loop was naturally rate-limited because it interleaved `din_readNextInput()`, `dout_updateOutputs()`, and `uart_checkAndParse()` once per parameter and serialized data over 500 kbaud UART. In this port, `frontPanel_sendData()` calls the local parser immediately, so a direct full-loop dump would become an in-process parameter storm.

### Current Port Morph Shape

`parameters2[]` exists in `Core/Menu/menu.c` and is declared in `menu.h`, but Session 016 intentionally leaves it zero-filled. This gives a deterministic "not loaded" morph target for validating the engine before Session 017 adds real morph-kit load/save.

`preset_morph(uint8_t morph)` now only schedules work:

- records the latest requested morph value,
- increments a request generation when the target changes or a new job starts,
- starts a pass if idle.

`preset_morphTick()` now applies exactly one sound-parameter slot per main-loop pass. Each pass uses a snapshot `morph_pass_value`. At pass end, request/pass generations decide whether to start another full pass at the newest target or go idle.

This matters because morph values can change faster than the one-parameter worker can drain. The newest value must get a complete final pass. Otherwise late parameters such as `PAR_TRANS1_WAVE` can remain at an older DSP value.

### No Skip Cache

The first implementation had a `morph_last_sent[]` cache. It was removed.

Reason: in the merged single-MCU port, front-panel parameter state, DSP state, and morph-worker state can diverge. A cache can decide not to send a value even when the DSP needs that value restored. The cost saving is not worth the risk at one parameter per tick.

Current rule:

- every active morph pass sends every non-skipped parameter,
- even if the computed value matches the previous computed value,
- and the final latest morph value must get one complete pass.

### Skip Rules

Morph sends skip:

- index `127`, because `frontPanel_sendData(MIDI_CC, 127, ...)` becomes data1 zero and underflows `midiParser_ccHandler()` to parameter `65535`,
- `PAR_VEL_DEST_1 .. PAR_VEL_DEST_6`,
- `PAR_VOICE_LFO1 .. PAR_VOICE_LFO6`,
- `PAR_TARGET_LFO1 .. PAR_TARGET_LFO6`.

The mod-target skip matches the original save-morph intent: mod targets are not morphed.

### Automation Behavior

The implementation deliberately keeps original "front-panel sends CCs" behavior. Morph uses `frontPanel_sendData()` rather than a special DSP-only apply path. If sequencer recording is armed, morph can record automation. This may feel awkward, but it is authentic to the porting target and was explicitly kept.

### Transient Waveform Bug

Observed symptom:

- current kit has Drum 1 transient waveform value `1` (`Ofs`),
- morph away from zero and back to `0`,
- `PAR_TRANS1_WAVE` sometimes ends as value `0` (`Snp`).

Trace:

- `PAR_TRANS1_WAVE` is index `203`.
- It is `DTYPE_MENU | (MENU_TRANS << 4)`.
- `MENU_TRANS`: `0 = Snp`, `1 = Ofs`.
- Current `parameters2[]` is zero-filled, so a high morph value legitimately sends `0`.
- `CC2_TRANS1_WAVE` applies to the DSP transient generator and does not update `parameter_values[PAR_TRANS1_WAVE]`.
- Therefore, after returning to morph value `0`, a completed final pass should compute and send `1` again.

Conclusion:

- Not transient-specific.
- Any non-skipped parameter whose source and morph target differ can show this class of symptom.
- Discrete/menu parameters make it more obvious than continuous parameters.
- The fix is the no-cache generation scheduler, not a LUT or special-case dtype policy.

### Session 017 Morph Work

Remaining work:

- add SD/load destination support for `parameters2[]`,
- load morph kits into `parameters2[]` instead of `parameter_values[]`,
- normalize morph-loaded mod-target indices just like normal kit loads,
- implement/validate `SAVE_TYPE_MORPH` with original mod-target preservation,
- hardware-test morph between real kits rather than the zero placeholder.

---

## Consolidated Endless-Pot Audit

### Signal Path

RV1-RV4 are analog endless pots sampled through the shared ADC DMA buffer, not digital Gray-code encoders.

Path:

- `endlessPots_tick()` runs from TIM6 at 1kHz.
- It reads raw A/B ADC channels from `adc_dma_buf[]`.
- It subtracts the fixed midpoint `2048`.
- It computes `angle = atan2f(b, a)`.
- It computes and wrap-corrects angle delta against `prev_angle`.
- It scales by `SCALE_FACTOR = 200 / (2*pi)` and accumulates fractional movement.
- When the accumulator crosses an integer boundary, it adds that integer to `delta`.
- `main.c` later calls `endlessPots_getDelta(i)` and passes the small signed delta into `menu_parseKnobDelta()`.

Relevant files:

- `Core/Hardware/frontPanel/IO/endlessPots.c`
- `Core/Hardware/frontPanel/IO/endlessPots.h`
- `Core/Menu/menu.c`
- `main.c`
- `config.h`

### Root Ghost Mechanism

Before Session 016, the algorithm integrated tiny angle changes continuously. There was no raw A/B snapshot after page changes, no post-movement rebaseline, and no deadzone gate before accumulating angle movement. ADC noise, supply noise, LCD/SPI activity, or analog drift could accumulate fractional motion until it became a real +/-1 parameter edit.

Changing menu pages made this feel worse because a stale pending delta could land on the newly mapped parameter.

### Implemented Driver Behavior

The driver now owns raw A/B baselining:

1. Store a raw A/B baseline per pot.
2. While idle/armed, follow current angle but do not accumulate deltas.
3. Enter active tracking only after raw A or B leaves `ENDLESS_POT_DEADZONE`.
4. If active but no parameter delta has been emitted yet, cancel/rebaseline if raw A/B falls back inside the deadzone.
5. After a real emitted delta, recenter after `ENDLESS_POT_DELTA_TIMEOUT_MS`.
6. Also perform a long naive rebaseline after `ENDLESS_POT_TIMEOUT_MS`.
7. Explicit `snapshot` / `snapshotAll` clears pending movement and stores the current raw A/B baseline.

Current defines:

```c
#define ENDLESS_POT_DEADZONE          20
#define ENDLESS_POT_TIMEOUT_MS        5000
#define ENDLESS_POT_DELTA_TIMEOUT_MS  20
```

`ENDLESS_POT_DEADZONE` is +/- raw ADC counts out of 4096.

### Page Mapping API

New driver API:

```c
void endlessPots_snapshot(uint8_t i);
void endlessPots_snapshotAll(void);
void endlessPots_setDouble(uint8_t i, uint8_t enabled);
```

`menu_updateEndlessPotScales()` checks the current RV1-RV4 parameter mapping and enables double scale only for `PAR_MORPH`.

`menu_endlessPotMappingChanged()` updates per-pot scale and snapshots all raw baselines. It is called when menu page/subpage mapping changes.

### Morph Speed

The user wanted X-FADE / `PAR_MORPH` to move twice as fast while preserving +/-1 increments. The correct implementation is not `delta * 2`; it is driver-level angular scaling:

```c
float scale = pot->double_speed ? (SCALE_FACTOR * 2.0f) : SCALE_FACTOR;
```

This halves the angular travel needed to emit one delta, but still emits normal integer deltas.

An initial implementation enabled this for every `DTYPE_0B255` parameter. Hardware testing showed global `PAR_BPM` drift, so the rule was narrowed to `PAR_MORPH` only.

### BPM Drift Follow-Up

After narrowing double speed to `PAR_MORPH`, BPM could still float. That revealed the deeper bug: the deadzone was only an activation latch. Once one noisy sample activated a pot, the active path integrated every angle sample until a timeout.

Fix landed:

- if no delta has been emitted yet,
- and raw A/B falls back inside the deadzone,
- cancel the active state and rebaseline immediately.

This prevents probabilistic noise false-starts from becoming a long random walk.

### Remaining Endless-Pot Risks

- `endlessPots_getDelta()` returns `int32_t`, but `main.c` still narrows it to `int8_t`. Normal RV1-RV4 hardware should not create huge per-loop deltas, but this remains a cleanup candidate if main-loop latency grows.
- The angle math assumes both channels are centered at `2048`. The snapshot/deadzone gate masks normal noise but does not calibrate offset or amplitude mismatch.
- Final `ENDLESS_POT_DEADZONE = 20` should be validated for both long idle stability and very slow intentional turns.

### Hardware Test Plan For Session 017

1. Boot and sit on global BPM page for several minutes without touching RV1.
2. Confirm BPM does not drift.
3. Switch between global/performance/voice/euklid pages without touching RV1-RV4.
4. Confirm no mapped parameter changes on page transitions.
5. Turn each RV slowly enough to test the pre-delta path; confirm deliberate slow movement still eventually updates.
6. Test `PAR_MORPH` on performance and euklid pages; confirm it is faster than normal 0-255 params but still allows +/-1 increments.

### Build Status

Final build command:

```sh
make && make img
```

Final image:

- `build/LXRV2_lxr02.img`
- image size: 243328 bytes
- binary payload: 243312 bytes
- ELF size: `text=242976, data=336, bss=72440, dec=315752`

Only the usual nano-libc syscall warnings were emitted (`_close`, `_lseek`, `_read`, `_write`) plus the LTO serial compilation note.
