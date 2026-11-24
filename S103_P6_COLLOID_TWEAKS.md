# S103 P6 — Colloid tweaks: `spr` / `mot` in a mono route, the paraphonic release hold

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline` (the S103 P5 tree,
uncommitted). **Status:** **implemented**, built and checked on the host.
**Uncommitted. Not run on the device.** §9 holds the implementation notes;
§1–§8 are the plan as approved, with the answers applied where they changed
it (each such place is marked "as built").

**Your answers (2026-11-24):** Q1 yes; Q3 **equal-power**; Q4 approved; Q5
approved; Q6 ok; every other recommendation approved (Q2 the relabel
included).

**Your request (2026-11-24):**
1. In the dual-filter mono mode, `spr` and `mot` should control:
   - `spr`: the balance between the two filters. Lowest = only the primary
     filter (the one that sets both under `str`), highest = only the
     secondary, middle = 50 % of each;
   - `mot`: serial (as now) at the lowest value to parallel at the highest.
     In parallel, the mono signal goes through both filters equally and is
     mixed back to mono.
2. Releasing a held paraphonic chord gives a small pitch tail, as the
   oscillators start to slew back to the interval parameters. Add a small
   delay on key release only, so the rest of the chord can be released
   first. You asked: 50 ms, or an exponential ramp into the slew?

**Labels:**
- **Code:** read from the tree, with a file and line.
- **Measured:** from the linked image of this tree (`build/lxr02.elf`, S103
  P5).
- **Est.:** an estimate with its basis, to be measured at the gate named.

**Binding rules that apply** (`MEMORY.md`):
- constant CPU, and the S089 P2 ceiling: no per-sample cost may rise on the
  charged path;
- the S093 per-block accounting;
- RAM needs sign-off;
- no backward compatibility;
- `INSTRUMENTS_DSP_REFERENCE.md` §12;
- runtime work stays non-blocking (MIDI is main-loop).

No step here edits `MEMORY.md` or `knowledge_files/` (§8 lists what goes
out of date).

---

## 0. Summary

| Item | Plan |
|---|---|
| **Part 1, mono `spr`** | Filter balance: 0 = filter A only, 63 = both (serial: the full A→B chain as today; parallel: 50/50), 127 = filter B only |
| **Part 1, mono `mot`** | Serial ↔ parallel morph: 0 = today's serial chain, 127 = parallel (both filters fed the same mono input, mixed by `spr`) |
| **Part 1, compatibility** | At `spr` 63, `mot` 0 the mono output is **bit-identical** to today's (host law). Stereo (`rte str`) is untouched: `col_renderPar()` does not change |
| **Part 1, CPU** | Only the mono loop grows: est. +14–18 instructions a sample. Measured headroom: the mono modes sit **26.2–26.8 below** the stereo modes, which set the Colloid's charge. **The charged worst case is unchanged.** **Measured as built:** mono modes +19.7 a sample, all **7.1–7.2 under** their stereo modes; stereo modes ±0.1 (§9.4) |
| **Part 1, RAM** | +28 B inside `ColloidVoice` (no allocation grows). Approved (Q4). **Measured:** 1,248 B of the 1,264 B slot reserve |
| **Part 2, the fix** | A **release hold** in the MIDI note-mode layer. A paraphonic key release defers its automation writes (interval back to the patch, or the coa / interval re-root) by **50 ms**. If the chord is fully released within the hold, they are **never written**, so the chord rings through its release with no pitch movement. A note-on, a Pattern trigger or a Scene change during the hold writes them at once. Presses are not delayed |
| **Part 2, exponential slew-in** | **Not recommended** (§2.4): the DSP cannot tell a release from a press, and the oscillators would still move during the hold |
| **Part 2, CPU** | 0 DSP. MIDI: one compare per group per main-loop pass |
| **Part 2, RAM** | +24 B SRAM1 static (`MidiNoteModes.c`, six 32-bit release stamps); the pending mask uses the record's unused `flags` byte. Approved (Q5). **Measured:** `midiNoteMode_relStart` 24 B (`nm`) |

---

## 1. Part 1 — `spr` and `mot` in a mono route

### 1.1 What exists (code)

- **The rows.** `col_spread` (`spr`) and `col_motion` (`mot`): `TYPE_FLT`,
  byte / 127, on the FM page (`ColloidParameters.c:305–306`, pages at
  `:462`). In stereo they place the stack's oscillators: spread and moving
  pan, `col_oscControl()` `ColloidVoice.c:986–987`, into the side bus.
- **In a mono route they do nothing today.** `col_renderSer()` never reads
  the side bus (`ColloidVoice.c:1797–1814`). `colloidParameters_cellInactive()`
  shows `spr`, `mot` and `npn` as "---" when `rte` is a filter type
  (`ColloidParameters.c:548–553`).
- **The mono route** is `rte` = a filter type (`COL_RTE_TYPE(r)`,
  `ColloidVoice.h:292–294`). `col_renderSer()` (`ColloidVoice.c:1815–1892`)
  runs, per sample:
  - `x = col_drive(M·inGain + noise·nGM)`;
  - **filter A** (type `typ`, the base cut / res / fam: the primary, the one
    that sets both under `str`);
  - `col_drive()` again, at the inter-stage gain `g2 = inGainR =
    2^(COL_DRIVE_OCT·(drv + dro))`, folded into A's output mix (`mHA·g2` …);
  - **filter B** (type `rte`, plus the `fro reo eno dro` offsets: the
    secondary);
  - then the post overdrive, the amp and the tone, with L = R.
- `col_drive()` is the clamped cubic, `ColloidVoice.c:324–329`.
- The route decides the loop once a block (`ColloidVoice.c:2116–2119`). Both
  filters' coefficients are computed every block in either route
  (`:1524–1534`).
- **The gains:** `ColloidVoice.c:1484–1495`. Under mono, `dgR` is overwritten
  with `dgL` after `inGainR` is set: "the noise enters the first stage only".

### 1.2 The law (proposed)

The filter-A output is `yA`. Filter B's input is `uB = drive(·)`, and its
output is `yB`. Let `b = spr × 127` (continuous under Morph and the LFO),
`m = clamp01(mot)` and `ε = 1e-4`:

```
kA = clamp01((127 − b)/64 · (1 + ε))     A's share of the serial chain:  1 up to b = 63, 0 at 127
kB = clamp01(b/63 · (1 + ε))             B's share of the serial chain:  0 at b = 0, 1 from 63
s  = 0.5 · (kB + 1 − kA)                 the parallel weight:  0 at 0, exactly 0.5 at 63, 1 at 127

serial   (m = 0):  uB = drive(kA·g2·yA + (1 − kA)·DRY_B);   y = (1 − kB)·yA + kB·yB
parallel (m = 1):  uB = drive(DRY_B);                       y = √(1 − s)·yA + √s·yB   (as built: equal power, Q3)
in between:        cA = (1 − m)·kA,  cX = 1 − cA
                   uB = drive(cA·g2·yA + cX·DRY_B)
                   oA = (1 − m)(1 − kB) + m·√(1 − s)
                   oB = (1 − m)·kB      + m·√s,             y = oA·yA + oB·yB
DRY_B = M·norm·dgR + noise·nGM·dgR/dgL       (B's own input drive, as its channel under str)
      = px · dgR/dgL                          (as built: px, the first drive's input, rescaled)
```

**As built (Q3):** the parallel mix is equal-power, so the two output
weights no longer sum to 1 and B's weight gets its own ramp (`oB`). In the
plan, `oB` was `1 − oA`.

**What you hear:**

| | `mot` 0 (serial) | `mot` 127 (parallel) |
|---|---|---|
| `spr` 0 | A only | A only |
| `spr` 0 → 63 | A, crossfading into A→B | A and B mixed, B rising |
| `spr` 63 | **A → B, today's chain** | A and B, each at −3 dB (equal power) |
| `spr` 63 → 127 | A fades out of the chain, so B is fed more and more of the dry input | B rising to all |
| `spr` 127 | B only, on its own drive | B only |

`mot` between 0 and 127 morphs both the feed into B (from A's output to the
dry input) and the output mix, continuously.

**Why this shape:**
- **The serial "middle = both".** In a chain, "50 % each" can only mean
  that both filters are fully in, so `spr` 63 at `mot` 0 is exactly the
  current sound.
  - **Below 63**, B is faded out of the output (A alone at 0).
  - **Above 63**, A is faded out of B's feed (B alone at 127).
- **Where each end bypasses from.** "A only" is A's output before the
  inter-stage shaper, a true bypass of the second stage. "B only" is B fed
  through its own drive (`drv + dro`), as its channel under `str`, so the dry
  input is shaped once, not twice.
- **The parallel mix is equal power (your Q3 answer).** Two different
  filters keep a steady level across the balance. Two identical filters
  (zero offsets) sum to +3 dB at the middle (law CO-MONO part 4). The serial
  crossfade stays linear, because A and A→B are correlated signals.
- **The exact plateau at 63.** `ε` makes byte 63 give `kA = kB = 1`
  exactly, despite the float row (63/127 × 127 is 63 ± 4e-6). That is what
  lets law CO-MONO1 be bit for bit.

### 1.3 Implementation

| # | File, place | Change |
|---|---|---|
| C1 | `ColloidVoice.h` (struct, after `inGainR`) | **As built:** +7 floats. `monoCX` / `monoCXStep` (B's dry-feed share), `monoDry` (`dgR / dgL`), `monoOA` / `monoOAStep` (A's weight ÷ g2), `monoOB` / `monoOBStep` (B's weight). The plan's `monoGB`, `monoNB` and `monoInvG2` are replaced: B's dry feed is `monoDry · px` (both its bus and noise terms at once), and the ÷ g2 is in `monoOA`'s target. That frees the float the equal-power `oB` ramp needs. +28 B |
| C2 | `ColloidVoice.c`, the gain block `:1484–1495` | Under mono, keep the true `dgR` for `monoGB` / `monoNB` before it is overwritten |
| C3 | `ColloidVoice.c`, new `static noinline col_monoMixControl()` | The law of §1.2 → the targets of `monoCX` / `monoOA` and their steps (`(target − now)/32`); `monoInvG2 = 1/inGainR` (one VDIV a block). Called from the async **only under a mono route**, so the stereo path does not change by one instruction |
| C4 | `col_renderSer()` `:1815–1892` | See below |
| C5 | `ColloidParameters.c:548–553` | `spr` and `mot` stay live in a mono route (`npn` still inactive). The comment is updated |
| C6 | `menu.c` `menu_instrumentCellLabels()` `:12867` | Under a mono route, `spr` / `mot` show the long view **"Filter / Balance"** and **"Filter / Ser>Par"** (literal strings, the Spc precedent at `:12878–12887`). Short labels stay `spr` / `mot`. Q2 |
| C7 | `colloid_init` files | None: `init.col` routes `str` (`Colloid_initVoice()` sets `COL_RTE_STR`, `ColloidVoice.c:1160`), so the default sound does not change |

**C4 in detail**, per sample:
- **A is unchanged** (its mix is still g2-folded).
- **B's input** (as built): `uB = drive((1 − cX)·yAg + (cX·monoDry)·px)`,
  where `px = M·inGain + noise·nGM` is the first drive's input.
- **B is unchanged.**
- **The output** (as built): `y = oA·yAg + oB·yB`, with `oA` already
  divided by g2.
- **The ramps:** `cX`, `oA`, `oB`, each `+= step`.
- **The post stage is unchanged.**
- **Exactness:** with `cX = 0` and `oA = 0` every new term is `0·finite` or
  `1·v`, so the result is bit for bit today's. This is CO-MONO1.

**Per-block cost:** `col_monoMixControl()` ≈ 40–60 instructions (est.:
clamps, three products, one VDIV), on the mono modes only.

### 1.4 CPU

**Measured** (`colloid_cpu.py`, this tree), all-in per sample:

| Mode | stereo (`par`, charged) | mono (`ser`) | headroom |
|---|---:|---:|---:|
| plain12 | 416.2 | 389.4 | 26.8 |
| pwm6 | 400.8 | 373.9 | 26.9 |
| pwmw6 | 403.9 | 377.1 | 26.8 |
| scan6 | 404.3 | 377.5 | 26.8 |
| chop6 | 386.7 | 359.9 | 26.8 |

**Mono growth (est.):**

| Part | Instructions a sample |
|---|---:|
| B's input (`cA`, two FMAs, one product, one FMA) | ≈ 5 |
| The output (two products, one subtraction, one FMA) | ≈ 4 |
| Two ramps | 2 |
| Register spills (the loop holds `yAg` across B now) | ≈ 3–5 |
| Per block, `col_monoMixControl()` / 32 | ≈ 1.5–2 |
| **Total** | **≈ 14–18**, inside the 26.8 headroom |

**Gates:**

| Gate | Rule |
|---|---|
| G6-1 | every `*-ser` mode ≤ its `*-par` mode (`colloid_cpu.py`, which then charges `col_monoMixControl()` to the `ser` modes) |
| G6-2 | every `*-par` mode unchanged within 0.5 (`--baseline`); `col_renderPar` loop exact |
| G6-3 | `elf_s099_check`: all loops exact except `col_renderSer` (changed by design). The S099 baseline is kept; the new loop is recorded in `tools/dsp_test/baselines/s103p6/` |
| G6-4 | the mixer hot loops 0 grew (the Colloid objects are `-fno-lto`, `Makefile:347–358`, so none is expected) |
| G6-5 | `sizeof(ColloidVoice)` ≤ 1,264 (the existing assert, `InstrumentManager.c:355`) |

**If G6-1 fails:** give the mono loop the stereo loop's 8-sample coefficient
stepping (S096 P3, `col_renderPar()` comment). That frees six adds a sample,
but loses CO-MONO1's bit identity, which becomes a tolerance.

### 1.5 Host laws (`tools/dsp_test/modules/colloid.c`)

| Law | What |
|---|---|
| **CO-MONO1** | `rte` LP, `spr` 63, `mot` 0, over 200 blocks with drive, offsets and noise on: `col_renderSer()` equals a frozen copy of today's loop (copied verbatim into the test from `b79707b3`) **bit for bit** |
| CO-MONO2 | `spr` 0 at `mot` 0 and at `mot` 127: the output does not change when B's coefficients are scrambled (A only) |
| CO-MONO3 | `spr` 127 at `mot` 0 and at `mot` 127: the output does not change when A's coefficients are scrambled (B only, fed its own drive) |
| CO-MONO4 | `mot` 127, `spr` 63, zero offsets and `dro` 0: the output equals **√2 ×** the `spr` 0 output within 3 LSB (identical filters in parallel, equal power; as built for Q3) |
| CO-MONO5 | the ramps: a `spr` or `mot` step lands at the block's end (the coefficients equal their targets after 32 samples), and a 0 → 127 → 0 sweep per block for 2,000 blocks stays finite and in int16 |
| CO-SER | (existing) L == R, still |
| **Break builds** | `colloid_break_monofeed` (B fed from A in parallel) must fail CO-MONO3/4; `colloid_break_monoramp` (no per-sample ramp) must fail CO-MONO5 |

---

## 2. Part 2 — the paraphonic release hold

### 2.1 What happens today (code)

`midiNoteMode_paraOff()` (`MidiNoteModes.c:407`) removes the key and, while
keys remain, queues transient automation at once:
- **another owner released:** its interval goes back to the patch value
  (`:478–486`);
- **the fundamental released:**
  - `coa` re-roots to the earliest remaining key F' (`:440–448`);
  - F''s old interval returns to the patch (`:449–459`);
  - every owned interval is rewritten against F' (`:460–475`).

The writes go through `seq_queueForegroundAutomation()`, and the Colloid's
oscillators then slew to the new targets at `slw` (`ColloidVoice.c`,
`col_oscControl()`).

A chord is released one finger at a time. So every key but the last moves
some oscillators, and the slew is audible in the amp release that follows the
last key (sus off, `:423–433`). The same path serves the S102 chromatic pads
(`buttonHandler.c:1561`, `:1708` call `midiNoteMode_noteOff()`).

### 2.2 The fix: defer the release's writes, cancel them if the chord ends

- **On a release that leaves keys held:** update the key list, the owners
  and `fund` **at once**, as now. Do **not** queue the writes. Instead:
  - set the targets they would have written into a **pending mask** (4 bits:
    coa, in1, in2, in3) in the voice record's `flags` byte. It is documented
    as "midiHold mirror, reassert pending" (`:59`) but nothing in the tree
    reads or writes it;
  - set the slot's **deadline** to now + `MNM_RELEASE_HOLD_US`
    (`timebase_tim2Now()`, `timebase.h:110`).
- **On the last key's release** (count 0): clear the mask with the group,
  then sus off as today. **The deferred writes are never sent**, so the chord
  rings through its release unchanged. This is the case you described.
- **The flush** (`midiNoteMode_flushRelease(slot)`, new). For each masked
  target it computes the value **from the state at flush time**:
  - coa = patch coa + fund − note0;
  - an owned interval = 63 + key − fund;
  - an unowned interval = the patch byte.

  It queues them as one LiveRecord automation event (≤ 4 targets, the
  record's limit, `:289`) and clears the mask. It runs:
  - from `midiNoteMode_service()` (`:571`, main loop) when the deadline has
    passed (`timebase_tim2Delta()`, wrap-safe);
  - at the **start of `midiNoteMode_paraOn()`** for that slot, so a new key
    lands on a consistent chord. A restore and the new key's write to the
    same interval drain in queue order before the block renders, so the
    later write wins with no block in between (`sequencer.c:4259–4275`);
  - in **`midiNoteMode_prepareTrigger()`** (`:554`) and on a **Scene change**
    (the epoch branch of `midiNoteMode_service()`), before
    `midiNoteMode_queueState()`. That function re-asserts coa and the owned
    intervals, and would otherwise apply half of the pending change.
- **Presses are untouched:** no delay on a note-on's own write.
- **`midiNoteMode_freeGroup()`** (`:194`) clears the slot's mask.

### 2.3 The hold time

**`MNM_RELEASE_HOLD_US` = 50,000 (50 ms)**, a constant in `MidiNoteModes.c`.
- Fingers lifting off a chord are usually spread over well under 50 ms
  (est., by ear). A deliberate "drop one note" is slower, and its pitch then
  moves 50 ms later than today.
- **Range to try on the device:** 30–80 ms. A row is not proposed: the
  Colloid has no spare Basic index before its effect rows.

### 2.4 Why not an exponential ramp into the slew

- **It would sit in the voice** (`col_oscControl()`). The voice sees only
  new interval targets and cannot tell a release from a press, so chord
  changes while playing would also glide in slowly. You asked for release
  only.
- **The pitches would still move during the ramp.** Releasing a chord over
  ~30 ms would still start a (slower) tail. The hold removes the tail
  entirely when the chord ends.
- **Cost:** a per-oscillator onset state (10 × 4 B in the voice) and
  per-block work, against 0 DSP for the hold.

It can be added later if you want a softer drop of a single note *after*
the hold. That is not proposed now.

### 2.5 Host laws (`tools/midi_test/notemode_test.c`, the existing harness)

`notemode_host.h` gains a controllable `timebase_tim2Now()` /
`timebase_tim2Delta()` pair.

| Law | What |
|---|---|
| NM-HOLD1 | C3 E3 G3 held; E3 then G3 then C3 released 10 ms apart: **no** interval or coa write is queued; only sus = off |
| NM-HOLD2 | C3 E3 G3 held; E3 released, time + 49 ms: nothing queued; + 51 ms (service run): in1 = the patch byte, once, reported to LiveRecord |
| NM-HOLD3 | C3 E3 G3 held; C3 (the fundamental) released, + 51 ms: coa + 4 and in2 = +3 queued, as today's immediate writes (the §2.1 example), F' = E3 |
| NM-HOLD4 | E3 released then A3 pressed after 20 ms: the pending writes are queued **before** A3's, in that order, and A3 takes the freed group |
| NM-HOLD5 | a Pattern trigger (`prepareTrigger`) and a Scene epoch change during the hold flush first |
| NM-HOLD6 | the TIM2 wrap: a deadline across 2³² still fires at 50 ms |
| (existing) | every current notemode law still passes (the immediate-write expectations move to "after the hold") |

---

## 3. RAM

| Item | Bytes | Region / owner | Sign-off |
|---|---:|---|---|
| `ColloidVoice` +7 floats | +28 (measured 1,248 of 1,264) | the tagged slot union (no allocation grows) | **Q4 approved** |
| `midiNoteMode_relStart[6]` (`uint32_t`; named `relDeadline` in the plan) | +24 | SRAM1 `.bss`, `MidiNoteModes.c`. The pool assert moves from 2,384 to 2,408 | **Q5 approved** |
| The pending mask | 0 | the unused `flags` byte of `midiNoteMode_voice_t` | — |

The cheaper Q5 alternative is one shared deadline (4 B). Two groups released
within 50 ms of each other would then both wait for the later deadline.

---

## 4. Order of work

| Step | What | Gate |
|---|---|---|
| 1 | Part 2 (MIDI only): mask, deadline, flush, the four call sites, the host stand-ins, NM-HOLD1–6 | `make -C tools/midi_test run` passes; firmware builds |
| 2 | Part 1: C1–C3, then C4 with CO-MONO1 first (bit identity before any other change), then C5–C6 | CO-MONO1 |
| 3 | CO-MONO2–5 and the two break builds | `make -C tools/dsp_test colloid` |
| 4 | CPU and image: `colloid_cpu_check`, `elf_s099_check`, the mixer diff, `link_budget.py` | G6-1 … G6-5 |
| 5 | Device: mono `spr` / `mot` sweeps by ear; paraphonic chord releases (MIDI keyboard and the S102 chromatic pads); the `p` probe on one mono Colloid at its dearest form | no pitch tail on a chord release; the probe ≤ the stereo figure |

---

## 5. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Existing mono patches change sound.** Their `spr` / `mot` were inactive, so they hold arbitrary values (`init.col` stores 20 / 10), and now read as a balance (spr 20 ≈ mostly A) | No migration (the standing rule). Set `spr` 63, `mot` 0 for today's sound. `init.col` is `str`, so it is unaffected |
| R2 | One row means two things by route: an LFO or Morph on `spr` that sweeps stack spread under `str` sweeps the filter balance under mono | Inherent to reusing the rows (your choice). C6 shows which meaning applies |
| R3 | The CPU estimate is low | 26.8 headroom against ≈ 18 est.; the fallback in §1.4 |
| R4 | A held note dropped on purpose moves 50 ms later | That is the trade; the time is a constant (§2.3) |
| R5 | LiveRecord records the deferred writes 50 ms after the release (possibly on the next step) | The recorded take matches what played; a fully released chord records only sus = off |

---

## 6. Questions

| # | Question | Recommendation |
|---|---|---|
| Q1 | Serial at `spr` 63 = the full A→B chain (both fully in), with B fading out below and A fading out of B's feed above (§1.2). Is that your "middle 50 % both" for serial? | Yes: it keeps today's chain reachable exactly |
| Q2 | Relabel the long view of `spr` / `mot` under a mono route ("Filter / Balance", "Filter / Ser>Par")? | Yes (C6); the short labels stay |
| Q3 | Parallel mix: linear (as asked) or equal-power? | Linear: identical filters stay at unity; uncorrelated outputs dip ≈ 3 dB at the middle |
| Q4 | +28 B inside `ColloidVoice` (slot reserve unchanged) | OK |
| Q5 | +24 B SRAM1 for six deadlines, or 4 B shared | 24 B (exact per slot) |
| Q6 | Release hold 50 ms; exponential slew-in not done | As written; tune 30–80 ms by ear |

---

## 7. What this does not change

- the stereo route and its loop;
- the Colloid's charged CPU;
- the paraphonic press behaviour;
- the NORMAL and LAYER4 note modes;
- the Colloid's rows, keys and stored data.

---

## 8. Statements this plan would make out of date (for you to log, if adopted)

- `COLLOID_MODULE_REFERENCE.md` / `COLLOID_MODELLING_REFERENCE.md`:
  - the mono route's filter structure;
  - `spr` / `mot` live in a mono route;
  - the voice size.
- `MENU_UI_SPEC.md`: `spr` / `mot` relabelled under a mono route.
- `MIDI_SPEC.md` (the note modes): the paraphonic release hold.
- `MEMORY.md`:
  - the Colloid voice size (1,248 B, measured);
  - MidiNoteModes RAM (2,380 → 2,404 B);
  - the release hold.

---

## 9. Implementation notes (2026-11-24)

### 9.1 What changed from the plan

| # | Plan | As built | Why |
|---|---|---|---|
| D1 | Parallel mix linear, `oB = 1 − oA` | **Equal power:** `oA = (1 − m)(1 − kB) + m√(1 − s)`, `oB = (1 − m)kB + m√s`, each with its own ramp | Your Q3 answer |
| D2 | B's dry gains `monoGB` (bus), `monoNB` (noise) and `monoInvG2` stored | **`monoDry = dgR / dgL`** times `px` (the first drive's input, bus and noise together); the ÷ g2 is folded into `monoOA`'s target | It keeps the approved 28 B while adding D1's `oB` ramp. It is exact by construction, because `px` already carries the stack's normalisation, the pwm level and the chop's `inGain` rescale (`col_chopBlock()` scales `inGain` in the sync, so a stored bus gain would have missed it) |
| D3 | The MIDI deadline array `midiNoteMode_relDeadline` | `midiNoteMode_relStart`: the stamp of the latest release | A stamp and the wrap-safe `timebase_tim2Delta()` express "50 ms since the latest release" directly; each release restarts the hold |
| D4 | — | `colloid_cpu.py` now fails a `ser` mode over its `par` mode (G6-1) and charges `col_monoMixControl()` to the `ser` modes | So G6-1 is a standing gate, not a one-off reading |
| D5 | — | A break build for the release hold (`notemode_break_hold`) in `tools/midi_test` | The DSP suites' practice; the plan listed laws only |

### 9.2 Part 1: the code (every change carries its comment block in the source)

| File | Change |
|---|---|
| `Core/DSP/Instruments/Colloid/ColloidVoice.h` | After `inGainR`: the seven `mono*` floats, with a block giving the per-sample form, each member's meaning, the S102 identity at `spr` 63 / `mot` 0 and the accessors |
| `Core/DSP/Instruments/Colloid/ColloidVoice.c` | `COL_MONO_EPS` (1e-4: the plateau at byte 63). `col_monoMixControl()` (new, `noinline`, before the async): the law of §1.2, with a block on the law, the reasons, the inputs and the cost. `Colloid_initVoice()`: the mix at rest on the serial chain (`cx` 0, `oa` 0, `ob` 1, `monoDry` 1). The async's gain block: `col_monoMixControl(voice, dgL, dgR)` under a mono route, before the mono overwrite of `dgR`, so the stereo route's async is unchanged. `col_renderSer()`: an S103 P6 block in its header; `px` kept; `yAg`; `uB = drive((1 − cx) yAg + (cx rD) px)`; `y = oa yAg + ob yB`; three ramps and their write-back. Two older comments that said `spr` / `mot` have no effect in mono are corrected |
| `Core/DSP/Instruments/Colloid/ColloidParameters.c` | `colloidParameters_cellInactive()`: `spr` and `mot` are live in every route (`npn` still blank in mono), with a block. The rows' comment names the mono meaning |
| `Core/DSP/Instruments/InstrumentManager.h/.c` | `instrumentManager_slotColloidMono(slot)` (new): 1 for a Colloid (col or clx) in a mono route. Its contract block is in the header (the `instrumentManager_slotVoiceChain()` pattern) |
| `Core/Menu/menu.c` | `menu_instrumentCellLabels()`: under a mono Colloid, `col_spread` reads **Filter / Balance** and `col_motion` **Filter / Ser>Par** (the category through `menuLabel_cat(CAT_FILTER)`). Keyed by file key, as the WFd rule beside it. The short labels stay `spr` / `mot` |
| `tools/dsp_test/modules/colloid.c` | Group **CO-MONO** (after `test_chop()`, so every earlier law keeps its RNG stream), and the frozen S102 loop `col_renderSerS102()` (copied verbatim from `b79707b3`) |
| `tools/dsp_test/Makefile` | `colloid_break_monofeed`, `colloid_break_monoramp` |
| `tools/dsp_test/colloid_cpu.py` | D4 |
| `tools/dsp_test/baselines/s103p6/` | `colloid_cpu.txt`, `col_render.txt`, `README.md` |

**Bit identity at `spr` 63 / `mot` 0.**
- In that state `col_monoMixControl()` gives `cx` 0, `oa` 0, `ob` 1, each
  with a step of exactly 0. CO-MONO part 1 checks those values and steps.
- The new terms are then `(1 − 0)·yAg + (0·rD)·px` and `0·yAg + 1·yB`, so
  the loop reproduces the S102 loop's int16 output **bit for bit**.
- That is checked over 60 blocks of a driven, resonant, noisy voice with
  offsets on.

### 9.3 Part 2: the code

| File | Change |
|---|---|
| `Core/MIDI/MidiNoteModes.c` | `#include "timebase.h"`. The `flags` field's comment: it is the pending mask now; nothing read or wrote it before. `MNM_RELEASE_HOLD_US` (50,000), `MNM_REL_*`, `midiNoteMode_relStart[6]`, with the block that gives the whole mechanism. The pool assert: 2,408 with `relStart`. `midiNoteMode_init()` clears the stamps. `midiNoteMode_freeGroup()` clears the slot's mask. `midiNoteMode_flushRelease()` (new) computes and queues the masked targets from the current state and reports one LiveRecord automation event. `midiNoteMode_paraOff()`: the hand-over / restore updates state at once and sets the mask and the stamp instead of queuing. `midiNoteMode_paraOn()`: flushes before a new key. `midiNoteMode_prepareTrigger()` and the Scene-change branch of `midiNoteMode_service()`: flush before `queueState()`. `midiNoteMode_service()`: the hold's end (one compare per group per pass) |
| `tools/midi_test/notemode_host.h` | `timebase_tim2Now()` / `timebase_tim2Delta()` over `host_now_us`; `instrumentManager_typeEngine()` (see §9.6) |
| `tools/midi_test/notemode_test.c` | `host_now_us`; the S096 P3 release laws now wait for the hold (each first checks that nothing was queued at the release); NM-HOLD1–7 |
| `tools/midi_test/Makefile` | `notemode_break_hold` (the hold set to 0 must fail), run by `make run` |

**Laws** (all pass; the break build is caught):

| Law | What |
|---|---|
| NM-HOLD1 | a whole chord released inside the hold queues only sus = off, and nothing after |
| NM-HOLD2 | an owner's release: nothing at 49 ms; at 51 ms in1 back to the patch, reported once, written once |
| NM-HOLD3 | the fundamental's release: at 51 ms coa +4, in1 (F''s old group) to the patch, in2 +3 (the S096 P3 values) |
| NM-HOLD4 | a key pressed 20 ms after a release: the restore, then the new key's in1, in that order; nothing left pending |
| NM-HOLD5 | a Pattern trigger and a Scene change write the pending restore first |
| NM-HOLD6 | 40 ms across the TIM2 wrap is still held; 51 ms fires |
| NM-HOLD7 | a second release restarts the hold; both restores go out 51 ms after it |

### 9.4 Measurements (S103 P6 image)

`colloid_cpu.py`, instructions a sample, all-in:

| Mode | par (S103 P5 → P6) | ser (S103 P5 → P6) | ser under par |
|---|---|---|---:|
| plain12 | 416.2 → 416.3 | 389.4 → 409.1 | 7.2 |
| pwm6 | 400.8 → 400.8 | 373.9 → 393.6 | 7.2 |
| pwmw6 | 403.9 → 403.9 | 377.1 → 396.8 | 7.1 |
| scan6 | 404.3 → 404.3 | 377.5 → 397.2 | 7.1 |
| chop6 | 386.7 → 386.8 | 359.9 → 379.6 | 7.2 |
| clx-pwm6-par | 447.7 → 447.7 | — | — |

- **The mono loop:** `col_renderSer` 99 → **116** instructions a sample.
- **The new helper:** `col_monoMixControl()` costs 60 a block (1.9 a
  sample), charged to the ser modes. The plan's estimate was 14–18 in all;
  the measured growth is 19.7.
- **The stereo modes** are within 0.1 of before; the `par` path's own code is
  unchanged.

**Gates:**

| Gate | Result |
|---|---|
| G6-1 | every ser ≤ its par: **pass** (margin ≥ 7.1) |
| G6-2 | par within 0.5: **pass**; `col_renderPar` loop exact |
| G6-3 | `elf_s099_check`: 14 of 15 exact; `col_renderSer` changed (by design, recorded in `baselines/s103p6/col_render.txt`) |
| G6-4 | mixer hot loops 40 equal, 1 shrank, **0 grew** |
| G6-5 | `sizeof(ColloidVoice)` **1,248 B** ≤ 1,264 (a compiled probe; the assert holds) |
| Image | 697,176 B (headroom 56,488 B); ITCM 15,424 B (unchanged) |
| Host suite | `make -k -C tools/dsp_test all`: only the two pre-existing failures (`voice_control` 14, AL-OD0's absent golden file); `special_tags` and `check_special_tags.py` pass; `make -C tools/midi_test run` passes |

### 9.5 CO-MONO, as it stands

| Part | Law |
|---|---|
| 1 | `spr` 63 / `mot` 0: `cx`, `oa`, `ob` = 0, 0, 1 with zero steps, and over 60 blocks the loop's int16 output equals the frozen S102 loop's, bit for bit |
| 2 | `spr` 0, `mot` 0 and 127: replacing filter B (another type, cutoff and state) changes nothing (≤ 1 LSB): A alone |
| 3 | `spr` 127, `mot` 0 and 127: replacing filter A changes nothing (≤ 1 LSB): B alone, fed its own drive |
| 4 | parallel, identical filters, drive 0: `spr` 63 = √2 × the A-alone output (≤ 3 LSB); `monoDry` 1 at `dro` 0 |
| 5 | the six corners of the law (`spr` 0 / 63 / 127 × `mot` 0 / 127): the targets of `cx`, `oa·g2`, `ob`; `monoDry = dgB / dgL` |
| 6 | a 0 → 127 `spr` step lands at the block's end; 2,000 blocks of alternating `spr` and sweeping `mot` stay finite |

**Break builds** (both caught):
- `colloid_break_monofeed` (B always fed from A): parts 2–4 fail by
  thousands of LSB;
- `colloid_break_monoramp` (no ramps): part 6 fails.

### 9.6 Found on the way

- **`tools/midi_test` did not compile** since S099. `MidiNoteModes.c` calls
  `instrumentManager_typeEngine()`, which `notemode_host.h` never stood in
  for. `make run` printed "PASS" only because the committed binary was
  stale and `make` did not rebuild it until a source changed. The stand-in
  is added (every host type is its own engine), and the harness builds and
  passes again.

### 9.7 On the device

1. Flash `build/LXRV2_lxr02.img`. No card file changes: `init.col` routes
   `str`.
2. **Mono `spr` / `mot`** (set `rte` to a filter type):
   - **`spr` 63, `mot` 0:** exactly the old sound.
   - **`spr` 0:** filter A alone. **`spr` 127:** filter B alone (with `dro`).
   - **`mot` 127:** parallel; the balance is equal power, so with zero
     offsets the middle is +3 dB.
   - **The labels:** the long view reads "Filter Balance" / "Filter
     Ser>Par" in mono, "Unison Spread / Motion" in `str`.
3. **Paraphonic chords** (MIDI keyboard on the Colloid's own channel, and the
   S102 chromatic pads):
   - release a held chord: no pitch tail;
   - drop one note of a held chord: it moves after 50 ms;
   - a quick chord change: no glitch.
4. **Tune `MNM_RELEASE_HOLD_US`** (`MidiNoteModes.c`) between 30 and 80 ms
   if 50 ms feels late or early.
5. **Existing mono patches** read their old `spr` / `mot` under the new law
   (R1). Set `spr` 63 / `mot` 0 for the old sound.
