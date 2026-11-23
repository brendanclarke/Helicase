# Tetrakt — Firmware Module Reference

> Four-layer sample player. 7th instrument type, Basic class.
> Token `tkt`, extension `.tkt`, folder `/Instrument/Tetrakt/`.
> Introduced in Session 096. Build: `-Ofast -fno-lto` (outside LTO unit).

---

## 1. Files

| File | Lines | Purpose |
|------|-------|---------|
| `Core/DSP/Instruments/Tetrakt/TetraktVoice.h` | — | Voice struct (`TetraktVoice`, 1,064 B), layer struct (`TktLayer`), constants, entry-point declarations |
| `Core/DSP/Instruments/Tetrakt/TetraktVoice.c` | — | Async (control-rate) and sync (audio-rate, fused sample read) processing |
| `Core/DSP/Instruments/Tetrakt/TetraktParameters.h` | — | Registry exports: 64 descriptors, 8 pages, velocity-group helpers |
| `Core/DSP/Instruments/Tetrakt/TetraktParameters.c` | — | Parameter table (64 rows in page order), menu layout, velocity lookup |
| `Core/DSP/Instruments/Tetrakt/TetraktTables.h` | — | Table declarations |
| `Core/DSP/Instruments/Tetrakt/TetraktTables.c` | — | Generated tables (decay, drive, SRR rate, tone SVF coefficients) |
| `tools/tetrakt/gen_tkt_tables.py` | — | Table generator |
| `tools/tetrakt/make_init_tkt.py` | — | Init-file generator (matches `Tetrakt_initVoice()`) |
| `SD_CARD/Instrument/Tetrakt/init.tkt` | — | Default instrument preset |
| `tools/dsp_test/modules/tetrakt.c` | — | Host test: 9 law groups, 6 break builds |
| `tools/dsp_test/tetrakt_cpu.py` | — | CPU instruction counter |

---

## 2. Registry

```c
[INSTRUMENT_TYPE_TKT] =
{ INSTRUMENT_TYPE_TKT, "tkt", "Tetrakt", ".tkt", "Tetrakt",
  INSTRUMENT_FLAG_BASIC,
  tetrakt_param_descriptors, 64,
  tetrakt_menu_pages, 8,
  21 /* switch_row_plus1: lon at index 20 */ }
```

- **Type value:** 6 (appended before UNKNOWN, which moves to 7)
- **Class:** Basic — any slot, any number per Scene, held to Basic share (19,620 cycles)
- **Switch row:** `lon` at descriptor index 20, referenced by bit-cell codes 0xF0..0xF3
- **Storage:** `STORAGE_INSTRUMENT_TKT`, token `"tkt"`, extension `".tkt"`, directory `/Instrument/Tetrakt/`
- **`.hcnames` acceptance:** a header written before the Tetrakt was added is a leading part of the new list

---

## 3. Voice Struct

```c
typedef struct {
    TktLayer layer[4];           // four independent processing layers
    uint8_t  layerOn;            // 4-bit mask: which layers trigger (lon row)
    uint8_t  veloKind;           // TKT_KIND_* velocity group, or TKT_KIND_NONE
    uint8_t  veloVol;            // velocity-to-volume enable
    float    vol;                // voice volume (TYPE_FLT, read by mixer)
    uint8_t  pan;                // L-R balance (TYPE_UINT8)
    uint8_t  width;              // W2 allpass width
    // space stage parameters
    uint8_t  spaceTime, spaceFeedback, spaceDetune, spaceTone, spaceLevel;
    // space stage state
    int16_t *spaceLine;          // FxBuffer voice unit (2,208 × int16)
    uint32_t spaceWritePos;
    float    spaceApState[2];    // allpass states
    float    spaceToneState;     // one-pole tone state
    float    spaceLpState;       // feedback LP state
    float    spaceWobPhase;      // wobble phase accumulator
    // platform LFO
    Lfo      lfo;
    // right channel output (32 int16, read by mixer via instrumentManager_slotRight)
    int16_t  right[32];
} TetraktVoice;
```

Each `TktLayer`:
```c
typedef struct {
    // parameters (TYPE_UINT8 rows)
    uint8_t sample, tune, level, decay, pan;
    uint8_t drive, srr, space, tone, toneEnv;
    // read state
    OscState osc;                // platform oscillator state (bind, phase, etc.)
    float    bindGain;           // catalogue gain, set once per bind
    // envelope state
    float    env;                // decay envelope level
    uint8_t  stage;              // IDLE/PLAY/FADE/START
    float    fadeGain;           // fade-to-zero ramp during FADE
    // processing state
    float    dcState;            // drive DC blocker
    float    srrCnt;             // SRR hold counter
    float    srrHeld;            // SRR last held value
    float    toneState[2];       // SVF integrator states
    // velocity
    uint8_t  velocity;           // latched trigger velocity
} TktLayer;
```

**Size:** `sizeof(TetraktVoice)` = 1,064 B, within the 1,264 B slot reserve (200 B spare). Asserted at compile time.

---

## 4. Signal Path (Sync Block)

`Tetrakt_calcVoiceSyncBlock(voice, buf, size)` — `noinline`, called from `instrumentManager_calcSlotSyncBlock()`.

For each of the four layers, in the layer's post loop (the fused read):

1. **Read:** `s = (a << 12) + (ph & 4095)(b − a)`, exact integers. Phase clamped to end.
2. **Gain:** `x = s × g` where `g` = `envelope × bindGain / 4096`
3. **Drive:** `y = tanh(x × driveK)`, DC blocker
4. **SRR:** hold-and-step (runs inside the post loop)
5. **Tone:** 2-pole bipolar SVF with envelope modulation
6. **Level × pan:** into L summing bus and space feed bus

The first layer uses a `store` loop (clears the bus); layers 1–3 use `add` loops.

After the four layers:
- **W2 allpass:** widens the mono sum into L/R using an envelope-derived coefficient
- **S1 space:** reads two taps from the delay line, writes the space feed, applies feedback
- **Pack:** float → int16 into `buf` (left) and `voice->right` (right)

---

## 5. Control Path (Async)

`Tetrakt_calcVoiceAsync(voice)` — `noinline`, called from `instrumentManager_calcSlotAsync()`.

Per layer:
1. **Stage management:** IDLE → skip; PLAY → decay step; FADE → fade step (if done → START); START → re-arm → PLAY
2. **Decay step:** `env *= colEgK[decay]`; if below threshold → IDLE
3. **Pitch:** `phaseInc` from `tune` ±63 semitones offset by trigger note, via `osc_noteToPhaseInc()`
4. **Bind:** `osc_userBind()` for the sample; `bindGain` from catalogue entry. Only at START or when sample row changes.
5. **Tone coefficients:** lookup `tktToneB1[t]`/`tktToneB2[t]` where `t = tone + toneEnv × env`
6. **Drive gain:** `tktDriveK[drive]`
7. **SRR rate:** `tktSrrRate[srr]`
8. **Level/pan/send gains:** from row values

Voice-level:
- **Space stage coefficients:** tap positions, feedback, wobble phase advance, tone filter
- **W2 coefficient:** from width and master envelope

---

## 6. Trigger

`Tetrakt_triggerVoice(voice, slot, velocity, note, layers)`:

1. Read `layers` mask (from MIDI layer note or overlay repeat); if 0, use `voice->layerOn`
2. For each set bit in the mask:
   - If the layer is in PLAY → enter FADE (one-block linear ramp to 0.0); then START next block
   - If the layer is IDLE → enter PLAY directly
   - Latch velocity, set trigger note
3. Platform: `lfo_retrigger()`, velocity modulator update
4. No step length (Tetrakt is one-shot; layers decay naturally)

---

## 7. Sequencing and Menu

### 7.1 Parameter Table (64 rows)

| Index | Page | Rows | Notes |
|-------|------|------|-------|
| 0–3 | OSC | smA..smD | Sample selectors (MENU_WAVEFORM, catalogue sNN codes) |
| 4–7 | OSC | tuA..tuD | Tune ±63 semitones |
| 8–11 | AEG | lvA..lvD | Level 0..127 |
| 12–15 | AEG | dyA..dyD | Decay 0..127 |
| 16–19 | AEG | pnA..pnD | Pan ±63 |
| 20 | MOD | lon | Layer switch (4 bit cells: onA..onD) |
| 21–23 | MOD | dst, amt, vel | Velocity destination, amount, volume enable |
| 24–27 | FM | drA..drD | Drive 0..127 |
| 28–31 | FM | srA..srD | SRR 0..127 |
| 32–35 | Click | spA..spD | Space send 0..127 |
| 36–39 | Click | stm, sfb, sdt, sto | Space time, feedback, detune, tone |
| 40–43 | FILTER | tnA..tnD | Tone cutoff ±63 |
| 44–47 | FILTER | teA..teD | Tone envelope depth ±63 |
| 48–59 | LFO | (platform pair) | The Drum/Colloid layout |
| 60 | MIX | vol | Voice volume (TYPE_FLT) |
| 61 | MIX | pan | L-R balance (TYPE_UINT8) |
| 62 | MIX | wid | W2 width 0..127 |
| 63 | MIX | slv | Space level 0..127 |

All rows are `TYPE_UINT8` except `vol` (TYPE_FLT) and the platform LFO rows. All are morphable, modulatable, and automatable except the sample selectors (automatable + selector only) and `lon`/`vel` (automatable + selector).

### 7.2 Bit Cells (SD1–SD2)

Page cell code `0xF0 + b` (b 0..3) = "bit b of the type's switch row". Resolved by `instrumentManager_menuDescriptorIndex()` and `instrumentManager_menuCellBitPlus1()`. Display: dtype is plain on/off (tag 0); value = one bit of the row's Normal value. Commit: the whole mask is committed (R5 rule). Page name: `onA`..`onD`. Automation, search, MIDI Learn, pot clear and live record all see one `lon` row.

### 7.3 Velocity Groups (SD3)

Nine groups of four layer rows: tune, level, decay, pan, drive, SRR, space, tone, toneEnv. The velocity destination list names each group once by its layer-A row (`tn*` for tnA). `tetraktParameters_velocityKind()` returns the group; `velocityTokenHidden()` returns 1 for B/C/D rows. The LFO target lists keep every row.

### 7.4 Sample Rows

MENU_WAVEFORM selectors: `instrumentManager_waveDomainMax()` clamps to `sampleMemory_getNumSamples() − 1`. Display: the catalogue's 8-character name and sNN code. Never morphed (selector).

---

## 8. FxBuffer Voice Unit

The Tetrakt claims one FxBuffer voice unit (2,208 × int16 = 4,416 B) for its S1 delay line:
- Claimed at type assignment (`instrumentManager_slotVoiceUnits()`, SD11)
- Released at type change (a Tetrakt → Tetrakt reset keeps the unit)
- Six Tetrakts own six of twelve units
- The Effect is notified through the share callback (foreground only)
- `Tetrakt_initVoice()` receives the line pointer; NULL = S1 muted

---

## 9. Build

**Makefile:**
- `Core/DSP/Instruments/Tetrakt/TetraktVoice.c` and `TetraktTables.c`: `-Ofast -fno-lto` (DSP recipes)
- `TetraktParameters.c`: standard `-O2` (in SRCS, like other parameter tables)
- Reason for `-fno-lto`: outside the LTO unit, the Tetrakt cannot shift LTO's partitioning of the other engines' loops (gated by `elf_p4_check`). Its entry points are `noinline` calls from InstrumentManager.

**ITCM:** 0 B after §19 (the fused read eliminated the `osc_renderTyped()` ITCM section that LTO had pulled in). The space function is not in ITCM.

---

## 10. Cost

| Resource | Value | Limit |
|----------|-------|-------|
| CPU worst block | 382.7 instructions | 390 (gate) / 19,620 cycles (Basic share) |
| CPU device median | 19,995 cycles (final settings) | 19,620 |
| CPU device highest window | 21,893 cycles | — (over; flash read variance) |
| Voice struct | 1,064 B | 1,264 B slot reserve |
| Shared work area | 384 B (.bss) | — |
| ITCM | 0 B (after §19) | — |
| FxBuffer units | 1 per voice | 12 total |

Cycles per instruction: 1.52 (1.41..1.63). Higher than Colloid (1.37..1.41) due to four simultaneous flash reads.

---

## 11. Host Tests

| Law | What it checks |
|-----|---------------|
| TK-TRIG | Layer mask, FADE/START stages, fresh-voice start, retrigger isolation |
| TK-DEC | Monotone decay, IDLE threshold, continuous across blocks |
| TK-TUNE | Phase increment law, semitone accuracy, clamp at extremes |
| TK-SRR | Exact bypass at 127, hold-count law at 64 |
| TK-TONE | Exact bypass at 63/63, no step discontinuity, LP/HP −3 dB corners |
| TK-DRV | Exact dry at 0, zero-in/zero-out, DC blocker mean |
| TK-W2 | Exact dry at wid 0, identity at wid 127 / env 1.0 |
| TK-S1 | Tap delay accuracy, detune offset, feedback bounded decay |
| TK-VEL | Per-layer latch, group application, retrigger isolation |
| TK-READ | Fused read vs platform read (§19), bindGain, phase, unbound silence |

Break builds: `tetrakt_break_fade` (no fade), `tetrakt_break_bias` (no DC blocker), `tetrakt_break_srr` (no hold), `tetrakt_break_guard` (no read guard), `tetrakt_break_gain` (no bindGain), `tetrakt_break_frac` (no interpolation).

---

## 12. Extending

**To add a new per-layer row:**
1. Add the `uint8_t` member to `TktLayer` in `TetraktVoice.h` (check slot reserve)
2. Add four descriptor rows to `TetraktParameters.c` (groups of four: A, B, C, D)
3. Update `tetrakt_param_index_t` and the `_Static_assert` count
4. If it's a velocity group, add the layer-A row to `tkt_groupA[]` and increment `TKT_KIND_COUNT`
5. Place it on the appropriate menu sub-page in `tetrakt_menu_pages[]`
6. Set the init value in `Tetrakt_initVoice()` and `make_init_tkt.py`
7. Use it in the async (coefficients) or sync (processing) path
8. Add a host test law in `modules/tetrakt.c`

**Constraints:**
- The table is 64 of 64 rows — adding a row requires removing one or expanding `INSTRUMENT_PARAM_COUNT`
- The append-only storage rule: a later row can only be appended before `TKT_PARAM_COUNT`
- All rows must be `TYPE_UINT8` (except vol, which is TYPE_FLT)
- The constant-CPU rule: every new stage must run every block

**To change the layer count:** `TKT_LAYERS` (currently 4) is a compile-time constant. Changing it requires updating every loop, the bit-cell range, the layer names, the velocity groups, and the trigger mask.
