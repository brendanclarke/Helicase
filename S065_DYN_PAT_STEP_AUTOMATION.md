# S065 — Dynamic Pattern Step Automation

## Scope

Add read and write of per-step automation entries to the existing dynamic
Pattern pool. This is the first real use of the 6-bit automation count field
already reserved in the block header (bits 5..0, currently always zero) and the
2-byte-per-entry automation payload described in `PATTERN_DYNAMIC_STACK.md` §4.

The session covers:

1. Pool block format extension: writing and reading automation entries after the
   specials region;
2. Step-edit menu pages for per-step automation (Method 1);
3. VOICE-menu held-step automation overlay (Method 2);
4. Sequencer playback of automation entries (applying values to targets on each
   triggered/active step);
5. PAT4 compatibility (the existing file format already streams the complete
   pool, so automation entries are persisted automatically once the block writer
   produces them).

Out of scope this session: live-record capture (`SCOPING_TARGETS.md` §4.3a),
automation hold flag semantics (§4.3a reconciliation), copy operations,
defragmentation/slack management, and the full automation-view UI (§5.3).

---

## 1. Automation Entry Wire Format

Each automation entry is 2 bytes, stored little-endian in the pool:

```text
bits 15..9   7-bit value (0..127)
bits 8..0    9-bit parameter target
```

### 1.1 The 9-bit parameter target

The 9-bit target IS the existing `instrument_param_id_t` canonical namespace,
defined in `InstrumentManager.h:24` as `uint16_t`. Construction is
`instrumentParam_make()` at `InstrumentManager.c:213-219`:

```c
id = (uint16_t)slot * INSTRUMENT_PARAM_COUNT + descriptor_index
```

where `INSTRUMENT_PARAM_COUNT == 64` and `INSTRUMENT_SLOT_COUNT == 6`.

The 512 addressable IDs (9 bits) divide into two regions:

| Range | Count | Meaning | Source |
|-------|-------|---------|--------|
| 0..383 | 384 | Voice parameters: `slot * 64 + descriptor_index` | `INSTRUMENT_VOICE_ID_COUNT`, `InstrumentManager.h:20` |
| 384..511 | 128 | Scene and future FX parameters | `INSTRUMENT_TOTAL_ID_COUNT`, `InstrumentManager.h:21` |

Voice IDs decompose back to slot and descriptor index via
`instrumentParam_slot(id)` = `id / 64` and `instrumentParam_local(id)` =
`id % 64` (both at `InstrumentManager.c:222-238`). The sentinel
`INSTRUMENT_PARAM_INVALID == 0xffff` (`InstrumentManager.h:22`) is wider than
9 bits and cannot appear in a stored entry.

Scene targets start at `SCENE_MOD_TARGET_BASE == 384`
(`SceneModTargets.c:5`). Currently 8 are defined (`SceneModTargets.c:20-45`):

| ID | Token | Meaning |
|----|-------|---------|
| 384..389 | `1vm`..`6vm` | Per-voice Morph amount |
| 390 | `srt` | Scene Decimation |
| 391 | `7dc` | Slot-6 Track-7 Amp Decay |

IDs 392..511 are reserved for future FX parameters, matching the Phase 6 FX
sequencer's shared 9-bit ID space (`SCOPING_TARGETS.md` §4.4).

### 1.2 Automation target validation

`INSTRUMENT_TARGET_AUTOMATION` is already defined as an `instrument_target_use_t`
enum value (`InstrumentManager.h:84-87`). Descriptors carry an
`INSTRUMENT_PARAM_FLAG_AUTOMATABLE` flag (`InstrumentManager.h:118`, value
`0x04`), set on standard `ROW` and `ROW_MENU` descriptors via the `FLAGS_IMAGE`
macro in all four instrument parameter tables (`DrumParameters.c`,
`SnareParameters.c`, `CymbalParameters.c`, `HiHatParameters.c`, each at
line 34).

**Known gap**: `instrumentManager_targetValid()` at `InstrumentManager.c:731`
currently returns 0 for ALL non-voice parameters (`id >= 384`):

```c
if (!instrumentParam_isVoiceParameter(id))
    return 0u;
```

Scene targets (384+) are validated only through the separate
`sceneModTarget_valid()` path for velocity/LFO use. Step automation targeting
Scene parameters (per-voice Morph, Scene Decimation, etc.) requires extending
`instrumentManager_targetValid()` to accept Scene targets when `use ==
INSTRUMENT_TARGET_AUTOMATION`. This is a required code change before Method 1
can offer Scene targets in its parameter picker.

### 1.3 The 7-bit value

The 7-bit value maps to the parameter's `instrument_param_value_t` (uint8_t,
0..255 range) via the same conversion already used by MIDI CC paths. This
conversion is settled and implemented at `MidiParser.c:312-319`:

```c
return (value == 127u) ? 255u : (uint8_t)(value * 2u);
```

Documented in `SCOPING_TARGETS.md:595-598`, `FILESYSTEM_SPEC.md:1566-1569`,
and `MEMORY.md:1276`. The inverse (8-bit → 7-bit for display) is
`(value >= 255) ? 127 : value / 2`.

### 1.4 Block layout with automation

The complete block byte layout, extending `PATTERN_DYNAMIC_STACK.md` §4:

```text
bytes 0..1    little-endian header
               bits 15..6: track * 128 + step (10-bit back-reference)
               bits 5..0: automation count (0..63)
byte 2        special flags (present only when PAT_ADDR_SPECIALS_BIT set):
               bit0 note, bit1 velocity, bit2 probability
bytes 3..     present special values in note, velocity, probability order
              (0..3 bytes depending on popcount of flags & 0x07)
next bytes    automation entries, 2 bytes each, `auto_count` entries:
               each entry: bits 15..9 = 7-bit value, bits 8..0 = 9-bit target
padding       zero to 4-byte boundary
```

### 1.5 Block size calculation

```
payload_bytes = PAT_BLOCK_HEADER_BYTES (2)
              + (has_specials ? 1 + popcount(special_flags & 0x07) : 0)
              + auto_count * 2;
chunks = (payload_bytes + 3) / 4;
```

Examples:

| Specials | Automations | Payload bytes | Chunks (4B each) |
|----------|-------------|--------------|-------------------|
| none | 0 | 2 | 1 |
| note only | 0 | 4 (2+1+1) | 1 |
| note+vel+prob | 0 | 6 (2+1+3) | 2 |
| none | 1 | 4 (2+2) | 1 |
| note | 1 | 6 (2+1+1+2) | 2 |
| note+vel+prob | 4 | 14 (2+1+3+8) | 4 |
| none | 63 | 128 (2+126) | 32 |

### 1.6 Uniqueness invariant

A step must never contain two automation entries with the same 9-bit target.
This means the same `instrument_param_id_t` — the same voice slot AND the same
descriptor index. Every writer (Method 1 and Method 2) must enforce this at
write time. Duplicate detection is a linear scan of the step's automation list
(at counts ≤63 this is negligible).

### 1.7 Per-step ceiling

The 6-bit count field defines the hard ceiling at 63 (0..63; zero is a valid
count). The pool is the natural limiter beyond that — the dynamic allocator
exists precisely to let any step use as much or as little of the pool as it
needs. No firmware-enforced ceiling below 63 is appropriate.

A stack-local read struct holding 63 entries is `63 × 4 = 252 bytes`, well
within the available stack budget on all call paths.

---

## 2. Pool Block Reader/Writer Extension

### 2.1 pat_blockChunks()

Currently takes only `special_flags`. Must be extended to accept `auto_count`:

```c
static uint8_t pat_blockChunks(uint8_t special_flags, uint8_t auto_count)
```

The existing callers all pass `auto_count = 0` until automation is implemented.
The formula is §1.5 above.

### 2.2 pat_blockRead()

Currently at `PatternData.c:329`. Returns `pat_step_specials_t` (4 bytes:
note, velocity, probability, flags). Must be extended to also return automation
entries. Two options:

**Option A**: return a larger `pat_step_data_t` struct that includes both
specials and automations. This is the clean approach — one read, one struct.

**Option B**: keep `pat_blockRead()` returning specials only, add a separate
`pat_blockReadAutomations()` for callers that need them. This avoids enlarging
the return struct on paths that don't need automations (the sequencer's
note/velocity/probability path).

Recommendation: Option B. The sequencer's hot trigger path
(`seq_advanceTrackStep` at `sequencer.c:389-408`) needs note/velocity/
probability every step but only needs automations for the foreground application
path. The specials reader stays small; the automation reader is a separate call.

### 2.3 pat_blockWrite()

Currently at `PatternData.c:287`. Accepts header fields, special flags, and the
three special values. Must be extended to also accept an automation entry array
and count. The writer places entries after the specials region and pads to
4-byte boundary. The header's bits 5..0 are set to the automation count.

### 2.4 Data structures

```c
typedef struct {
    uint16_t target;   /* 9-bit instrument_param_id_t (bits 8..0) */
    uint8_t  value;    /* 7-bit (0..127) */
} pat_automation_entry_t;
```

For the full step read (specials + automations together):

```c
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t probability;
    uint8_t flags;
    uint8_t auto_count;
    pat_automation_entry_t autos[63];
} pat_step_data_t;
```

`sizeof(pat_step_data_t)` = 4 + 1 + 63×4 = 257 bytes. Stack-safe.

### 2.5 pat_writeSpecials() impact

The existing `pat_writeSpecials()` (`PatternData.c:372-428`) performs the
read-modify-write for specials changes. It calls `pat_blockChunks()` to decide
whether to reallocate. With automation, it must preserve existing automation
entries across a specials-only edit: read the current block (specials + autos),
modify only the specials, rewrite the full block (specials + same autos). The
chunk count may change because the specials size changed, but the automation
count stays the same.

### 2.6 Existing stubs to replace

The following stubs exist from the old AVR 2-lane automation model and must be
completely replaced (not extended):

- `pat_setStepAutomationDestination()` — `PatternData.c:797`, no-op. The old
  signature takes a `slot` (lane 0 or 1) and a `uint16_t value` (the resolved
  `instrument_param_id_t` from `modTargets[].param` in `menu.c:9598`). The new
  model has no fixed lanes.
- `pat_setStepAutomationValue()` — `PatternData.c:798`, no-op. Same old 2-lane
  model.
- `pat_setActiveAutomationTrack()` — `PatternData.c:795`, no-op.
- `pat_setSelectedStep()` — `PatternData.c:796`, no-op.

The legacy menu integration at `menu.c:9582-9623` (the `modTargets[]` array
index conversion for PAR_P1_DEST/PAR_P2_DEST) must also be replaced.

---

## 3. Method 1 — Step Edit Menu Automation Pages

### 3.1 Entry context

The user is in MODE_STEP (third mode button) and has pressed a SEQ button to
select a step for editing. The existing step-edit menu shows a fixed set of
"specials" pages: note, velocity, probability (more coming). After those fixed
pages, dynamically generated automation pages appear.

The step-edit flow currently works through `pat_applyStepToMenu()`
(`PatternData.c:830-837`) which copies resolved specials into
`parameter_values[PAR_STEP_NOTE/VOLUME/PROB]`. The automation pages are new
additional pages after the existing specials pages.

### 3.2 Page layout

Each automation page shows one automation entry:

```
Top row:    "nnn voi par amt >"
Bottom row: " del   <v> <p> <a>"
```

| Position | Top row | Bottom row | Behavior |
|----------|---------|------------|----------|
| Item 0 | `nnn` — zero-based automation index (e.g. `000`, `001`) | `del` or `clr` — pot turn toggles between `del` (delete this entry) and `clr` (clear all entries on this track with same voice+parameter); encoder click executes the displayed action | |
| Item 1 | `voi` — label | `<v>` — target voice number (1..6), turned by pot, displayed as 1-based | |
| Item 2 | `par` — label | `<p>` — target parameter's first 3 characters of `ParamDescriptor::short_name` (`InstrumentManager.h:137`), turned by pot. Displays `inv` if the stored target does not resolve to a valid descriptor on the current instrument (see §7.3) | |
| Item 3 | `amt` — label | `<a>` — automation value (0..127), turned by pot | |

The `>` at the end of the top row indicates more pages to the right.

### 3.3 Parameter cycling

The voice pot (item 1) cycles 1..6 (the 6 instrument slots). The parameter pot
(item 2) cycles through the target voice's automatable descriptors using the
existing `instrumentManager_stepTargetForSlot()` function
(`InstrumentManager.c:996-1071`) which already accepts
`INSTRUMENT_TARGET_AUTOMATION` as a use type and skips non-automatable
descriptors.

The parameter pot must additionally skip any `(voice, parameter)` combination
already present in another automation entry on this step (the uniqueness
invariant). This is a linear scan of the step's automation list for each
candidate target — cheap at counts ≤63.

When the voice changes, if the currently displayed parameter doesn't exist on
the new voice's descriptor table, or if the `(voice, parameter)` pair is already
taken, the parameter display shifts to the first valid available target.

### 3.4 Navigation

Scrolling right with the encoder moves to the next automation entry, one per
page. After the last existing automation, one additional "add" page:

```
Top row:    "nnn voi par amt"     (NO '>' — this is the last page)
Bottom row: " add   off off off"
```

If the user clicks "add" (encoder press on item 0), or adjusts any of the three
value pots away from `off`:

1. A new automation entry is written to the step's pool block. Default values:
   the step's own track voice as target, the first valid automatable parameter
   of that voice's instrument (from `instrumentManager_stepTargetForSlot()` with
   `current = INSTRUMENT_PARAM_INVALID` and `direction = +1`), and that
   parameter's current Scene image value from
   `scene_instrumentSlotConst(scene, slot)->parameter_images
   .instrument_parameters[descriptor_index]`.
2. The page transforms into a normal editing page (`add` → `del`, values
   populate, `>` appears).
3. One more "add" page becomes available on further right scroll.

### 3.5 Delete and clear behavior

**`del` (delete this entry):** Encoder click on item 0 when showing `del`:

- Pool read-modify-write: read current block, remove the entry at the current
  index, rewrite with `auto_count - 1`, free/realloc if chunk count changed.
- No confirmation screen.
- If remaining automations exist, show the next one (or previous if deleted was
  last). If none remain, the page becomes the "add" page.

**`clr` (clear track-wide):** Encoder click on item 0 when showing `clr`:

- Searches all 128 steps of the current track for automation entries with the
  same 9-bit target (same voice + same parameter) as the current entry.
- Deletes all matching entries found, including the entry on the current step.
- Each affected step gets an independent pool read-modify-write.
- This is the primary cleanup mechanism for stale entries after an instrument
  swap (see §7.3). When a parameter shows `inv`, the user uses `clr` to
  remove all instances of that orphaned target from the track.
- After completion, the page shows the next remaining entry, or the "add" page
  if none remain.

### 3.6 Implementation notes

- Track `current_auto_page_index` state variable in the step-edit menu.
- The "add" page index is always `auto_count` (one past the last entry).
- Total page count for step-edit: `fixed_specials_pages + auto_count + 1`.
- On step selection change, `current_auto_page_index` resets to 0.
- Pool exhaustion during add: the add operation fails silently (the step retains
  its current state, the page stays on "add"). No state corruption.

---

## 4. Method 2 — VOICE Menu Held-Step Automation Overlay

### 4.1 Entry context

The user is on the VOICE page. They press and hold one or more SEQ buttons.
While buttons are held, the VOICE page becomes an automation overlay for the
currently viewed voice's track.

Use a short long-press, configured globally as `BUTTON_HOLD_DELAY_MS` in
`config.h` and initially 100 ms. This replaces the private `BUTTON_TIMEOUT`
threshold and is shared by every UI gesture that distinguishes hold from tap.

The Pattern-wide name indicator described below remains active on ordinary
VOICE pages even when no step is held. All behavior applies in the compact
four-parameter and clicked-in single-parameter views, including the persistent
SHIFT+VOICE Morph endpoint view. `S065_DYN_PAT_VOICE_PARAM_UX.md` is the
detailed implementation authority for this method.

### 4.2 Visual behavior: one marker per parameter

Each displayed parameter uses at most one underlined character:

- **Pattern-wide automation:** if the parameter is automated on any step of the
  current track/Pattern, underline the leftmost character of its short name in
  overview or its long name in clicked-in view.
- **Held-step automation:** for each parameter, find the most recently pressed,
  still-held step containing automation for that exact target. Display that
  automated value and underline only the rightmost non-space character of the
  value. Do not underline that parameter's name.

The held-step rule has precedence per parameter. Different visible parameters
may source their values from different held steps. Whenever a value is
underlined it must be an automation value for that exact target, never the
saved normal or Morph endpoint value.

A small async foreground agent scans all 128 steps for the Pattern-wide result.
Held-step matching reads the small held set directly and does not wait for that
scan.

#### LCD underline mechanism

The HD44780-compatible LCD has 8 CGRAM (user-defined character) slots. Each
slot holds one 5×8 pixel glyph. At init (`lcd.c:332-339`):

- Slot 0: ellipsis character (`lcd_char_ellipsis`)
- Slot 1: pop/bar character (`lcd_char_pop`)
- Slots 2-7: boot splash logo characters (`lcd_splash_char_1` through `_6`)

The splash characters are used only at boot and are dead at runtime. **Slots
2-7 (6 slots) are free for runtime use.** Slots 0 and 1 are used by the menu
(`...` for Load/Save operations, pop bar for level displays).

Only `0-9`, `A-Z`, and `a-z` may be underlined: 62 glyphs stored as 8-byte
CGRAM row images, for a 496-byte flash table sourced from the matching WS0010
ROM font. Define the chosen glyph with the bottom row set to `0x1F` (all 5 dots
on), then emit that slot code at the marker location.

#### Slot budget analysis

There are at most four visible parameters and each receives at most one marker,
so the hard worst case is four distinct underlined glyphs. Use slots 2..5 as a
four-entry cache and leave slots 6 and 7 free. A clicked-in view needs only one
slot. No indicator is dropped. During rapid held-step value editing, show the
new value immediately without its underline and reapply it once after
`VOICE_AUTOMATION_UNDERLINE_QUIET_MS` (initially 100 ms) of foreground quiet,
avoiding repeated CGRAM rewrites.

### 4.3 Step illumination in single-parameter view

When the VOICE overlay is active (any SEQ button held) AND the user is in
single-parameter view (encoder click-in on a specific parameter):

- The SEQ button LEDs change from their normal illumination (steps with active
  trigger) to showing only the steps that carry an automation entry for the
  currently viewed parameter, on the current track, targeting the track's own
  voice.
- Steps without automation for that parameter are unlit, regardless of trigger
  state.
- Every physically held step remains illuminated so the edit selection is not
  hidden.

Normal trigger-based illumination is restored when:
- All SEQ buttons are released (overlay exits);
- The user exits single-parameter view (encoder click-out to overview);
- These transitions are immediate.

While in this mode, the user can still change bar, track, and SELECT page. The
step illumination must update immediately on any of these changes:
- **Bar change**: re-read the new bar's steps for automation presence on the
  current parameter and track.
- **Track change**: the parameter set changes (different voice's descriptors),
  so re-evaluate which steps carry the now-current parameter's automation.
- **SELECT page change**: different parameter, re-evaluate step illumination.
- All of these are foreground operations reading the pool blocks for the visible
  16-step range.

### 4.5 Value display behavior

For each displayed parameter, walk held steps from newest press to oldest and
use the first step containing an automation entry for that exact target:

- The value display shows the automated value (after 7-bit → 8-bit expansion),
  never the saved normal or Morph endpoint.
- Only the rightmost non-space character of the value is underlined, in both
  overview and single-parameter views. The name is not underlined.

If no held step automates this exact parameter:

- The value shows the selected normal or Morph endpoint and is not underlined.
- If the parameter is automated elsewhere in the Pattern, its name retains the
  Pattern-wide first-character underline.

### 4.6 Writing automations via parameter adjustment

While holding SEQ buttons, if the user turns an endless pot or the clicked-in
encoder to adjust a displayed parameter:

- An automation entry is written for that parameter on **every held step**.
  The target is the track's own voice (there is no cross-voice targeting from
  this method). The `instrument_param_id_t` is
  `instrumentParam_make(track_voice_slot, descriptor_index)`.
- If a held step already has that `(voice, parameter)` automated, the existing
  entry's 7-bit value is updated in place (no new allocation needed if the
  block size doesn't change).
- If a held step does not have that parameter automated, a new entry is added
  (pool read-modify-write, may reallocate if chunk count grows).
- An existing matching held automation supplies the edit's starting value. If
  none exists yet, the first edit may start from the visible normal/Morph
  endpoint, but the value marker appears only after that adjusted result has
  been stored successfully as automation.
- The same resulting automation value is broadcast to every held step. After
  at least one successful write, the display sources an actual held-step
  automation value, removes the name underline, shows the new value immediately
  in ROM characters, and reapplies its one-character value underline after the
  configured value-edit quiet period.

Best-effort semantics for multi-step writes: if the pool fills partway through
or a step reaches the 63-entry ceiling, successfully written steps keep their
automation; steps that failed are unchanged. No notification — fall-through.

**Endpoint safety is absolute:** while any step is held, neither the normal nor
Morph endpoint, its Menu mirror, nor its Autosave state may be changed. With
playback running, the UI performs only the Pattern automation writes. With
playback stopped, it may additionally send the value through an audited
runtime-DSP-only path for audible preview. That preview is ephemeral and is not
an endpoint write. If the implementer cannot prove the distinction, omit the
preview and write only the Pattern automation.

### 4.7 Pot input handling

The VOICE page uses endless pots with atan2 delta tracking
(`endlessPots.c`). These pots report angular deltas, not absolute positions.
Pressing SEQ buttons does not generate a pot delta. Only actual pot rotation
creates a delta, so there is no dead-zone/pickup problem — the user must
physically turn the pot to trigger a write.

### 4.8 Implementation notes

- Detect "any SEQ button held" state to enter overlay mode.
- Retain held buttons in press order. Walk newest-to-oldest, read each held
  step once, and resolve the newest exact-target match independently for each
  visible parameter.
- Underline rendering updates on: SEQ button press/release, parameter page
  scroll, Pattern scan completion, automation write, and the configured
  value-edit quiet-period expiry.
- Budget the Pattern-wide scan with
  `VOICE_AUTOMATION_SCAN_STEPS_PER_PASS` in `config.h`, initially 4 steps. At
  the 63-entry per-step ceiling this is at most 252 automation entries/target
  comparisons per foreground pass.
- On full SEQ button release, exit overlay and restore normal VOICE display
  while retaining Pattern-wide name indicators. CGRAM slots 2..5 can stay
  defined until reused; ordinary pages stop emitting their codes.
- Writing to up to 16 steps is a foreground loop, each step doing one
  independent pool read-modify-write. Bounded at 16 iterations.

---

## 5. Sequencer Playback

### 5.1 Current path

Step advance lives in `seq_advanceTrackStep()` at `sequencer.c:388-409`. The
current flow for an active, unmuted step is:

1. `pat_isStepActive()` checks bit 15 of the address entry.
2. `pat_readStepSpecials()` reads note, velocity, probability from the pool.
3. Probability is evaluated against the hardware RNG.
4. `seq_triggerVoice()` fires the note at the resolved velocity.

Automation is not read or applied.

### 5.2 Required behavior

On each step advance, for every step that has a pool block (bit 14 set and a
valid offset, regardless of trigger state per `SCOPING_TARGETS.md` §4.6):

1. Read the step's automation entries from the pool block.
2. For each entry, resolve the 9-bit target to a voice slot + descriptor index
   (or a Scene target for IDs ≥ 384).
3. Expand the 7-bit value to 8-bit: `(v == 127) ? 255 : v * 2`.
4. Apply the value to the runtime DSP state.

### 5.3 ISR safety — instrumentManager_writeRuntime() is NOT ISR-safe

`instrumentManager_writeRuntime()` (`InstrumentManager.c:2993-2997`) delegates
to `instrumentManager_writeRuntimeInternal()` (`InstrumentManager.c:2874`),
which:

- Calls `instrumentManager_writeSpecialRuntime()` (`InstrumentManager.c:2742`),
  a function that does **`strcmp()` on `descriptor->file_key` strings** for
  every special-case parameter (filter freq/reso, amp envelope, pitch envelope,
  transient, distortion, LFO rate — about 15 strcmp calls in the worst path).
- Calls `instrumentManager_noteRuntimeValueChanged()` which iterates all 12
  LFO descriptor adapter slots (`InstrumentManager.c:1921-1929`).
- For `INSTRUMENT_BIND_INSTANCE_OFFSET` parameters, performs pointer arithmetic
  on the runtime instance and writes a typed `Parameter` value.

The `strcmp` chain and the modulation-baseline refresh loop make this function
**unsuitable for the TIM3 ISR** (priority 2, 4 kHz tick rate, ~250 µs budget).
It is a foreground function.

### 5.4 Foreground automation application

Automation values must be applied from the foreground, not from TIM3. The
design:

1. In the TIM3 ISR step-advance path, when a step has automation entries (bit 14
   set, valid offset, auto_count > 0), copy the decoded automation entries into
   a pending buffer and set a drain flag.
2. In the foreground main-loop pass (the same loop that services
   `timebase_serviceFrontPanel()`, `filesystem_tick()`, and the audio render),
   check the drain flag. If set, iterate the pending entries and call
   `instrumentManager_writeRuntime()` for each, then clear the flag.

#### Buffer sizing and debounce

The pending buffer is a flat array of 32 entries, each containing
`(step_id, target, value)` where `step_id` = `track * 128 + step` and `target`
is the 9-bit `instrument_param_id_t`.

**Debounce rule:** multiple writes to the same `(step_id, target)` pair
coalesce — the ISR overwrites the existing entry's value rather than appending
a duplicate. This prevents buffer saturation when the same step's automation
is re-evaluated before the foreground drains (e.g., a step that loops back
before the foreground pass, or overlapping tick/drain timing). The debounce
scan is linear over at most 32 entries per insertion.

Entries from DIFFERENT steps targeting the same runtime parameter are NOT
coalesced — they both stay in the buffer. The foreground applies them in order;
the later value overwrites the earlier one through the normal
`instrumentManager_writeRuntime()` path.

If the buffer is full and no coalesce match is found, the new entry is dropped.
In practice, 32 entries exceeds any realistic simultaneous automation load
(7 tracks × 1 step each × a few automations per step).

#### Race protection

The ISR is the sole writer. The foreground is the sole reader/drainer. The
drain flag is set by the ISR after writing; the foreground clears it after
reading all entries and resetting the write index. Since the foreground cannot
preempt TIM3, the ISR can safely append/overwrite entries between foreground
drain passes. A `volatile` qualifier on the drain flag and write index is
sufficient; no critical section needed.

#### Latency

The automation value is applied within one foreground pass of the step advance
— sub-millisecond in practice. This is imperceptible.

### 5.5 Default automation persistence

The default automation behavior: any parameter automation that occurs on a
voice's triggering step or later applies to that voice's parameters until the
voice's next trigger. In practice, this means:

- `instrumentManager_writeRuntime()` sets the DSP runtime state, which persists
  naturally until something else overwrites it.
- On the voice's next trigger, the voice reinitializes from the Scene image
  defaults (or from automation entries on the new triggering step, if any).
- No explicit "un-apply" or hold-tracking logic is needed this session. The
  runtime state IS the hold.

The full hold-and-reset FLAG model (`SCOPING_TARGETS.md` §4.3a) is deferred.
It would add explicit hold/reset semantics that modify this default behavior.
The flag model is additive and does not change the storage format.

---

## 6. PAT4 File Compatibility

The PAT4 format (`PATTERN_DYNAMIC_STACK.md` §7) streams the raw pool bytes and
bitmap. Automation entries live inside pool blocks. Therefore:

- **No file format change is needed.** A PAT4 file written by firmware with
  automation entries is read correctly by the existing reader (pool bytes are
  opaque to the file layer).
- A PAT4 file written by older firmware (auto_count always 0) is read correctly
  by new firmware.
- The file format version remains 1. The header `format_version` does not change
  because the file geometry and CRC calculation are unchanged.
- Pattern AutoSave uses the same `pat_snapshotScene()` memcpy of the raw region;
  automation entries in pool blocks are captured automatically.

Host validators that perform allocator-graph audits should be updated to
understand blocks with auto_count > 0.

---

## 7. Risk Cases

### 7.1 Pool fragmentation under automation churn

Adding and removing automation entries changes block sizes, causing
allocate/free cycles. The existing first-fit allocator has no compaction
(`PATTERN_DYNAMIC_STACK.md` §3). Automation adds a new source of fragmentation
but does not change the fundamental allocator contract. The future
defragmentation design (`SCOPING_TARGETS.md` §4.3) addresses this.

### 7.2 Pool exhaustion and 63-entry limit during multi-step VOICE write

Method 2 writes to up to 16 steps in one gesture. Each write may grow a block.
A write fails if the pool cannot allocate the larger block OR if the step has
already reached the 63-entry ceiling. Best-effort semantics: successfully
written steps keep their automation; steps that failed are unchanged. No
notification — the user sees partial underlining for the steps that succeeded.
Fall-through, no error display.

### 7.3 Automation target validity after instrument swap

An automation entry targeting `instrumentParam_make(3, 12)` becomes invalid if
slot 3's instrument type changes (e.g., Drum to Cymbal with a different
descriptor table). The 9-bit ID still points at an index that may not exist or
may mean a different parameter on the new type.

**At playback time:** validate the target before applying. If the descriptor
index is out of range for the current instrument type's `descriptor_count`,
skip the entry silently. No error, no crash — the entry is inert.

**At edit time (Method 1):** stale entries are NOT automatically cleaned.
Instead:

- The parameter display (item 2) shows `inv` (invalid) when the stored 9-bit
  target does not resolve to a valid descriptor on the current instrument.
  The voice and amount fields still display their stored values.
- The item 0 action pot offers `clr` (in addition to `del`). Clicking `clr`
  searches all 128 steps of the track for automation entries with the same
  voice and parameter target and deletes them all (see §3.5). This is the
  intended cleanup path after an instrument swap.
- The user can also change the voice (item 1) or parameter (item 2) pots to
  re-target the entry to a valid destination, effectively repurposing it.

**At edit time (Method 2):** the voice/parameter selector only shows currently
valid targets. Stale entries are not visible in the overlay — they can only be
managed through Method 1's step-edit page.

### 7.4 Scene target validator gap

As documented in §1.2, `instrumentManager_targetValid()` currently rejects all
IDs ≥ 384. This must be extended before Scene targets (per-voice Morph, Scene
Decimation) are reachable from the automation parameter picker. The extension
should call through to `sceneModTarget_valid()` for Scene-range IDs when
`use == INSTRUMENT_TARGET_AUTOMATION`.

### 7.5 CGRAM slot lifecycle

Slots 0 (ellipsis) and 1 (pop) must not be touched. The six boot-only splash
slots 2..7 are runtime-free, but the automation indicator may use only slots
2..5. Its four-entry cache records the rendered base character assigned to
each slot and redefines only changed entries. Slots 6 and 7 remain available.

Before redefining a cached slot, replace stale DDRAM references with their ROM
characters; then define the new glyph and write its new references. Preflight
the whole ordered transaction against the async LCD queue so it is deferred as
a unit rather than partially enqueued. On leaving VOICE pages the definitions
may remain, but no ordinary page emits their slot codes.

---

## 8. Implementation Order

1. **Extend `pat_blockChunks()`** to accept `auto_count`. Update all existing
   callers to pass 0.
2. **Extend `pat_blockWrite()`** to accept and write automation entries after
   specials. Update all existing callers to pass NULL/0.
3. **Extend `pat_blockRead()`** — add a separate `pat_blockReadAutomations()`
   that reads just the automation entries from a pool offset. Keep
   `pat_blockRead()` unchanged for specials-only callers.
4. **Add `pat_readStepAutomations()`** — public API returning a step's
   automation entries (scene/track/step → array + count). Validates coordinates
   and pool offset.
5. **Add `pat_writeStepAutomation()`** — add or update one automation entry on
   a step. Read-modify-write: read current block, find or append the entry,
   rewrite. Enforces the uniqueness invariant.
6. **Add `pat_removeStepAutomation()`** — remove one entry by 9-bit target.
   Read-modify-write: read, remove, rewrite.
7. **Add `pat_removeTrackAutomationByTarget()`** — search all 128 steps of a
   track for automation entries matching a given `(voice, parameter)` target and
   remove them all. Used by the `clr` action in Method 1.
8. **Extend `instrumentManager_targetValid()`** for Scene-range automation
   targets.
9. **Replace the four legacy stubs** (`pat_setStepAutomationDestination`,
   `pat_setStepAutomationValue`, `pat_setActiveAutomationTrack`,
   `pat_setSelectedStep`) and the legacy `menu.c` `modTargets[]` integration
   with the new dynamic API.
10. **Implement Method 1 menu pages** — step-edit automation page rendering,
    navigation, add/delete/clear, `inv` display for stale targets, parameter
    cycling with duplicate skip.
11. **Implement Method 2 VOICE overlay** — press-ordered held-step detection,
    per-parameter exact-target value sourcing, bounded four-slot CGRAM marker
    rendering from the 496-byte alphanumeric table, configured hold and
    value-underline delays, step illumination in single-parameter view,
    normal/Morph display integration, and an endpoint-bypassing
    parameter-to-automation write path.
12. **Implement async track-wide automation search** — polled foreground agent
    that scans 128 steps for every automatable descriptor on the current track.
    Runs while VOICE pages are active, processes the configured four steps
    (at most 252 entries) per foreground pass, and updates the
    first-name-character indicators when complete.
13. **Implement sequencer playback** — TIM3 copies pending entries to the
    32-entry debounced buffer; foreground drains it through
    `instrumentManager_writeRuntime()`.
14. **Test on hardware** — create automation via both methods, verify playback
    applies values audibly, test `inv`/`clr` after instrument swap, test step
    illumination in single-parameter view, exercise four simultaneous markers
    and the configured reapplication delay in normal and SHIFT+VOICE Morph
    views, prove both endpoint images and Autosave state remain unchanged by
    held-step edits, save/load PAT4, and confirm automations survive.

---

## 9. Resolved Architectural Decisions

| # | Question | Resolution |
|---|----------|------------|
| 1 | Automation on inactive steps | **Implement now.** Per `SCOPING_TARGETS.md` §4.6, automation applies regardless of trigger state. Additionally: in single-parameter VOICE view with a held step, SEQ LEDs show only steps carrying automation for the viewed parameter (§4.3). |
| 2 | LCD underline rendering | **CGRAM for all VOICE views, bounded to four slots.** Each visible parameter has exactly one possible marker: first name character for Pattern-wide presence, or rightmost value character when a held step supplies automation for that exact target. Held value wins and can never display an endpoint. Slots 2..5 suffice; a 62-glyph alphanumeric table costs 496 flash bytes; value-marker reapplication uses the configured 100 ms edit quiet period (§4.2). |
| 3 | Sequencer automation context | **Foreground only.** TIM3 copies decoded entries to a 32-entry pending buffer with `(step, target)` debounce; foreground drains via `instrumentManager_writeRuntime()` (§5.4). |
| 4 | Multi-step write failure | **Best-effort, fall-through.** Pool exhaustion and 63-entry ceiling both cause silent failure on the affected step. No notification (§7.2). |
| 5 | Automation persistence | **Persists until next trigger.** The DSP runtime state holds the value naturally. No explicit hold/reset tracking this session. Hold/reset flags are deferred and additive (§5.5). |
| 6 | Stale entries after instrument swap | **`inv` display + `clr` track-wide cleanup.** Invalid targets show `inv` in the Method 1 parameter field. `clr` action searches all 128 steps for matching voice+parameter and deletes them. Playback skips invalid entries silently (§7.3). |
| 7 | Scene target validation | **Extend `instrumentManager_targetValid()`.** Accept Scene-range IDs (384+) when `use == INSTRUMENT_TARGET_AUTOMATION`, delegating to `sceneModTarget_valid()`. Required before Method 1 can offer Scene targets (§1.2). |
| 8 | Pending-automation buffer | **32 entries, debounced by `(step, target)`.** Multiple writes to the same step and parameter coalesce; different steps targeting the same parameter are preserved. Overflow drops the new entry (§5.4). |
| 9 | Hold recognition | **Short configurable long-press.** `BUTTON_HOLD_DELAY_MS` lives in `config.h`, starts at 100 ms, replaces the private `BUTTON_TIMEOUT`, and is common to all hold-vs-tap UI gestures (§4.1). |
| 10 | Endpoint/runtime behavior during held edits | **Never mutate endpoints.** Running playback receives Pattern writes only. Stopped playback may additionally receive an ephemeral runtime-DSP-only preview; if that boundary is unclear, omit preview (§4.6). |
| 11 | VOICE-overlay retained SRAM | **40 bytes approved.** One Menu-owned, two-byte-aligned `.bss` block with runtime lifetime, as itemized in `S065_DYN_PAT_VOICE_PARAM_UX.md` §10. Any increase requires new approval. |

---

## 10. Session Split

This general plan is split into two implementation sessions:

**Session 065** (`S065_DYN_PAT_STEP_AUTOM_EDITING.md`): Core pool block
extension, PatternData read/write/remove APIs, Method 1 step-edit menu pages,
sequencer playback with foreground application buffer, legacy stub replacement,
and target validator extension. Delivers a working end-to-end path: create
automation in step-edit mode, hear it play back, save/load via PAT4.

**Session 066** (`S065_DYN_PAT_VOICE_PARAM_UX.md`): Method 2 VOICE overlay
(held-step automation writing, four-slot single-marker CGRAM underline,
per-parameter held value sourcing, normal/Morph view integration, step
illumination in single-parameter view, async Pattern-wide search agent), and
any non-essential polish or edge-case handling deferred from 065.
