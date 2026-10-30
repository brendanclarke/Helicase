# S089 Phase 0 — Move SRR Into Voice Processing

**Status:** Blocked — all attempted implementations freeze at boot  
**Depends on:** nothing (prerequisite for S089 P1 voice chain)  
**Goal:** move sample-rate reduction (S&H decimation) from the mixer into each
voice engine's `calcVoiceSyncBlock()`, so it becomes part of the per-voice
processing budget and is available as a voice-chain element.

---

## 1. Current Architecture

### 1.1 Mixer-side state (mixer.c / mixer.h)

| Symbol                        | Type        | Section | Notes                              |
|-------------------------------|-------------|---------|-------------------------------------|
| `mixer_decimation_rate[6]`    | float       | DTCM    | 0..1; 1 = full rate, 0 = hold      |
| `mixer_decimation_cnt[6]`     | float       | DTCM    | S&H accumulator                     |
| `mixer_voice_samples[6]`      | int16_t     | DTCM    | last passed sample                  |

All three are `INCCMZ` (DTCM), guarded by `#if USE_DECIMATOR`.

### 1.2 Mixer render call site (mixer.c:1014–1015)

```c
instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);
mixer_decimateBlock(slot, sampleData);   /* ← to be removed */
```

`mixer_decimateBlock()` (lines 171–186) is a simple S&H loop:
accumulate rate, on overflow latch input sample, output held sample.

### 1.3 Parameter binding (InstrumentManager.c:3906–3911)

```c
case INSTRUMENT_BIND_SLOT_DECIMATION:
    mixer_decimation_rate[slot] = valueShaperI2F(value, -0.7f);
    return 1u;
```

The descriptor row uses `ROW_SLOT_DECIMATION` (identical in all four
engines), which resolves to `ROW_NOBIND_IMAGE` with bind type
`INSTRUMENT_BIND_SLOT_DECIMATION`.

### 1.4 Supplemental modulation

LFO and velocity can target slot decimation through the
`INSTALLED_MOD_TARGET_SLOT_DECIMATION` kind. The machinery is:

- `instrumentManager_applySlotDecimationTarget()` — decode descriptor,
  clamp to 0..127, call `writeRuntime` (which hits the SLOT_DECIMATION
  binding above)
- `instrumentManager_slotDecimationBase()` — reads current Morph
  interpolation image for the descriptor, returns 0..127 base value
- LFO restore (line 3229): calls `applySlotDecimationTarget` with base
- Velocity restore (line 3267): same

These helpers do not read or write mixer arrays directly; they call
`writeRuntime` → `INSTRUMENT_BIND_SLOT_DECIMATION`. Moving the binding
target is therefore sufficient.

---

## 2. Target Architecture

### 2.1 New engine struct members

Add to each engine voice struct (`DrumVoice`, `SnareVoice`,
`CymbalVoice`, `HiHatVoice`):

```c
float       decimation_rate;   /* 4 B — 0..1, 1 = full rate           */
float       decimation_cnt;    /* 4 B — S&H accumulator               */
int16_t     decimation_held;   /* 2 B — last passed sample            */
                               /* + 2 B pad for 4-byte alignment      */
```

Total: **12 B per engine struct**.

The slot reserve is `2 × sizeof(DrumVoice)` = `2 × 588` = `1176 B`.
DrumVoice is currently the largest at 588 B. Adding 12 B → 600 B.
Reserve remains 1200 B (recalculated as `2 × 600`), well within SRAM.

### 2.2 Binding change

`INSTRUMENT_BIND_SLOT_DECIMATION` in `instrumentManager_writeRuntime()`
changes to write the engine struct member instead of the mixer array:

```c
case INSTRUMENT_BIND_SLOT_DECIMATION: {
    /* Resolve the engine struct for this slot and write its rate. */
    InstrumentRuntimeSlot *rs = &runtime_slots[slot];
    float rate = valueShaperI2F((uint8_t)((value > 127u) ? 127u : value), -0.7f);
    /* All four engine types place decimation_rate at the same struct offset
     * — use the slot's active type to pick the member. */
    switch (runtime_slot_type[slot]) {
    case INSTRUMENT_TYPE_DRUM:   rs->drum.decimation_rate   = rate; break;
    case INSTRUMENT_TYPE_SNARE:  rs->snare.decimation_rate  = rate; break;
    case INSTRUMENT_TYPE_CYMBAL: rs->cymbal.decimation_rate = rate; break;
    case INSTRUMENT_TYPE_HIHAT:  rs->hihat.decimation_rate  = rate; break;
    default: return 0u;
    }
    return 1u;
}
```

Alternatively, if all four structs place the three decimation members at
identical offsets (guaranteed by matching field order), a single
pointer-to-member write is possible. This is an implementation detail.

### 2.3 Voice render integration

At the **end** of each engine's `calcVoiceSyncBlock()`, after the
post-chain and amp-ramp-arm clear, insert an inline S&H loop:

```c
/* Sample-rate reduction (S089 P0: moved from mixer). */
{
    float cnt  = voice->decimation_cnt;
    float rate = voice->decimation_rate;
    int16_t held = voice->decimation_held;
    for (uint8_t i = 0; i < size; i++) {
        cnt += rate;
        if (cnt >= 1.f) {
            cnt -= 1.f;
            held = buf[i];
        }
        buf[i] = held;
    }
    voice->decimation_cnt  = cnt;
    voice->decimation_held = held;
}
```

This is the same algorithm as `mixer_decimateBlock()`, just reading/writing
the engine struct instead of mixer arrays.

### 2.4 Initialization

Each engine's async/init function must set:
- `decimation_rate = 1.f` (full rate, no reduction)
- `decimation_cnt = 0.f`
- `decimation_held = 0`

The mixer init (`mixer_init`) still seeds `mixer_decimation_rate[i] = 1`
etc. for backwards compatibility during the transition, but nothing reads
these arrays once the move is complete. They can be removed once this
phase is verified.

### 2.5 Mixer removal

- Remove `mixer_decimateBlock(slot, sampleData)` call at mixer.c:1015.
- Leave the mixer arrays and `mixer_decimateBlock()` definition in place,
  guarded by `USE_DECIMATOR`, until the phase is validated on hardware.
  Then remove in a cleanup commit.

---

## 3. Signal Path Equivalence

The signal processing order does not change:

**Before:** engine renders buf → mixer calls `mixer_decimateBlock` on buf → mixer applies volume/pan  
**After:** engine renders buf → engine S&H loop on buf → return to mixer → mixer applies volume/pan

The decimation happens on the same `sampleData` buffer at the same point
in the pipeline. The only change is *who owns the state*.

Rate = 1 (the parameter default) means the accumulator overflows every
sample, so `held = buf[i]` on every iteration and the output is
bit-identical to the input. No special bypass is needed.

---

## 4. Supplemental Modulation — No Change Needed

The supplemental modulation paths (LFO and velocity) for slot decimation
all funnel through `instrumentManager_writeRuntime()` →
`INSTRUMENT_BIND_SLOT_DECIMATION`. By changing only this binding case
(§2.2), all modulation paths automatically write the engine struct. No
other modulation code needs modification:

- `instrumentManager_applySlotDecimationTarget()` — unchanged (calls writeRuntime)
- `instrumentManager_slotDecimationBase()` — unchanged (reads Morph image)
- LFO restore — unchanged (calls applySlotDecimationTarget)
- Velocity restore — unchanged (calls applySlotDecimationTarget)

---

## 5. Cost Accounting

The S&H loop is ~12–15 instructions per sample (accumulate, compare,
branch, latch, store). This was previously outside the voice budget.
After the move it is **inside** the voice chain budget.

Updated voice chain budget:

| Element        | Cycles/sample | Notes                         |
|----------------|---------------|-------------------------------|
| Transient      | 32            | unchanged                     |
| sft pre-drive  | 13            | unchanged                     |
| D+S post-dist  | 48            | voicePost + distortion curve  |
| **SRR (S&H)**  | **15**        | **newly inside the chain**    |
| **Total**      | **~108**      | was ~93 without SRR           |

The default T+D chain now costs ~108 cycles/sample including SRR. The
voice chain budget for alternative chains is therefore ~108, not ~93.

---

## 6. Files Touched

| File                              | Change                                       |
|-----------------------------------|----------------------------------------------|
| `Core/DSP/Instruments/Drum/DrumVoice.h`       | add 3 struct members (12 B)   |
| `Core/DSP/Instruments/Snare/SnareVoice.h`     | add 3 struct members (12 B)   |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h`   | add 3 struct members (12 B)   |
| `Core/DSP/Instruments/HiHat/HiHatVoice.h`     | add 3 struct members (12 B)   |
| `Core/DSP/Instruments/Drum/DrumVoice.c`        | S&H loop + init              |
| `Core/DSP/Instruments/Snare/Snare.c`           | S&H loop + init              |
| `Core/DSP/Instruments/Cymbal/Cymbal.c`         | S&H loop + init              |
| `Core/DSP/Instruments/HiHat/HiHat.c`          | S&H loop + init              |
| `Core/DSP/Instruments/InstrumentManager.c`     | binding rewrite (§2.2)       |
| `Core/DSPAudio/mixer.c`                        | remove decimateBlock call     |

`mixer.h` extern declarations, `mixer_init()`, and `mixer_decimateBlock()`
left in place initially (dead code) for safe rollback.

---

## 7. Architectural Decisions Needed

1. **Common struct vs. per-engine members.** The three decimation fields
   could be factored into a `VoiceDecimator` struct included in each
   engine, or added as bare members. Bare members are simpler and match
   the current style (no sub-structs for small field groups). The binding
   switch (§2.2) needs a per-type case either way.
   **Recommendation:** bare members, matching `Distortion` pattern
   (which is a sub-struct, but decimation is smaller at 3 fields).

2. **DTCM placement.** The mixer arrays are DTCM (`INCCMZ`). The engine
   structs live in the `runtime_slots` union, which is regular SRAM
   (`.bss`). The S&H loop's three state accesses per sample are now
   regular SRAM instead of DTCM. At 15 cycles/sample this is unlikely
   to matter (the VDIV in distortion dominates), but it is a change.
   **Recommendation:** accept SRAM placement; revisit only if profiling
   shows a regression.

3. **Init site.** Each engine has an async init path (e.g.,
   `Drum_calcVoiceAsync`). Decimation init should go there, alongside
   the existing `distortion` init. The voice chain parameter's default
   will also need to set rate = 1 at load time.
   **Recommendation:** init in async + runtime reset, same as distortion.

---

## 8. Verification

- **Bit-identity test:** render the same pattern with SRR at default
  (rate=1) before and after the move. Output must be bit-identical.
- **SRR audible test:** set SRR to a low value (~20), play a sustained
  sound. The S&H staircase must sound identical before and after.
- **Modulation test:** assign LFO to SRR target, verify sweep is smooth
  and restores correctly when LFO is cleared.
- **Velocity test:** assign velocity to SRR, verify onset modulation
  works and restores on retrigger.

---

## 9. Failed Implementation Log

All attempts to make SRR switchable per voice caused hard-freeze at boot.
The root constraint is `mixer_calcNextSampleBlock()`: 13,508 bytes at
baseline, occupying 82% of the STM32F765's 16 KB I-cache. GCC LTO
inlines all four engine renders into it. Any code growth or cross-TU
call disrupts the inlining heuristics catastrophically.

### 9.1 Attempt A — Inline S&H in engine renders (§2.3 plan)

Copied the S&H loop into each engine's `calcVoiceSyncBlock()` with
per-struct state. Removed the `mixer_decimateBlock()` call.

**Result:** mixer grew from 13,508 → 13,904 bytes (+396). Four inlined
copies of the loop instead of one. Triggered worst-case scene freeze.
The +396 bytes pushed the function past the I-cache thrash threshold.

### 9.2 Attempt B — Noinline S&H extract in mixer.c

Extracted the S&H loop into a `__attribute__((noinline))` function
within mixer.c (same TU). Called from the mixer render loop.

**Result:** Complete boot freeze. Cause unknown but suspected LTO
constprop interaction — the noinline attribute may have broken LTO's
ability to propagate constants through the render loop, bloating the
surrounding code even though the function itself was small.

### 9.3 Attempt C — Cross-TU function in InstrumentManager.c

Moved the decimation function into InstrumentManager.c with a type
switch to resolve per-engine struct members. Called from mixer render.

**Result:** mixer grew from 13,508 → 14,860 bytes (+1,352). The
cross-TU `bl` instruction acts as an LTO optimization barrier, forcing
GCC to spill/reload state around the call and inhibiting other
optimizations across the entire inlined function.

### 9.4 Attempt D — Cross-TU function without type switch

Same as C but using the existing mixer arrays (no type switch, no
per-engine struct access). Simplest possible cross-TU function.

**Result:** Still 14,860 bytes. Confirmed the bloat is caused by the
cross-TU call itself, not the called function's complexity. Any `bl`
to a different TU triggers the same LTO barrier.

### 9.5 Attempt E — Same-TU gate array (minimal change)

Added `static uint8_t mixer_decimation_enabled[6]` in mixer.c.
Gate: `if (mixer_decimation_enabled[slot]) mixer_decimateBlock(...)`.
Setter `mixer_setDecimationEnabled()` called from InstrumentManager.c
during `runtimeInit` and `resetRuntimeSlotFromScene` (both init paths
set enabled=1 for all four engine types). Default off in `mixer_init`.

```diff
 // mixer.c — new array alongside existing DTCM arrays
+static uint8_t mixer_decimation_enabled[6];

 // mixer_init() — default off
+mixer_decimation_enabled[i] = 0u;

 // render loop — gate replaces unconditional call
-mixer_decimateBlock(slot,sampleData);
+if (mixer_decimation_enabled[slot])
+    mixer_decimateBlock(slot,sampleData);

 // new setter
+void mixer_setDecimationEnabled(uint8_t slot, uint8_t enabled)

 // mixer.h — declaration
+void mixer_setDecimationEnabled(uint8_t slot, uint8_t enabled);

 // InstrumentManager.c — both init paths, all four engine cases
+mixer_setDecimationEnabled(slot, 1u);
```

**Build result:** mixer 13,736 bytes (+228 over baseline). Binary
677,012 bytes.

**Hardware result:** Hard-freeze at boot. +228 bytes was expected to be
safe (well under the ~400 byte danger zone from Attempt A), but the
freeze indicates the I-cache sensitivity is more severe than modeled,
or the branch itself disrupts the pipeline in a way the size delta
doesn't capture.

### 9.6 Conclusions

1. `mixer_calcNextSampleBlock()` is untouchable. Even a +228 byte
   change with a single branch addition causes a freeze. The function
   sits at a cliff edge in the I-cache.

2. All paths that modify the mixer render loop — whether adding code,
   adding branches, or adding calls — risk crossing this threshold.

3. **Next approach to try:** make the gate zero-cost at the mixer level.
   Options:
   - Move the `mixer_decimateBlock` call into the engine render
     functions but as a **tail call** that returns to the mixer without
     growing the inlined body. Requires the engines to call a mixer-TU
     function, which is the cross-TU barrier problem (§9.4).
   - Use a function pointer array in DTCM: `mixer_postProcess[slot]`
     points to either `mixer_decimateBlock` or a no-op. The indirect
     call is the same size as the current direct call — no branch added.
     Risk: indirect calls may also act as LTO barriers.
   - Accept SRR as always-on (baseline behavior) and implement the
     voice chain switchability at a higher level (scene parameter that
     sets rate=1 to effectively disable).
