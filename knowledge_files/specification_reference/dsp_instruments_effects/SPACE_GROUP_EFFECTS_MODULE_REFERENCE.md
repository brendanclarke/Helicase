# Space Group Effects Module Reference

How the Space group effects (Delay, Reverb, Echo) work as firmware on the
LXR-02. This document covers the module layout, arena management,
registration, parameter tables, drain logic, slot lock, and build
integration. For the DSP design rationale and equations, see
`SPACE_GROUP_EFFECTS_MODELLING_REFERENCE.md`; for the effects bus
architecture, see `EFFECTS_MODULES_DSP_REFERENCE.md`.

- **Sessions:** S095 P1 (implementation), S095 P4 (bus tweaks).
- **Types:** `EFFECT_TYPE_DELAY` (2), `EFFECT_TYPE_REVERB` (3),
  `EFFECT_TYPE_ECHO` (4), all in the Space group.

---

## 1. Files

| File | Role |
|---|---|
| `Core/DSP/Effects/Space/SpaceKit.h` | Shared DSP primitives (inline), control block struct, seating/feed API declarations, constants |
| `Core/DSP/Effects/Space/SpaceKit.c` | Seating (`sk_seat`, `sk_feedBegin`, `sk_feedRead`, `sk_feedCut`), span computation, segment packing |
| `Core/DSP/Effects/Space/SpaceEffect.c` | Per-type init/process entry points: `space_initDelay`, `space_initReverb`, `space_initEcho`, `space_processDelay`, `space_processReverb`, `space_processEcho` |
| `Core/DSP/Effects/Space/SpaceParameters.c` | Shared parameter descriptor table, labels, format helpers (`space_formatValue`, `space_step`), cell-active predicates |
| `Core/DSP/Effects/Space/SpaceTables.c` | `const` flash tables: delay-time curve, halfband coefficients (`SPACE_HB_C1`/`C3`/`C5`), pitch-shifter window sizes |
| `Core/DSP/Effects/EffectsManager.c/.h` | Drain logic, logical/physical type management, slot-lock API, type-change orchestration |
| `Core/DSP/Effects/FxBuffer.h/.c` | Arena share struct (`fx_share_t`), handoff record (`fxbuf_handoff_t`, 196 B), `SK_PREFIX_BYTES` constant |
| `tools/gen_space_tables.py` | Generates halfband coefficients and delay-time curve |

Build: `SpaceKit.c` and `SpaceEffect.c` in `DSP_SRCS` with `-Ofast`;
`SpaceParameters.c` and `SpaceTables.c` in normal sources (`-O2`).

---

## 2. Registry

The three Space types share a single parameter descriptor and are
registered individually in the effect type table:

| Field | Delay | Reverb | Echo |
|---|---|---|---|
| Enum | `EFFECT_TYPE_DELAY` | `EFFECT_TYPE_REVERB` | `EFFECT_TYPE_ECHO` |
| ID | 2 | 3 | 4 |
| Display label | `Delay   ` | `Reverb  ` | `Echo    ` |
| Init function | `space_initDelay` | `space_initReverb` | `space_initEcho` |
| Process function | `space_processDelay` | `space_processReverb` | `space_processEcho` |
| File extension (effect save) | `.fx` | `.fx` | `.fx` |

All three types use the same parameter descriptor table in
`SpaceParameters.c`. Parameter visibility and formatting vary by type
through predicates and format helpers.

---

## 3. Arena Management

### 3.1 Arena prefix

The first 512 B of the DTCM arena (`SK_PREFIX_BYTES`) are reserved:
- Bytes 0–63: `sk_ctl_t` control block
- Bytes 64–511: type state header

The prefix is NEVER part of the ring memory. All ring addressing starts
at offset `SK_PREFIX_BYTES`.

### 3.2 Control block (`sk_ctl_t`, 64 B)

```c
typedef struct {
    uint32_t magic_owner;       // identifies the owning type
    uint32_t span_off;          // ring span start offset from share base
    uint32_t span_len;          // ring span length in bytes
    // Feed state:
    uint8_t  feeding;           // 1 while a feed is active
    uint8_t  slot_locked;       // 1 while an interpolation slot is locked
    uint8_t  feed_sides;        // number of feed sides (1 or 2)
    uint8_t  feed_bits;         // sample width: 8 or 16
    struct {
        uint32_t start, end;    // byte offsets of segment in old ring
        uint8_t  side, lane;    // which side and lane the segment serves
    } seg[4];                   // up to 4 segments
    float    gain[2];           // per-side gain (includes shift compensation)
    uint32_t pos[2];            // per-side read position (integer part)
    float    frac[2];           // per-side fractional accumulator
    int32_t  left[2];           // per-side samples remaining
    uint32_t stride;            // bytes per sample in source ring
    uint32_t wrap_off;          // wrap origin offset
    uint32_t wrap_bytes;        // wrap region length
    float    end_fade[2];       // per-side end-fade ramp (block start, end)
    uint16_t dry_uq15;          // initial dry ramp from effect_dry_uq15
} sk_ctl_t;
```

### 3.3 Type state header (448 B)

The remaining 448 B of the prefix hold type-specific runtime variables.
Each type casts this region to its own struct:

**Delay state** includes: write pointer, ring mask, left/right read
fractional positions, feedback filter states, tone filter state, ducking
envelope, LFO phase, pitch shifter state.

**Reverb state** includes: write pointer, ring mask, 4 input allpass
states, 2 delay-line states, 2 in-loop allpass states, 2 damping filter
states, 2 pitch shifter states, halfband decimator/interpolator states
(per channel), LFO phase.

**Echo state** includes: tape write pointer, tape ring mask, tape read
fractional position, tape feedback filter state, plate write pointer,
plate allpass states, damping filter state.

### 3.4 Elastic seating

`sk_seat()` divides the arena between the Effect and voice units:
1. The Effect declares its `seat_min` (minimum ring bytes).
2. Voice units (CrumpBit sub-types) declare their minimums.
3. Remaining space is allocated to the highest-priority consumer.
4. Each consumer receives a contiguous region starting at an aligned
   boundary.

**Growth at the wrap** (SD9): ring length changes only when the write
head crosses the old end. This means existing ring content is always
valid for reads.

---

## 4. EffectsManager Integration

### 4.1 Type change flow

When the user selects a new effect type:

1. EffectsManager records the outgoing type's handoff
   (`fxbuf_handoff_t`, 196 B)
2. If new type is OFF: enter drain mode (§4.2)
3. If new type is non-OFF: call `sk_feedBegin` to set up feed-through,
   then call the new type's init function
4. The init function reads the control block (populated by
   `sk_feedBegin`) to know its span and whether a feed is active

### 4.2 Drain into OFF

EffectsManager maintains two type variables:
- `logical_type`: what the user selected (OFF)
- `physical_type`: what is actually running (the old effect)

During drain:
- The old effect renders with muted input (dry ramp → 0)
- EffectsManager monitors the output via an envelope follower
- Drain ends at: output ≤ −80 dBFS, or 6 s elapsed
- At drain end: physical type → OFF, arena released

### 4.3 Slot lock

The EffectsManager exposes:
- `effects_setFeedSlotLock(1)` — lock one interpolation slot during feed
- `effects_setFeedSlotLock(0)` — release the slot when feed ends

The locked slot prevents wavetable frame blending from claiming it. Its
cost is charged to the Advanced instrument budget.

### 4.4 `effects_service()`

Runs every render block. Rescans all effect descriptors (no dirty
notifications). Processing order: Morph base → interpolate → FX lock →
Pattern overlay → LFO → clamps → `write_param` on change. This is
unchanged from the S072 architecture.

---

## 5. Parameter Table

### 5.1 Shared parameter set

All three Space types share one descriptor table in `SpaceParameters.c`.
Visibility and formatting differ by type through predicates.

### 5.2 Per-type parameter map

| Row | Delay | Reverb | Echo | Dtype | Notes |
|-----|-------|--------|------|-------|-------|
| `dly` | Delay time | — | Tape delay time | 0B127 | CrumpBit's dly curve |
| `siz` | — | Room size | — | 0B127 | Scales all delay taps |
| `dcy` | — | Decay time | — | 0B127 | Feedback coefficient |
| `fbk` | Feedback | — | Tape feedback | 0B127 | Unity at 100 (not 106) |
| `dmp` | — | Damping | Damping | 0B127 | One-pole LP in feedback |
| `shm` | — | Shimmer | — | 0B127 | Pitch-shifter mix |
| `hed` | Head mode | — | — | STEPPED | 0..6 |
| `frz` | Freeze | Freeze | — | STEPPED | 0..2 |
| `rtg` | — | — | — | STEPPED | 0..2 |
| `ton` | Tone | Tone | Tone | 0B127 | Feedback or pre-filter |
| `dck` | Ducking | — | — | 0B127 | Envelope → wet gain |
| `wid` | Width | — | — | 0B127 | Mid/side output balance |
| `rfl` | — | — | Reflections | 0B127 | Plate reflection density |
| `vol` | Volume | Volume | Volume | 0B127 | Output (stays 127) |

Rows that are not applicable to a type are hidden (cell-inactive predicate
returns true).

### 5.3 P4 changes

- **Mix removed** (T1): parameter enum indices shifted down by one.
  `SpaceRuntime.mix` → `.wet`; dry starts at 0.
- **17 rows plain** (T2): no special formatting needed.
- **fbk unity at 100** (T2): `space_fbkGain()` law changed.
- **Stepped rows** (T2): `space_step()` replaces `space_stop()` zones.
  `hed` 0..6, `frz` 0..2, `rtg` 0..2.
- **CrumpBit rvb damping** (T2): `toneK[ton >> 1]` across the full row
  (applies to CrumpBit, not Space, but changed in the same pass).

---

## 6. Format and Display

### 6.1 Value formatting

`space_formatValue()` handles type-specific display:
- Stepped rows show their mode names (e.g., `frz`: `off`, `hld`, `h+s`)
- `fbk` shows percentage with `100%` at value 100
- Plain rows show 0..127

### 6.2 Labels

`SpaceParameters.c` provides 3-character short labels and up to 16-character
long labels for each row, shared across all three types. The long label
string selection varies by active type.

---

## 7. Build Integration

### 7.1 Makefile

```makefile
DSP_SRCS += Core/DSP/Effects/Space/SpaceKit.c
DSP_SRCS += Core/DSP/Effects/Space/SpaceEffect.c
SRCS     += Core/DSP/Effects/Space/SpaceParameters.c
SRCS     += Core/DSP/Effects/Space/SpaceTables.c
```

The DSP sources get the `-Ofast` compilation rule.

### 7.2 Linker

No new ITCM sections for Space effects (all primitives are inline or
small enough to stay in flash). The arena is in the existing
`.dtcm_fxbuf` NOLOAD section.

### 7.3 StereoFilter removal (P4 T3)

Four files deleted:
- `Core/DSP/Effects/StereoFilter/StereoFilter.c`
- `Core/DSP/Effects/StereoFilter/StereoFilter.h`
- `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c`
- `Core/DSP/Effects/StereoFilter/StereoFilterParameters.h`

Makefile source/object paths updated. Effect type IDs renumbered: the
StereoFilter ID no longer exists. Savings: −3.7 KB flash.

---

## 8. Cost

### 8.1 Budget

The Effect share is **16,634 cycles per block**. All three Space types
must fit within this budget.

### 8.2 Estimates

| Type | Estimated cycles/block | Notes |
|------|----------------------|-------|
| Delay | ~12,000 | Stereo ring, Hermite reads, feedback, ducking |
| Reverb | ~15,000 | Half-rate plate, 4 allpasses, halfband, shimmer |
| Echo | ~13,000 | Mono tape + simplified plate |

The Reverb is the most expensive. The half-rate operation cuts per-frame
cost in half, but the allpass stages, halfband filter, and shimmer
shifters add up. All types are within budget.

### 8.3 Feed-through cost

Feed-through reads add ~12–20 instructions per frame for one or two sides.
This runs only during the feed period (seconds at most) and is charged
to the Effect share.

---

## 9. Extension Guide

### Adding a new Space effect type

1. Add the type enum after `EFFECT_TYPE_ECHO` in the effect type
   enumeration. Update `EFFECT_TYPE_COUNT`.
2. Add init and process functions in `SpaceEffect.c` (or a new file).
3. Register the type in the EffectsManager type table with its init and
   process function pointers.
4. Add or reuse parameter rows in `SpaceParameters.c`. Update the
   cell-active predicate to show/hide rows by type.
5. Update `space_formatValue()` for any new display formatting.
6. Ensure the type's ring requirements fit within the elastic seating
   budget. Declare `seat_min` appropriately.
7. Run `make -C tools/dsp_test all` and device-test within the 16,634
   effect budget.

### Modifying the arena prefix

The prefix size (`SK_PREFIX_BYTES` = 512) must accommodate both the
control block (64 B) and the largest type state header. If a new type
needs more than 448 B of state:
1. Increase `SK_PREFIX_BYTES` (must remain a power of 2 or at least
   aligned to the ring sample size).
2. Update `sk_seat()`, `sk_feedBegin()`, and all init functions.
3. The usable ring shrinks by the same amount.

### Adding a feed-through variant

The four compiled feed-read loops (8/16 bit × 1/2 sides) cover all
current cases. If a new sample format is added (e.g., 24-bit or float):
1. Add the loop variant to `SpaceKit.c`.
2. Update `sk_feedRead()` dispatch.
3. Update the handoff record if new state is needed.
