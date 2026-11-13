# Effects Modules DSP Reference

Each Effect module's DSP, binding rules, arena use and cost. Part I is the
system framework every module shares; Part II has one section per Effect type.

- **Current as of:** Session 094 P2 (2026-11-13). S094 P2 restored the
  TPT SVF on `grn`/`rvb` (removed S091 P5 F1) and raised the CrumpBit
  cap to 181 instructions/frame. S091 expanded CrumpBit from one engine
  to five sub-types (dly/cmb/pit/grn/rvb) across six phases (P1–P6).
  S074 added CrumpBit (the first type using the arena) and the
  StereoFilter reference.
- **Scope split with `MIXER_SYSTEM_DSP_REFERENCE.md`:** that document covers
  the mixer signal path (decimation, fader gains, dry/send, pan/route), the
  FX bus handoff and return, the master bus compressor, the output pipeline,
  mixer costs, and how to modify the mixer.
- **Scope split with `EFFECTS_BUS_REFERENCE.md`:** that document is the
  authority for the Effect system's control side: the type registry,
  parameters and resolution order, the Effect page, the FX sequencer,
  automation and LFO targets, storage and AutoSave, and the add-a-type
  tutorial.
- **Related:** `INSTRUMENTS_DSP_REFERENCE.md` (the voices and modulation),
  `STORAGE_SRAM_MANIFEST.md` (memory), `CPU_USE_DSP_AUDIT.md` (cost audit),
  `tools/dsp_test/DSP_TEST.md` (host test bench).

---

## Part I — Effect System Framework

## 1. Resolution before processing

`effects_service()` (every block, foreground) rescans every descriptor of
the active type: Morph base → interpolation → FX-sequencer lock → Pattern
overlay → LFO → clamps (`EFFECTS_BUS_REFERENCE.md` §6). It calls the type's
`write_param(rt, index, value)` only when a row's effective value changed,
and applies the common rows (route, level, pan) itself.

- **Cost:** about 10–15 cycles per row per block (about 100 for the 7-row
  `flt`, up to about 1,000 for a 64-row type), plus whatever `write_param`
  does on a change.
- **An LFO or a moving lock on a parameter makes `write_param` run every
  block.** Budget coefficient recomputation as if it always runs.

## 2. Runtime state

- `effects_runtime` is a union of every type's runtime struct in DTCM
  (`.dtcmz`), 100 B since S091 P4 (CrumpBit: 56 B shared + a 44 B sub-engine
  union; StereoFilter 76 B). The
  manager zeroes it and calls the type's `init()` when the type becomes
  active. It does not zero or re-`init` on a same-type Scene switch: the
  runtime, and with it any tail, keeps running.
- **A larger runtime struct grows the union and shrinks the arena** (§3) by
  the same amount (the linker ASSERT keeps the arena ≥ 120 KiB). It needs
  RAM approval (`STORAGE_SRAM_MANIFEST.md` §10). Large audio state belongs
  in the arena share, not in the union.

## 3. The shared DTCM arena for buffer-using Effects

CrumpBit (§6) is the first type that uses the arena; StereoFilter uses
none.

### 3.1 Geometry

- `.dtcm_fxbuf`: **126,656 B** to the end of DTCM, 32-byte aligned, NOLOAD
  (never copied, never zeroed). DTCM is single-cycle, uncached and not
  reachable by DMA, which suits delay lines. History: 126,624 B before the
  bus compressor's 32 B DTCM state (S074); 126,592 B after it; 126,656 B
  since S091 P3/P4 (the runtime union crossed the 88 B boundary to 100 B,
  losing one 32 B granule, but other DTCM layout changes netted a gain).
- **Effect share:** one contiguous region from the arena bottom up to the
  lowest claimed voice unit, `fxbuf_effectShare(&share)` → `{base, offset,
  bytes}`. Base is 32-byte aligned; `bytes` is a multiple of 32.
- **Voice units** (for future buffer-using instruments): 4,416 B each
  (2,208 16-bit samples, 50.06 ms), at most two per slot, twelve in total,
  allocated from the top. With all twelve claimed the Effect keeps 73,600 B.
- **What fits:**

  | Share | 16-bit mono | 16-bit stereo | 8-bit mono | 8-bit stereo |
  |---|---:|---:|---:|---:|
  | Whole arena, 126,656 B | 1.44 s | 0.72 s | 2.87 s | 1.44 s |
  | Minimum, 73,600 B | 0.83 s | 0.42 s | 1.67 s | 0.83 s |
  | CrumpBit's loop, the share up to 126,656 B (N1); `grn` two half lines (≤ 1.436 s each); `rvb` words (31,664 frames of four 8-bit lines) | — | — | 2.871 s (whole arena) … 1.671 s (12 units) | — |

  (Seconds at 44,108 Hz, before any other use of the share.)

### 3.2 The contract a buffer-using type must meet

| Registry / ops field | Duty |
|---|---|
| `buffer_min_bytes`, `buffer_pref_bytes` | Declare the need (0 = no arena use). The type must still work at the minimum share. |
| `init(rt, handoff)` | Clear every region it will read, **unless** it deliberately adopts content that `handoff->state_flags` marks valid for it (`FXBUF_STATE_EFFECT_WRITTEN`) and whose type, channels, bits and rate match. Boot content is undefined. |
| `export_handoff(rt, out)` | On exit, describe what it leaves: type, channels, bits, rate, share bounds, up to four read/write offsets (entries 12..15, arena-relative), and set `FXBUF_STATE_EFFECT_WRITTEN` if the region holds real audio. |
| `buffer_changed(rt, share)` | Called synchronously when voice units are claimed or released and the share moves or shrinks. Re-seat positions and lengths; a forced re-resolution follows. |
| `effective_max(index, share)` | For `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT` rows (for example a delay time): the largest value the current share allows. It clamps at runtime only; stored values are never rewritten. |

- **"Clear unless you adopt":** the system never clears the arena, even on a
  type change. A delay can keep its tail across a same-type Scene switch,
  and the next owner decides what is valid.
- **Everything is foreground.** `FxBuffer` and `EffectsManager` must never be
  called from an ISR. Share changes happen between render blocks.

### 3.3 Gaps closed with the first buffer type (Session 074)

1. **Same-type Scene switch:** `effects_activateScene()` now refreshes the
   handoff (`effects_exportHandoff()`) without `init` when the type stays the
   same.
2. **FX return ramp while `off`:** the mixer's `off` branch zeroes
   `mixer_fx_return_last_gain[]` every block
   (`MIXER_SYSTEM_DSP_REFERENCE.md` §3.3 item 6).
3. **Diagnostic `fxbuf_init()` order:** `fxbuf_handoffResetAll()` runs
   directly after `fxbuf_clearOwners()`, so diagnostic forced voice units
   carry valid rates and the minimum-share test
   (`DEV_MODE_DIAGNOSTIC 1`, `DEV_FXBUF_FORCE_VOICE_UNITS 12`) is valid.

### 3.4 DSP guidance for delay-style Effects

- **Constant cost:** the per-sample work must not depend on parameter values.
  Delay time changes the read position, not the amount of work; feedback,
  filtering and interpolation run every sample whatever their settings.
- **Wrap without division:** the share is a multiple of 32 bytes, not a power
  of two. Keep read and write indices in range with a compare-and-subtract,
  which costs the same every sample.
- **Parameter changes arrive once per block.** Ramp delay time (or crossfade
  two taps) across the block to avoid clicks and pitch jumps; ramp gains as
  the mixer does.
- **Bounded feedback:** clamp or soft-clip inside the loop so the float state
  cannot run away; the bus float is unbounded until the return's ±255 clamp.
- **Mono input:** with `EFFECT_IO_MONO_IN`, `io->r` is NULL (or the zeroed
  output channel for mono-in/stereo-out).
- **8-bit storage** halves memory but needs explicit quantisation and
  scaling; decide deliberately whether it should sound raw. CrumpBit
  re-quantises its loop to 8 bits on every pass on purpose: that is its
  character, and it bounds the feedback.

## 4. How to add an Effect type (DSP side)

Follow the tutorial in `EFFECTS_BUS_REFERENCE.md` §14 for the registry,
descriptors, lanes, page layout and storage. For the DSP:

Rules for every DSP change: constant CPU; RAM approval for any new or larger
allocation (including the runtime union, which shrinks the arena);
foreground only; no VLAs. `-Ofast` applies to each Effect DSP file only
through an explicit Makefile rule. Verify with `make clean && make all`,
`arm-none-eabi-size`,
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`, the
`tools/dsp_test` targets that cover the code, and on hardware the
worst-case Scene and a listening check.

1. `Core/DSP/Effects/<Type>/<Type>Effect.c/.h`: the runtime struct and the
   `effect_type_ops_t`. Keep the struct small; add it to the
   `effects_runtime_t` union with a `_Static_assert`.
2. `process(rt, io)`: float, in place, `io->frames` samples, respect
   `io->channels` and a NULL `io->r`. Never truncate to int16.
3. `write_param(rt, index, value)`: rows 3..63 only; convert here; expect it
   to run every block under an LFO.
4. Buffer use: §3.2. `CrumpBitEffect.c` is the worked example: it seats its
   loop from `io->share`, masks unwritten reads with a fill counter, wraps
   by compare-and-subtract, ramps every gain across the block, and keeps
   feedback bounded by re-quantising.
5. Makefile: add `<Type>Effect.c` to `DSP_SRCS` **and** an explicit `-Ofast`
   rule like StereoFilter's (the pattern rule only covers `Core/DSPAudio/`);
   `<Type>Parameters.c` goes into the normal sources; add the include path.
6. Test on the host first (`DSP_TEST.md` §6.3: stability, bounded output,
   mono/stereo I/O, parameter jumps, cost), then on hardware with the
   diagnostic registry self-check (`FxBf` screen, `DEV_MODES.md`).

---

## Part II — Effect Modules

## 5. StereoFilter (`flt`)

- Stereo in, stereo out. Two `ResonantFilter` states (left, right) share
  linked coefficients: `stereoFilter_linkCoefficients()` copies `f`, `g`, `q`
  and `drive` from left to right after every coefficient write; the state
  variables (`s1`, `s2`, `a`, `b`, `zi`) stay per channel.
- `write_param` uses the same shaping as a voice filter:
  `filter_freq` → `SVF_directSetFilterValue(valueShaperF2F(v/127, −0.9))`,
  `filter_reso` → `SVF_setReso(v/127)`, `filter_drive` → `SVF_setDrive(v)`,
  `filter_type` → `v + 1`.
- `process` runs `SVF_calcBlockZDFFloat()` on `io->l`, and on `io->r` when
  there are two channels.
- **`SVF_calcBlockZDFFloat()`** is the float twin of the voice filter: the
  same batched two-division solver (S073), normalised float in and out, no
  `__SSAT`, float literals. Types 1..7 filter; any other value passes the
  block through. Its arithmetic must stay identical to `SVF_calcBlockZDF()`;
  both are described in `INSTRUMENTS_DSP_REFERENCE.md` §6.2. An `#error`
  guards the shaper configuration it does not mirror.
- **Cost (S073):** 80–103 instructions per sample per channel, 2 divisions
  (3 for LP); about 4–5 % of the CPU with both channels.

---

## 6. CrumpBit (`cbt`, Session 074; expanded S091)

Files: `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c/.h` (compiled `-Ofast`
through an explicit Makefile rule) and the generated `CrumpBitTables.c/.h`.
Page, rows and lanes are in `EFFECTS_BUS_REFERENCE.md` §4.2a and §8.6.
Design record: `074_SESSION_HANDOFF_LOG.md` §7; expansion:
`S091_P2_CrumpBit_Expanded_Plan.md`.

**What it is.** The stereo send is converted as a bipolar 8-bit audio ADC
would. The eight data lines can each be forced off or inverted, and an 8-bit
DAC decodes the result. AC coupling removes the DC the masks create. The two
channels then feed a mono 8-bit tape-style delay kept in the arena; its
feedback return is DC-blocked, tilted and soft-kneed, and the Mix control
crossfades the bit-crushed signal with the raw delay read. Finally a stereo
output filter shapes the Effect output: a TPT state-variable filter (eight
voice types) in all five sub-types (restored to `grn`/`rvb` in S094 P2
§23; was one-pole S091 P5 F1–S094 P1). Three stages, one row owner each
(S091 P2 §4).

```
per sample, both channels:
  code  = adc(x)                          x·128 + 128.5 truncated, then USAT to 0..255 (S091 R2; = the S074 clamp and 256 → 255 fold)
  code' = lines[code]                     (code & keep) ^ invert for all 256 codes, rebuilt only on a bof/biv change (S091 P5 X2)
  h     = (code' − code'[n−1])/128 + 0.9985755·h[n−1]   DAC + AC coupling, ~10 Hz one-pole high-pass (S091 R4: the code difference is exactly y − y[n−1])
per sample, the delay sub-type (the tape loop, mono):
  delay += (target − delay)·1.5113e-4     tape glide, τ 150 ms; target ≤ length − 2
  rp     = wp − delay (wrap by compare)   i0 = ⌊rp⌋, i1 = i0+1 (wrap), frac = rp − i0
  wet    = (loop[i0] + frac·(loop[i1] − loop[i0]))/128,   muted unless delay + 2 ≤ valid
  dc   = wet − wet[n−1] + 0.99928775·dc[n−1]        DC blocker, ~5 Hz (in the loop)
  s   += k(ton)·(dc − s);  t = a·s + b·dc            tilt: LP (1,0), flat (0,1), HP (−1,1)
  v    = clamp(fb·t, ±1.5);  ret = v − (4/27)·v³     soft knee, ±1 at ±1.5
  loop[wp] = adc(½(hL + hR) + ret) − 128   (int8; re-quantised every pass, masks not re-applied)
  wp++ (wrap), valid++ (saturating at the loop length)
  out  = h + mix·(g·wet − h)                         first echo unshaped
per sample, the shared output filter (second pass, both channels; S091 P5 F1):
  all sub-types: TPT SVF: v1 = a1·ic1 + a2·v3, v2 = ic2 + a2·ic1 + a3·v3, ic1 = 2·v1 − ic1, ic2 = 2·v2 − ic2
               out = cx·x + cb·bp + cl·lp            one 3-coefficient mix, ramped per block
               (grn/rvb restored to SVF in S094 P2 §23; was one-pole S091 P5 F1–S094 P1)
```

### 6.1 CrumpBit sub-types (S091 P3; efficiencies S091 P5; parameter cleanup S091 P6)

Four stages per block. Stage 1 is the shared dry path (ADC, the data-line
table, DAC, AC coupling) and stage 3 the shared MIX stage; stage 2 is exactly
one sub-engine, chosen once per block by `sub`; stage 4 is the output filter
of that sub-type (TPT SVF in all five sub-types since S094 P2; was
one-pole in `grn`/`rvb` from S091 P5 F1 to S094 P1). Within a sub-type
every statement runs every frame (constant CPU). The whole Effect is
capped at 181 instructions per frame in every sub-type, worst case,
gated by `crumpbit_cpu.py` (raised from 170 in S094 P2).

**S091 P6 changes across sub-types:**
- `dpn` renamed to `spn` ("SubPan") in every sub-type. The row's per-sub-type
  role is unchanged (wet balance in `pit`, Position in `grn`, etc.).
- A labels hook replaces `name3` for per-sub-type row names: the hook returns
  the row's display label dynamically instead of the old static third-name
  field.
- `snc` removed from `dly` lanes (Rate/Sync below).

- **Switching.** A change of the resolved `sub` (menu, MIDI, Scene or
  Pattern automation) is detected per block, so it lands on a block boundary.
  `crumpBit_enter()` reinterprets the arena in the new format: nothing is
  cleared or gated; every row and FX-lane value carries over, rows are
  aligned by role, and only the new engine's DSP state is re-initialised and
  its prefix clamped to the new geometry. Cross-format glitches are wanted.
  All sub-types now share the SVF output filter (S094 P2), so a switch
  hands the integrator states over directly. (Before S094 P2, a switch
  between an SVF sub-type and `grn`/`rvb` mapped SVF low integrator ↔
  one-pole state; S091 P5 F1.)
- **MIX stage (binding, user).** `mix` is not a sub-type control: for every
  sub-type, `out = h + mix·(wet − h)`, where `h` is stage 1 and `wet` is the
  selected engine's output. The row keeps its name and lane 3 in every lane
  map, and is never freeable. `mix` keeps its per-frame ramp in every
  sub-type (user, S091 P5 Q9).
- **`cmb` comb / resonator / flanger (engine A, loop 109).** The tape loop
  retuned to the semitone comb table (8.4 .. 2,141 samples) with a fast
  glide `4.5245e-3`; the same return chain and Q14 integer tap as `dly`;
  `snc` folds a negative feedback sign into the per-block gain. Per-block
  work is lower than `dly` (one table read instead of the Sync search).
  Whole-Effect worst case 153.8 per frame (S091 P5).
- **`pit` two-head pitch shifter (engine B, loop 106).** `φ += (1 − r)/W`
  per sample in a wrapping 0.32 word; head A trails by `2 + W·φ`, head B by
  `2 + W·frac(φ + ½)`; both reads are drop-sample (S091 P5 P1: no
  interpolation), and the triangular windows `wA = 1 − |2φ − 1|`,
  `wB = 1 − wA` sum to 1. `aux` is the interval (labelled "pit" /
  "Shift" / "SemiPtch"), pitch rows ±36 st; `rte` the window size with a
  20 ms one-pole glide on changes; `snc` semitone snap, `spn` the wet
  balance; `fin` (row 23) is fine pitch ±52 cents. The return chain is the
  tape's. Whole-Effect worst case 154.1 per frame (S091 P6; was 153.6).
- **`grn` granular plus delay (engine B', loop 114; S091 P4 model;
  efficiencies S091 P5; P6 tap fade).** The
  share is split into a record line and a delay line of equal length N.
  Two voices read the record line at 16.16 positions with Q3.12 increments
  (`pos += inc x 16`), drop-sample with no interpolation (user G6). Density
  (`den`, bipolar) triggers each voice independently of its Length:
  63 off, 64 (+1) one trigger per N, rhythmic above, random below (at least
  one block). Length (`lng`, row 20, bipolar) is the grain size up to N,
  reversed left of centre, at least ~5 ms. Position (`spn`, bipolar) is 0
  follow (two frames behind the head), a rhythmic lag to the right, and a
  random scatter to the whole recorded line to the left. Each voice uses a
  flat-top window with 128-frame complementary fades, so back-to-back
  grains sum to exactly 1 (the copy setting). A trigger with both voices
  busy steals the older one with a 128-frame fade and a deferred trigger;
  the per-grain overwrite clamp keeps every read inside recorded, not
  overwritten audio. `snc` freezes the record line and anchors Position at
  the freeze head; rows 15..21 are the absolute P1-P3 pitches, spread
  (with +/-1 st detune above 80), Density, Length and Width. The delay tap
  reads its own `rte` time, the 6..384-tick generated curve with the
  rhythmic stops marked, clamped to N, with no glide; the curve includes
  4 positions below the lowest pinned stop (/64). S091 P6: a tap fade
  (`tap_d`/`tap_on`) applies a one-block linear fade on delay time changes
  to avoid clicks. S091 P5 A: a rhythmic
  Position stop is limited to the last stop that fits the line (the engine
  caches the fit per tempo and line); grain spans are rounded down to a
  block boundary, steals are padded to one, and an idle voice's gains are 0
  (the window needs no lower clamp). The echo is plain feedback with gain
  0..0.98 over `fbk` (no DC blocker, no soft knee; it always decays). One
  hardware RNG word per block is expanded by an integer hash. Whole-Effect
  worst case 180.8 per frame (S094 P2, SVF restored; was 165.3 S091 P6).
- **`rvb` 8-bit four-line FDN (engine C, loop 131).** Four integer taps from
  one packed 32-bit word per frame (one 8-bit lane per line), one-pole
  low-pass damping per line (S091 P5 R1: damping is LP-only; `ton` above
  63 plays flat, shown as `0` on the page), a 4 × 4 Hadamard mix scaled ½,
  and per-line decay gains from `fbk` as RT60; the lanes are offset-binary
  so one XOR makes `ldrsb` read signed values. `snc` freezes (`g_k` 0.999,
  input off) and the wet path plus the line feedback open per block only
  once `valid` covers the longest line. The output filter is the shared
  TPT SVF (stage 4; restored from one-pole in S094 P2 §23).
  Whole-Effect worst case 163.9 per frame.

`rte` is clamped per sub-type by `crumpBit_subtypeRateLimit()` through the
runtime-aware `effective_max(rt, index, share)`; BUFFER_DEPENDENT rows are
re-applied every block, so a switch or share change is clamped one block
later. Since S091 P5 A `spn` is not BUFFER_DEPENDENT: the `grn` engine
limits its Position stop itself, while the page label shows the stop that
plays (`crumpBit_grainPosLimit()`). The `dly` map is the S091 P2 lane table;
every lane read goes through
`effects_laneTable(record)`, keyed by the Scene's stored `sub`.

- **Offset binary** (user decision): code 128 is silence, the transfer is
  mid-tread, and one LSB is 1/128 of full scale. With every bit normal the
  type is still an 8-bit quantiser: it is never transparent, and quiet sends
  use few codes.
- **What the data lines do:**
  - bit 7 off or inverted moves even silence to −1.0 (full-scale DC);
  - lower bits off remove weight 2ⁿ (coarser steps, DC about −2ⁿ⁻¹);
  - lower bits inverted add a signal-correlated ±2ⁿ pattern, and silence
    becomes +2ⁿ/128.
  - The AC coupling returns the output to 0 in tens of ms. It cannot hide
    the step when a high bit switches (τ ≈ 16 ms, a thump). Without it the
    DC would also build to `DC/(1 − fb)` in the loop and pin it at a rail.
- **Rate/Sync (per block; S091 P2; `dly` table S091 P6):**
  - `rte` is interpreted per sub-type. `dly`: samples =
    `crumpBit_dlyTime[rte] × per_tick`, a dedicated 128-step tempo-relative
    curve with 13 pinned rhythmic stops (/64 through 2br); `per_tick =
    27,567.5 / BPM`, one multiply per block. `cmb`: MIDI note (semitone
    comb table). `pit`: window size in ms. `grn`: grain-time curve with
    rhythmic stops. `rvb`: raw number (line lengths).
  - **`dly` delay time table (S091 P6):** the old `rateTable[]` exponential
    curve and the `crumpBit_divisionFor()` Sync binary search were replaced
    by `crumpBit_dlyTime[128]`. The table stores tick-relative values;
    tempo changes move the delay target automatically (the tape glide still
    smooths it). `snc` was removed from `dly` lanes — the sync/free choice
    is now implicit in the table's tempo-relative curve. The old Sync path
    (`crumpBit_divisionFor()`) and `rateLimit` call are no longer used by
    `dly`; the other sub-types that had their own `rte` interpretations
    were unaffected.
  - the target is clamped to `length − 2`; `rte` is
    `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT`, so EffectsManager lowers the
    resolved value through `crumpBit_ops.effective_max`
    (`crumpBit_subtypeRateLimit()`; `crumpBit_rateLimit()` is the `dly`
    case) and the page clamps edits through
    `effects_paramEffectiveMax()`. A stored value above the clamp is kept
    and plays when the share grows; the page shows the clamped value.
- **Feedback, tone and knee (per block; S091 P2):**
  - `fb = v/100` up to 100, then `1 + (v − 100)·0.2/27`, so 100 is exactly
    unity and 127 is 1.2. `mix = row/127`;
    delay pan `gL = p ≤ 63 ? 1 : (127 − p)/64`, `gR = p ≥ 63 ? 1 : p/63`.
    All ramp linearly across the block; their origins are recomputed from
    the previous raw rows.
  - the in-loop DC blocker (`R 0.99928775`, ~5 Hz) is always on: above unity
    any offset would otherwise grow to a rail.
  - `ton` is one pole `s += k·(v − s)` with `k` from
    `crumpBit_toneK[]`: 0..62 low-pass, 63 flat (a bit-exact pass-through,
    weights `(0, 1)`), 64..127 high-pass. The tilt is applied inside the
    loop, so repeat *n* is shaped *n* − 1 times and the first echo is heard
    raw.
  - the return is clamped to ±1.5 and then `y = v − (4/27)·v³`: the fixed
    soft knee (§0A D7). It reaches ±1 with zero slope at the clamp, so the
    return alone never clips the 8-bit ADC and the loop gain falls below
    unity at high level (self-oscillation settles). The deferred `tdr`
    drive will replace it with the voice Padé knee.
- **Output filter (`typ frq res bal`; per block, S091 P2, balance S091 P4):**
  - `g = crumpBit_filterG[frq]`, the voice cutoff law
    (`fastTan(π·0.45·valueShaperF2F(frq/127, FILTER_SHAPER))`) from a
    table; `R` is the voice law `1 − res/127` with the low-end snap
    redirected to 0.0625 (Q ≤ 8, plan Q4); `a1 = 1/(1 + g(g + 2R))`,
    `a2 = g·a1`, `a3 = g·a2` (one division per channel per block). S091 P4
    `bal` (row 22, 63 = 0) splits the cutoff: the right channel uses
    `frq + (bal − 63)`, the left `frq − (bal − 63)`, each clamped to
    0..127. Both coefficient sets come from the same expression, so at
    `bal` 63 the result is bit-identical to S091 P2. `bal` has no lane.
  - the output mix is `out = cx·x + cb·bp + cl·lp` with the voice forms
    (LP `lp`, HP `x − 2R·bp − lp`, BP `bp`, UBP `2R·bp`, Nch
    `x − 2R·bp`, Pek `lp − (x − 2R·bp − lp)`); `LP2` plays as LP and
    `off` passes the input. Every type runs the same arithmetic.
  - the four coefficients ramp linearly over one block from the previous
    block's set, so a `typ` Morph step, a Pattern type lock or a `res`
    step is a one-block crossfade, not a click.
- **Arena use:** one byte per sample. The loop length is
  `min(share bytes, 126,656)`. On a share change the newest audio is kept
  when `write_pos` still fits (the tail), otherwise the loop is re-seated;
  either way the gliding delay is clamped to `length − 2`. A voice-unit
  reservation therefore shortens the loop and bends the delay, and the Rate
  is clamped to the same limit.
- **"Clear unless you adopt" by masking:**
  - `init` marks the loop unseated (`length = 0`) and unprimed, and seeds
    a flat tone and the filter `off` (with `off` as its ramp origin);
  - the first block seats it (`write_pos = 0`, `valid = 0`) and starts the
    delay on its target, with no glide;
  - reads further back than `valid` are muted;
  - `buffer_changed` keeps or re-seats the loop as above;
  - it never adopts a previous owner's content;
  - a same-type Scene switch keeps everything, so tails ring on.
- **Runtime (100 B since S091 P4):** 56 B shared (the AC outputs, the four TPT
  states, the two previous data-line codes, every row byte 3..22, `subtype`
  and `active_subtype`, the ramp origins, `primed` and the deferred `drv`
  byte, and the output filter's `bal`) plus a 44 B union of the sub-engine
  states (`dly`/`cmb` and `pit` 28 B, `grn` 44 B, `rvb` 28 B). `dly`,
  `cmb` and `pit` share the
  `length/write_pos/valid` prefix so a switch between them keeps the
  recording. `write_param` only stores raw bytes; conversions happen once per
  block, so an LFO costs nothing extra.
- **Constant CPU:** every stage runs every sample whatever the settings (the
  delay at Mix 0, feedback at 0, the bit logic with all bits normal, the
  filter with every type including `off`). There is no per-sample
  division or transcendental; per block there is one divide for `a1`, the
  Rate limit and table lookups (the `dly` Sync search was removed in S091
  P6). The filter is a second pass
  over the block so the stage-2 loop's register pressure stays bounded.
- **Cost (S094 P2, per sub-type worst case per frame):** `dly` 157.6,
  `cmb` 153.8, `pit` 154.1, `grn` 180.8, `rvb` 163.9. Worst is `grn`
  180.8, within the 181-cap gate (`crumpbit_cpu.py`; raised from 170 in
  S094 P2 §23 to accommodate the restored SVF on `grn`/`rvb`). About
  16,280 cycles against the 16,634 Effect budget share. Flash: 712,684 B
  (headroom 40,980 B). Runtime: 100 B.
  - (S091 P3 baseline for `dly` was 172 per frame; S091 P5 brought it to
    160.0; P6's delay time table removed the Sync search overhead.
    S094 P2 restored the SVF on `grn`/`rvb`, raising `grn` from 165.3.)

### 6.2 CrumpBit binding rules

- **Sub-type dispatch (S091):** five sub-types (dly/cmb/pit/grn/rvb) behind
  a per-block dispatch. Four stages: shared dry path, one sub-engine, shared
  MIX stage (`mix`, lane 3 everywhere, never freeable), shared output filter
  (TPT SVF for all five sub-types since S094 P2; was one-pole for grn/rvb).
  `sub` is Pattern-automatable only; never LFO, Morph, or FX lane.
- **Lane maps:** per stored `sub` (`effects_laneTable()`). Owners:
  `CRUMPBIT_ROWS_SHARED` / `_SUBTYPE` / `_GRN`.
- **Labels hook (S091 P6):** replaces `name3` for per-sub-type row names.
- **`dly` delay time table (S091 P6):** `crumpBit_dlyTime[128]` replaced the
  old Sync search. `snc` removed from `dly` lanes.
- **grn model (S091 P4/P5/P6):** flat-top windows, voice stealing, overwrite
  clamp, freeze with anchor, tap fade on time changes.
- **`fin` row (S091 P5):** pit fine pitch ±52 cents, row 23.
- **SVF restored on `grn`/`rvb` (S094 P2 §23):** the one-pole output filter
  added in S091 P5 F1 was replaced by the original TPT SVF. All five
  sub-types now share the same SVF output stage.
- **`CrumpBitRuntime` 100 B:** 56 shared + 44 union; 24 B of the approved
  +32 B over 76 B used; 8 B remain.
- **181-instruction/frame cap (S091 P5, raised S094 P2):** every CrumpBit
  sub-type must stay at or below 181 instructions per frame, worst case.
  Gated by `crumpbit_cpu.py --cap 181` in the test Makefile. `grn` is the
  binding sub-type at 180.8/frame (0.2 margin). `crumpBit_dataLines` rebuild
  (~48/frame for one block on bof/biv change) is outside the cap by design.
- **Host laws:** CB1–CB23, 25 break builds.
- **24 rows** total.

---

## 7. Pitfalls

- **`off` does no bus work at all.** Code that runs "every block" in an
  Effect does not run while `off`; `init()` handles re-entry.
- **Float overshoot is legal on the bus** and is clamped only at ±255 at the
  return. Keep the Effect's own feedback bounded.
- **Stale comments (S074):** `CrumpBitEffect.h` still quotes the old
  73,632 B minimum share (now 73,600 B).
