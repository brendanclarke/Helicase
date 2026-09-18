# S067 — Automation Value Dtype Offset Bug

## Symptom

When assigning per-step parameter automation via the voice-page held-step
overlay (p-lock path), releasing, and re-holding:

1. **Filter type "UBP"** (menu index 3) reads back as **"BP"** (index 2).
2. **Pan** values above display −15 read back one less than assigned (e.g.
   +64→+63, +10→+9, −14→−15). Values at −15 and below that happen to map to
   even internal values read back correctly.
3. The DSP runtime receives the same wrong value: the Sequencer doubles the
   stored 7-bit value before applying it, so playback matches the erroneous
   readback — the automation does not sound correct either.

## History

This is the same fundamental bug that caused S066 Fixes 1 and 4 (session
handoff log 066, lines 222–279). Those fixes added a working-value cache
(`va_workingValue[4]`) and nibble-split suppression to mask the lossy
8→7→8 round-trip during consecutive pot/encoder detents. The cache keeps
the 8-bit value alive between edits so the user doesn't see the truncation
until the held mask changes or the overlay exits. The root cause was never
addressed: the conversion itself is wrong for every automatable parameter.

## Root Cause

The 7-bit automation storage uses a MIDI CC-style `value / 2` (encode) and
`value * 2` (decode) conversion, borrowed from the original MIDI CC path
(documented in 065 session handoff log §6.3: "Same as MIDI CC path"). This
conversion exists because MIDI CC maps 0..127 to 0..255 for
`parameter_values[]`, where every parameter is stored in a flat uint8_t
array regardless of its actual range.

**But automation targets are instrument descriptor parameters, not
`parameter_values[]` entries.** Every descriptor parameter that carries
`INSTRUMENT_PARAM_FLAG_AUTOMATABLE` is declared with a dtype whose range
already fits in 7 bits:

- `DTYPE_0B127` range 0..127 — fits in 7 bits as-is
- `DTYPE_PM63` range 0..127 (display −63..+64) — fits as-is
- `DTYPE_MENU` range 0..N (N ≤ 8 typically) — fits as-is
- `DTYPE_ON_OFF` range 0..1 — fits as-is
- `DTYPE_MIX_FM` range 0..1 — fits as-is
- `DTYPE_LFO_POLARITY` range 0..2 — fits as-is
- `DTYPE_NOTE_NAME` range 0..127 — fits as-is
- `DTYPE_1B16` range 1..16 — fits as-is

**No `DTYPE_0B255` parameter is automatable today.** The only parameters
declared as `DTYPE_0B255` are:

| Parameter | Declared at | Automatable? |
|-----------|-------------|-------------|
| `PAR_MORPH` | menu.c:970 | No — global parameter, not a descriptor |
| `PAR_VOICE1_MORPH`..`PAR_VOICE6_MORPH` | menu.c:971–976 | No — global, not a descriptor |
| `PAR_BPM` | menu.c:1005 | No — global, not a descriptor |

None of these are instrument descriptors. They live in `parameter_values[]`
(the flat global array) and cannot be targets of per-step automation, which
only addresses descriptor parameters through `instrumentParam_make()`.

**The `/2` and `*2` conversion therefore serves no purpose for any currently
automatable parameter.** It was introduced under the assumption that
automation values work like MIDI CC, but they don't — they are direct
descriptor-domain values. The normal parameter edit path
(`menu_encoderChangeParameter()` → `menu_clampCellValue()` →
`menu_cellCommitValue()`) works correctly because it reads and writes the
raw parameter byte without any conversion. The step automation page's own
edit path (`menu_stepAutomationEdit()` field 3) also works correctly — it
operates on the raw 7-bit stored value directly.

The only thing that is wrong is the four code sites that apply `/2` or `*2`.

## Affected Code Sites

### Site 1 — Voice overlay write (encodes 8→7)

`menu.c` line 2387 in `va_writeAutomationFromKnob()`:
```c
stored7 = (value >= 255u) ? 127u : (uint8_t)(value / 2u);
```

Halves the clamped 8-bit value before writing to Pattern storage.

### Site 2 — Voice overlay read (decodes 7→8)

`menu.c` line 1817 in `va_expand7to8()`:
```c
return (value == 127u) ? 255u : (uint8_t)(value * 2u);
```

Called at lines 2216, 2265, and 2372. Doubles the stored value for display
and for seeding the next edit.

### Site 3 — Sequencer playback (decodes 7→8)

`sequencer.c` lines 607–608 in `seq_drainPendingAutomation()`:
```c
uint8_t value8 = (value7 == 127u) ? 255u :
                 (uint8_t)(value7 * 2u);
```

Doubles the stored value before passing to `instrumentManager_writeRuntime()`.
The DSP receives a doubled value, so automation playback is wrong.

### Site 4 — Step automation "Add" (encodes 8→7 at creation)

`menu.c` line 8047 in `menu_stepAutomationAddDefault()`:
```c
value = (uint8_t)(value >= 255u ? 127u : (value / 2u));
```

Reads the current parameter image byte and halves it when creating a new
automation entry. This means a newly created automation entry starts at half
the current endpoint value.

## What Works Correctly (and Why)

The step automation page's edit and display paths are correct because they
operate on the raw stored 7-bit value directly:

- **Edit** (`menu_stepAutomationEdit()` field 3, line 8177): reads
  `autos[page].value`, adds `inc`, clamps to `menu_automationValueMax()`,
  writes raw value back.
- **Display** (`menu_formatAutomationValue3()`, line 7844): interprets the
  raw 7-bit value using dtype-specific formatting — `DTYPE_PM63` does
  `value - 63`, `DTYPE_MENU` indexes the menu table directly.
- **Value max** (`menu_automationValueMax()`, line 7806): returns the correct
  7-bit maximum per dtype (e.g. MENU_FILTER returns 7, ON_OFF returns 1).

These three functions prove that the design intent for 7-bit automation
storage is **identity mapping** — the stored value IS the parameter value.
The `/2` and `*2` are leftover from a MIDI CC analogy that doesn't apply.

## Disagreement Between Paths

The halving/doubling creates a contradiction between the step automation page
(which is correct) and the voice overlay + Sequencer (which halve/double):

| Written via | Stored 7-bit | Step auto page shows | Overlay shows | Sequencer plays |
|-------------|-------------|---------------------|---------------|-----------------|
| Voice overlay (UBP, param 3) | 1 (halved) | "HP" (wrong) | "BP" (wrong) | BP (wrong) |
| Step auto page (UBP, raw 3) | 3 (correct) | "UBP" (correct) | "LP2" (doubled) | Nch (doubled) |

Neither path agrees with the other. The step automation page operates
correctly in isolation, but the Sequencer's `*2` expansion corrupts its
values during playback.

## Trace: Filter Type UBP via Voice Overlay

| Stage | Value | Domain |
|-------|-------|--------|
| User selects UBP | 3 | 8-bit parameter |
| Working cache (correct during edit) | 3 | 8-bit |
| Write: `3 / 2` → stored | **1** | 7-bit (WRONG) |
| Release + re-hold | | |
| Read: `va_expand7to8(1)` → `1 * 2` | 2 | 8-bit |
| Display: `filterTypes[2+1]` | "BP" | **expected "UBP"** |
| Sequencer: `1 * 2 = 2`, runtime `2 + 1` | 3 (BP) | **expected 4 (UBP)** |

## Trace: Pan +10 via Voice Overlay

| Stage | Value | Domain |
|-------|-------|--------|
| User sets +10 | 73 | 8-bit (63 + 10) |
| Working cache (correct during edit) | 73 | 8-bit |
| Write: `73 / 2` → stored | **36** | 7-bit (WRONG) |
| Release + re-hold | | |
| Read: `va_expand7to8(36)` → `36 * 2` | 72 | 8-bit |
| Display: `72 − 63` | **+9** | **expected +10** |
| Sequencer: `36 * 2 = 72` → pan register | 72 | **expected 73** |

Display −15 (param 48, even) round-trips correctly: `48/2 = 24`, `24*2 = 48`,
`48 − 63 = −15`. The bug affects only odd parameter values.

## Specific Menu Items Affected (Odd Index = Bug)

| Menu table | Entries | Shift on odd-index assignment |
|------------|---------|------------------------------|
| MENU_FILTER | LP(0) HP(1) BP(2) **UBP(3)** Nch(4) **Pek(5)** LP2(6) **off(7)** | UBP→BP, Pek→Nch, off→LP2 |
| MENU_WAVEFORM | sine(0) **tri(1)** saw(2) **rec(3)** noise(4) ... | tri→sine, rec→saw |
| MENU_LFO_WAVES | sine(0) **tri(1)** saw(2) **rec(3)** rnd(4) **s+h(5)** | tri→sine, rec→saw, s+h→rnd |
| MENU_RETRIGGER, MENU_TRANS | (similar pattern) | odd entries shift to previous |

`DTYPE_ON_OFF` and `DTYPE_MIX_FM` are catastrophically broken: ON (1) stores
as `1/2 = 0` (OFF). It is impossible to store ON via the voice overlay.

## Proposed Fix

Remove the `/2` and `*2` conversion entirely. Every automatable parameter
today has a range ≤ 127 and fits in the 7-bit automation storage field
without any compression. The stored value should equal the parameter value.

### Site 1 — `va_writeAutomationFromKnob()` (menu.c:2387)

Replace:
```c
stored7 = (value >= 255u) ? 127u : (uint8_t)(value / 2u);
```
With:
```c
stored7 = (value > 127u) ? 127u : (uint8_t)value;
```

### Site 2 — `va_expand7to8()` (menu.c:1815–1818)

This function should become the identity. Either inline `value` at call sites
or replace the body:
```c
static uint8_t va_expand7to8(uint8_t value)
{
    return value;
}
```

### Site 3 — `seq_drainPendingAutomation()` (sequencer.c:607–608)

Replace:
```c
uint8_t value8 = (value7 == 127u) ? 255u :
                 (uint8_t)(value7 * 2u);
```
With:
```c
uint8_t value8 = value7;
```

### Site 4 — `menu_stepAutomationAddDefault()` (menu.c:8047)

Replace:
```c
value = (uint8_t)(value >= 255u ? 127u : (value / 2u));
```
With:
```c
value = (value > 127u) ? 127u : value;
```

### Working-value cache review

With the identity conversion in place, `va_workingValue[]` and the
nibble-split `va_underlineSuppressed` mechanism from S066 Fixes 1/4 still
serve a purpose: they prevent the display from re-reading the stored value
(which truncates at 127) while the user is turning the encoder. For
`DTYPE_0B127` parameters where the endpoint is ≤ 127, the stored value
would round-trip perfectly and the cache would technically be redundant.
However, the cache also provides a consistent seeding path that avoids
re-resolving the held value between detents, so it remains useful.

### Future voice morph automation

If/when `PAR_VOICE*_MORPH` (DTYPE_0B255, range 0..255) becomes automatable,
its conversion will need special handling. That case should be addressed at
that time with a dtype-conditional path:
```c
if (dtype == DTYPE_0B255)
    stored7 = (value >= 255u) ? 127u : (uint8_t)(value / 2u);
else
    stored7 = (value > 127u) ? 127u : (uint8_t)value;
```

This is not needed now — no DTYPE_0B255 parameter is reachable as an
automation target.

### Backwards compatibility

Existing stored automation values written via the voice overlay are in the
halved domain. Values written via the step automation page are in the correct
raw domain. After the fix, all paths will store raw values, and old halved
values from overlay writes will play back at half their intended parameter
value.

A migration is not practical for live pattern data (the system cannot
distinguish between a halved-domain value and a raw-domain value). This is
acceptable because the current state already produces wrong display and wrong
audio. The user will need to re-enter affected automation assignments — this
is not a regression since those assignments were already wrong.
