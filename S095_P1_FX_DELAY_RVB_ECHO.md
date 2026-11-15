# S095 P1 — FX types: Delay, Reverb, Echo (plan)

**Status:** plan, reviewed by the user through revision 4 (2026-11-11), then
checked against the tree (S095 review, 2026-11-11: §H.6, §13). No code has
changed. Every CPU, flash and time figure is an estimate until the host
gates and one device run measure it.

**Next session:**

1. Read §H.
2. Ask the user for D23 (§12), the §13 questions A1–A3, and the §H.2 device
   results.
3. Write the implementation schedule from this plan.
4. Implement.

---

## H. Hand-off for the next session

### H.1 Order of work in S095

1. **S095 P1 (this document):** the Delay, Reverb and Echo Effect types, the
   elastic seating, the feed-through handover, and the CrumpBit changes.
2. **Compressor adjustments, at the end of S095 P1 (user).**
   - The user gives the scope. Nothing is planned for it here.
   - The compressor sits in the shared mixer budget: 12,784 cycles per
     block, with the compressor itself measured at 4,284–4,369 (S094 P2
     §17.3).
   - A change that costs more needs a re-budget, reported to the user as
     one net total.
3. **S095 P2:** the Colloid instrument (`S095_P2_DRAFT_COLLOID.md`). It
   depends on the Advanced budget this plan sets (21,635, §5.7).

### H.2 State of the tree when this was written

- **Branch:** `dev-ph7-alkaline`, with uncommitted work: S094 P3, P4, AS-13
  and the P4 comment fixes.
- **Image:** `build/LXRV2_lxr02.img`.
  - Flash: 691,100 B used, **62,564 B headroom**.
  - ITCM: 13,984 / 16,384 B.
- **DTCM:**
  - FxBuffer arena: 126,656 B.
  - `effects_runtime_t`: 100 B (asserted, `EffectsManager.c:154`).
  - `effects_state_t`: 80 B (asserted, `EffectsManager.c:184`), with one
    spare `reserved` byte (§5.7 uses it).
- **CPU budget in force (S094 P2 §21):**

  | Item | Cycles per block |
  |---|---:|
  | Basic, ×4 | 19,620 |
  | Advanced, ×2 | 21,803 |
  | Effect | 16,634 |
  | Shared mixer | 12,784 |
  | Slack | 500 |
  | **Total** | **152,004** (97 % of 156,705) |

  This plan changes it to Effect **16,970** and Advanced **21,635** (D19).
- **Device results pending.** The user is testing the current image:
  - S094 P3 T1–T10;
  - the P4 PWM sweeps and the F3 Tri level;
  - the worst Scene with the probe, including CrumpBit `grn` with the SVF
    restored (AS-13).

  The §5.7 arithmetic rests on `grn` at about 16,284 cycles with
  `effects_service()`. That is an estimate. If the device measures
  differently, recompute §5.7 before scheduling.

### H.3 Standing rules (user)

**Documents and commits**

- `MEMORY.md` and everything under `knowledge_files/` are never edited in
  the same turn as any other file. A plan or schedule contains no step that
  edits them. §11 lists what goes out of date, for the user to log.
- Commits belong to the user: do not commit, and do not suggest or prompt
  when to commit.
- Nothing needed goes in `build/`. Records go in
  `tools/dsp_test/baselines/`.
- Do not use `.claude` auto-memory. Project context lives in the root
  `MEMORY.md`; the agent working notes are
  `knowledge_files/volatile/memory/VOLATILE_MEMORY.md` (there is no
  `memory/MEMORY.md` in the tree).

**Resources**

- Any RAM or ITCM growth needs sign-off. D18 (+16 B handoff record) is
  approved.
- Logging allocations exist only when `DEV_MODE_LOGGING` is enabled.

**Device runs and reporting**

- Plan device runs as one flash per question. Do not schedule extra runs.
- Report resource figures as one net total against the current build.
  Never quote parts as totals.

**Code and style**

- Every code change carries an accurate comment block (What, Why, Inputs,
  Outputs, Callers/Affiliates), in CrumpBit's style, usable as
  documentation in place.
- Keep implementations targeted and direct. The user wants brevity and no
  mistakes.

### H.4 Code anchors (verified 2026-11-11)

| What | Where |
|---|---|
| Effect ops, registry entry, IO flags | `Core/DSP/Effects/EffectsManager.h` |
| Runtime union | `EffectsManager.c:52-63` |
| Registry rows | `EffectsManager.c:70-137` |
| Size asserts | `EffectsManager.c:140-154` |
| `effects_state_t` | `EffectsManager.c:171-184` |
| `effects_exportHandoff()` | `EffectsManager.c:1864` |
| `effects_switchRuntime()` | `EffectsManager.c:1885` |
| `effects_onShareChanged()` | `EffectsManager.c:1903` |
| `effects_activateScene()` | `EffectsManager.c:1939` |
| `effects_changeTypeScene()` (calls `effects_switchRuntime()` at `:2046`) | `EffectsManager.c:2017` |
| `effects_service()` | `EffectsManager.c:2074` |
| `effects_process()` | `EffectsManager.c:2194` |
| `effects_activeIoFlags()` | `EffectsManager.c:2210` |
| `fx_share_t`, `fxbuf_handoff_t` (180 B, at `0x200210e4`, SRAM1), the arena contract | `Core/DSP/Effects/FxBuffer.h:44-290` (`fxbuf_handoff_t` `:153-166`) |
| Handoff size assert | `Core/DSP/Effects/FxBuffer.c:56` |
| `fxbuf_voiceAcquire()` / `fxbuf_voiceRelease()` (**no caller** in the firmware or the host modules, §H.6) | `FxBuffer.c:291`, `:325` |
| Record and sequencer constants | `Core/DSP/Effects/EffectTypes.h` |
| CrumpBit loop and curve constants (`CRUMPBIT_DELAY_CURVE_MAX` 126,654 samples, `CRUMPBIT_LOOP_MAX_BYTES` 126,656) | `CrumpBitEffect.h:43-46` |
| Exported curve functions: `crumpBit_delaySamples()`, `crumpBit_shareMaxSamples()` | `CrumpBitEffect.h:303`, `:316` |
| The `dly` curve | `CrumpBitEffect.c:311-411` |
| `crumpBit_delayLimit(float max_samples, …)`, **static** | `CrumpBitEffect.c:478` |
| `crumpBit_seat()` | `CrumpBitEffect.c:636` |
| `crumpBit_init()` | `CrumpBitEffect.c:740` |
| `crumpBit_exportHandoff()` | `CrumpBitEffect.c:840` |
| `crumpBit_runPitch()` (the `pit` shifter law the kit reuses) | `CrumpBitEffect.c:1589` |
| `crumpBit_runReverb()` (the `rvb` FDN: four 8-bit lanes per word, line `d_k` = base_k × size, base 1,433–2,053, size 0.25–2.5) | `CrumpBitEffect.c:2570`; bases `:163-166` |
| `crumpBit_bufferChanged()` | `CrumpBitEffect.c:3080` |
| `crumpBit_effectiveMax()` | `CrumpBitEffect.c:≈3105` |
| `fx_active` | `Core/DSPAudio/mixer.c:1099` |
| FX bus convert, `effects_process()` and return | `mixer.c:1201-1255` |
| Stereo balance law | `mixer.c:1236-1247` |
| `modNode_setItpReserved()` call | `mixer.c:1030` |
| Probe sections | `mixer.c:926-940` (`MIXER_PROBE_SEC_FX` 5) |
| `modNode_interpClaim()` | `Core/DSPAudio/modulationNode.c:135-158` |
| Reserve recompute | `modulationNode.c:544-546` |
| `instrumentManager_itpFadeSlotAvailable()` | `Core/DSP/Instruments/InstrumentManager.c:2026-2037` |
| `OSC_WAVE_INTERP_MAX_ACTIVE` 2 | `config.h:961` (repository root) |
| `DEV_MODE_LOGGING` | `config.h:88` (repository root) |

### H.5 Tools

| Tool | Use |
|---|---|
| `tools/dsp_test/crumpbit_cpu.py` (cap 181) and the `crumpbit_cpu_check` / `crumpbit_elf_check` Makefile targets (`tools/dsp_test/Makefile:962`, `:992`) | Templates for `space_cpu.py` and `space_elf_check` |
| `break_cb*` targets (same Makefile) | Template for the break builds. Anchor every sed with `^…$$`: S094 P2 §23 found that `break_cb15` matched two sites. |
| `tools/dsp_test/elf_looppath.py` (`--cond-heads --in-range`), `tools/dsp_test/elf_blockpath.py`, `tools/alkaline/control_cost.py --elf` | Loop and per-block instruction counts |
| `tools/diagnostic_tools/link_budget.py <nm> <elf>` | Flash and ITCM |
| `tools/diagnostic_tools/decode_devlogs.py` | Decodes the probe (trace stage `'p'`): per-slot block-rate/render records and the mixer sections `ctl`, `fxsvc`, `setup`, `srr`, `adds`, `fx`, `comp`. Whole-block records are flags `(flags & 7) < 6`. |
| `make all && make img` (repo root) | Builds `build/LXRV2_lxr02.img` |

### H.6 Review against the tree (2026-11-11)

**Confirmed on the current ELF (`build/lxr02.elf`, 21:12):**

- `link_budget.py`: flash 691,100 / 753,664 B (62,564 B headroom); ITCM
  13,984 / 16,384 B; DTCM statics 4,396 B; FXBUF 126,656 B at `0x20001140`.
- `fxbuf_handoffRecord` is 180 B (`0xb4`) at `0x200210e4`.
- The asserts: `effects_runtime_t` 100 B, `effects_state_t` 80 B with the
  `reserved` byte, `fxbuf_handoff_t` 180 B.
- `effects_service()` loops over the type's `descriptor_count`, so its cost
  does scale with the row count (§1), and it calls `effective_max` for
  every BUFFER_DEPENDENT row every block.
- `modNode_interpClaim()`'s two grant paths and the reserve recompute are as
  §5.7 describes; `instrumentManager_itpFadeSlotAvailable()` tests
  `count < OSC_WAVE_INTERP_MAX_ACTIVE` (`:2036`).
- Both type-change routes reach `effects_switchRuntime()`, which exports the
  handoff, zeroes the union and calls the incoming `init`.
- The §0 and §4 times and capacities recompute from the arena figures.

**Corrections made in this revision:**

- §H.4: four line numbers; `config.h` is at the repository root.
- §H.3: the second project-context file path.
- §5.3: the Reverb's `seat_min` was 32 KB. That cannot hold the 256 B
  header and the 32 KB ring. It is now **40 KB**, which leaves the same
  0.18 s predelay as the minimum share.

**Findings that need a decision (§13):**

- **A1:** a ring's tail cannot be fed as §5.2/§5.4 describe. A tap reads
  coherent audio for only one line's length.
- **A2:** into OFF, the outgoing type could keep running (a drain) instead of
  a playout adapter.
- **A3:** nothing claims voice units today, so the voice-claim paths of the
  elastic seating have no trigger.

---

**Request:** three new Effect types beside CrumpBit, written compactly with
shared code. They should sound clean, where CrumpBit is the lo-fi one, and
use as much of the Effect CPU share as possible.

- **Delay:** true stereo, 16-bit, tape emulation, feedback above unity, and a
  tone control and a degradation/saturation stage inside the feedback loop.
- **Reverb:** a good-sounding reverb with shelving, freeze, and a
  pitch-shifted shimmer with interval and detune controls.
- **Echo:** a mix of the two: the Delay summed to mono, plus the Reverb
  without shimmer.

**User rules (revision 2):**

- **Every FX buffer user is elastic.** It uses as much of the Effect share as
  it can, up to the whole arena when no voice units are claimed.
- **A type change hands over in a musically responsible way.** The outgoing
  tail is never simply dropped. Revision 2 added §5 and brought CrumpBit
  under both rules. **Revision 3 (user):** §5 is now feed-through (C): the
  old tail is summed or converted and injected into the new effect's loop.
- **Revision 4 (user):**
  - D3: `tim` plays CrumpBit's `dly` curve and is truncated by the share the
    same way;
  - D14: a voice claim cuts the tape;
  - D18: approved;
  - D19: the feed is paid for by locking one interpolation slot, with the
    rest taken from Advanced (−168 each; §5.7);
  - every other recommendation accepted;
  - musical options added (§12).

---

## 0. Summary

| | Delay `dly` | Reverb `rvb` | Echo `ech` |
|---|---|---|---|
| IO | stereo in/out | stereo in/out (mono tank input) | stereo in/out (mono delay, stereo plate) |
| Engine | 2-channel varispeed tape, 16-bit interleaved | plate tank at half rate in a 16-bit ring, with 2-voice shimmer | 1-channel tape feeding the plate (no shimmer) |
| Share use | elastic: tape = share − 256 B | 64 KB ring (32 KB while seated small) + elastic predelay | 32 KB plate ring + elastic tape |
| `tim` | CrumpBit's `dly` curve (/64 … /2, then up to 2.87 s), truncated by the share | — | as Delay |
| Longest time, full arena / all 12 units claimed | 2.86 s / 1.66 s (CrumpBit: 2.87 s / 1.67 s) | predelay up to 1.4 s / 0.17 s | 2.87 s (whole curve) / 1.84 s |
| Estimated instructions per frame, with the §12 recommended options | ≈ 191 | ≈ 191 (one trim) | ≈ 196 |
| Feed-through adapter while a type change is feeding (§5) | + ≤ 18 | + ≤ 18 | + ≤ 18 |
| Engine cap (§1, §5.7) | ≈ 203 | ≈ 194 | ≈ 199 |
| Feed paid for by | one interpolation slot locked while the feed runs, plus Effect +336 from Advanced (§5.7) | same | same |

**Net totals against the current build, for the whole plan (three types,
the feed-through, and the CrumpBit changes):**

| | Change |
|---|---|
| Flash | **≈ +16 KB** (range 12–20 KB), against 62,564 B headroom |
| DTCM / ITCM | **0** |
| SRAM1 `.bss` | **+16 B**: the handoff record grows from 180 B to 196 B (§5.2). **Approved (D18).** |
| CPU budget (S094 P2 §21) | Effect 16,634 → **16,970**; Advanced 21,803 → **21,635** ×2; everything else unchanged |

---

## 1. Constraints from the current tree

**Registry and record**

- Ids are append-only: OFF 0, STEREO_FILTER 1, CRUMPBIT 2. The new types are
  **3 Delay, 4 Reverb, 5 Echo**, and `EFFECT_TYPE_COUNT` becomes 6.
- `effect_record_t` stores the type id, the normal and Morph images of the 64
  rows, and 16 steps. **The record format does not change.**
- Rows 0..2 are the common rows. Lane 0 is Morph, which leaves 15 lanes per
  type.

**IO**

- `effect_io_t` carries normalized float l/r, 32 frames per block.
- The mixer's bus conversion and return (`mixer.c:1201-1255`) sit inside the
  `fx` probe section.

**Arena** (from the ELF)

| Region | Bytes |
|---|---|
| DTCM | 131,072 |
| `.dtcm` + `.dtcmz` | 4,416 |
| FxBuffer arena | **126,656** |
| Twelve voice units, claimed from the top | 12 × 4,416 |
| Effect share with all twelve units claimed | **73,664** (the guaranteed minimum) |

- The share always starts at offset 0 and is contiguous.
- There is no system clear: an owner clears what it claims (A31/F6).
- **No code claims voice units today** (`fxbuf_voiceAcquire()` has no
  caller), so the share is always the whole arena, 126,656 B. The minimum
  share is a future case (§13 A3).

**Runtime union**

- `effects_runtime_t` is 100 B in DTCM, and its size is asserted
  (`EffectsManager.c:154`).
- This plan does not grow it (§3.3).

**Type switch today (`effects_switchRuntime()`, `EffectsManager.c:1885`)**

1. The outgoing type exports the handoff (`fxbuf_handoffBeginExit()`, then
   `ops->export_handoff`).
2. The union is zeroed.
3. The incoming `init(handoff)` runs.

What this means in practice:

- **CrumpBit never adopts.** Its comment says "re-entering CrumpBit never
  plays a previous owner's tail". It reinterprets the bytes muted under
  `valid`.
- **A type change is a hard cut of the old tail.** The return level ramps,
  but the content does not.
- **OFF has no ops and io 0.** `effects_activeIoFlags()` returns 0, so the
  mixer skips the Effect entirely (`mixer.c:1099`) and a tail into OFF is cut
  too.
- **A same-type Scene switch already keeps the runtime and its tail**
  (S074 gap 1). This plan leaves that unchanged.

**Tempo**

- `seq_getBpm()` is read once per block, as CrumpBit already does.

**CPU**

- The Effect share is 16,634 cycles per block today. That includes
  `effects_service()`, whose worst observed cost is 4,057.
- CrumpBit `grn` runs at 180.8 instructions per frame, about 350 cycles
  under the share. That is where the **cap of 181** comes from, calibrated at
  about 2.1 cycles per instruction.
- **The Effect share becomes 16,970** under D19 (§5.7). The caps below
  include the feed: 13 instructions per frame from the locked slot, 5 from
  the re-budget.
- **`effects_service()` scales with the type's row count.**
  - It costs 1,070 cycles with the Effect off and 4,057 with CrumpBit's 24
    rows. That is about 142 cycles per row: Morph interpolation, overlay,
    lock, LFO, clamp and compare for every row, every block.
  - The new types have fewer rows, so they get more engine budget than
    CrumpBit's 181. Each type's engine cap is (16,970 − its service cost)
    / 67.5 cycles per instruction-frame (32 frames × 2.11), less the 5 the
    feed adds beyond the locked slot:

    | Type | Rows | Estimated service | Engine cap |
    |---|---:|---:|---:|
    | Delay | 16 | ≈ 2,916 | ≈ 203 |
    | Reverb | 20 | ≈ 3,484 | ≈ 194 |
    | Echo | 18 | ≈ 3,200 | ≈ 199 |

  - These are estimates. The device run measures `fxsvc` per type.
- The new code is float-heavy and contains serial chains, so it may run at
  more cycles per instruction. The device run in §9 measures it.

---

## 2. Architecture

### 2.1 Files

**New folder `Core/DSP/Effects/Space/`** (D13, accepted):

| File | Contents |
|---|---|
| `SpaceKit.h/.c` | Shared static-inline DSP primitives, the two engines (tape and plate), **the seat/grow logic and the feed-through adapter**. The last two are used by CrumpBit too (§5.6). |
| `SpaceEffect.h/.c` | Three `effect_type_ops_t` sets that share init, write_param, buffer_changed, export_handoff and effective_max through a type tag. Three process functions. |
| `SpaceParameters.h/.c` | Three descriptor tables built from shared row definitions, the tokens/labels and `format_value3`. |
| `SpaceTables.h/.c` | Generated by `tools/plots/gen_space_tables.py`. |

### 2.2 Three registry types, one module

- Three rows, three tokens, and no subtypes, `select_layout` or lane variants.
- Code is shared at function level:
  - Delay and Echo share the tape (compiled for stereo and for mono).
  - Reverb and Echo share the plate (with and without shimmer).
  - All of them share the seat and feed-through code with CrumpBit.

### 2.3 Rates

- **Tape:** 44,108 Hz, with varispeed (§4.1).
- **Plate tank:** half rate, 22,054 Hz.
  - The input is summed to mono and decimated through a halfband filter.
  - The tank runs once per two frames.
  - The stereo taps are interpolated back up through the same halfband.
  - This halves both CPU and memory, and the tail is band-limited to about
    10 kHz.
- **Dry path:** full rate.

---

## 3. Shared kit (`SpaceKit`)

**Loop rules (the ELF gate in §8 checks them):**

- Every primitive is `static inline` and keeps its state in float.
- No division, libm call or double-precision operation in any per-frame loop.

### 3.1 Primitives

| Primitive | Law | Instructions | Used by |
|---|---|---|---|
| `sk_ring16p2` | Power-of-two int16 ring in the FV-1 style: one write pointer counts down, and every line is a fixed offset into it. Read `buf[(p + off) & mask]`. Write `VCVT.S16.F32 #15`, which saturates, so int16 never wraps. | ≈ 5 per access | plate tanks |
| `sk_ring16` | Elastic int16 ring of any length: compare-and-subtract wrap, same conversions as above. | ≈ 7 per access | tapes, predelay |
| `sk_readLin` / `sk_readHerm` | Fractional read, linear or 4-point Hermite. | ≈ 10 / ≈ 20 | tape, modulated allpass, shimmer |
| `sk_onePole` | `s += k (x − s)` | 2 | head LPs, bandwidth, damping |
| `sk_shelf2` | `lp = onePole(x)`; `y = g_lo·lp + g_hi·(x − lp)` | 5 | plate loop (decay per band) |
| `sk_svfTone` | TPT SVF driven by a bipolar `ton` row: LP below 63, flat at 63, HP above. Output `a·lp + b·bp + c·hp` with per-block coefficients from a table, so there is no branch. Q about 0.8, which gives a slight head bump. | ≈ 12 per channel | tape loops |
| `sk_softClip` | `1.5x − 0.5x³` applied to clamp(x, ±1) | 5 | tape loops |
| `sk_dcBlock` | One-pole HP at about 10 Hz | 3 | loops |
| `sk_lfo` | Per-block phase plus an LCG random walk, ramped across the block | ≈ 1 per frame | wow/flutter, tank modulation |
| `sk_hb` | 11-tap halfband decimator and interpolator | ≈ 6 down, ≈ 7 per channel up | plate |
| `sk_shifter` | Two-head rotating pitch shifter with a triangular crossfade (CrumpBit `pit`'s law, in float, with **interpolated** reads) | ≈ 26 per voice | Reverb shimmer |
| `sk_seat` / `sk_growAtWrap` | Seats a line or ring in a given span, and grows a tape seamlessly at its next wrap (§3.3) | per block | all buffer users |
| `sk_feed` | The feed-through adapter: reads the old tail in its own format, converts it (8-bit padded, stereo summed, mono doubled, old tape speed kept) and injects it into the loop (§5). It is also OFF's playout. | ≤ 18 per frame while a feed runs | all buffer users and OFF |
| `sk_clearStep` | Incremental clear of a newly seated span the type does not adopt | — | all |

### 3.2 Engines

- **`sk_tape`** (stereo or mono): record/playback heads, varispeed, wow and
  flutter, `ton`, `deg` and the clip, DC block (§4.1).
- **`sk_plate`** (with or without shimmer): predelay, input diffusers, a
  figure-8 tank with in-loop shelves, freeze, and taps (§4.2).

### 3.3 Elastic seating

Every buffer user (Delay, Reverb, Echo and CrumpBit) **seats** in a
contiguous **span** of the share. Outside a feed, the span is the whole
share. During a feed it is the largest contiguous piece outside the
feed window (§5.3).

**State header**

- 256 B at the bottom of the span.
- It holds the filter, LFO, shifter, halfband and feed state.
- When the seat moves down, the header moves with it (a 256 B copy of about
  70 cycles, done in the block that grows).
- The union member holds only the raw rows, the type tag, glide targets and
  the clear cursor, at most 32 B. **No RAM growth.**

**Per-type layout inside the span**

| Type | Layout |
|---|---|
| Delay | header + tape (L,R int16 frames, the rest of the span) |
| Echo | header + plate ring (16,384 int16 = 32 KB, power of two) + tape (mono int16, the rest) |
| Reverb | header + tank ring (32,768 int16 = 64 KB, or 16,384 = 32 KB while the span is under 96 KB) + predelay line (the rest, elastic) |

**Growth**

- **Tapes and predelay grow seamlessly at the next wrap (`sk_growAtWrap`).**
  - When the write head reaches the end of the old ring, it continues into
    the new span instead of wrapping.
  - The history stays in order behind the write head, and the new space is
    ahead of it.
  - That new space is read only once `valid` covers it, so nothing is
    muted.
  - The same rule works downward: wrap to the new base instead of the old
    base, and the history stays contiguous behind the head.
  - The delay's top time then rises with `valid`, so a long time that was
    clamped by a small seat glides out to its target like tape.
- **The plate ring changes size only through a "quiet reseat."**
  - The reverb grows from 32 KB to 64 KB only once the tank's energy has
    fallen below −80 dBFS (one tap magnitude checked per block), so nothing
    audible is lost.
  - Until then `siz` is clamped through `effective_max`.

**Shrink (a voice unit claimed while playing)**

- The claim is synchronous, so the cut-off audio cannot be moved first.
- **What happens:**
  - Tapes keep the newest audio when the write head is still inside the new
    span (CrumpBit's existing rule). Otherwise they reseat, and the wet
    fades back in over 10 ms.
  - The plate ring sits at the bottom of the span. Voice units are claimed
    from the top, and the minimum share holds a 64 KB ring, so a voice claim
    never cuts the ring. It only cuts the predelay, Echo's tape, or a
    feed window.
- This is the one case where audio can still be lost. See D14.

---

## 4. The types

### 4.1 Delay (`dly`, stereo tape)

**Tape**

- One stereo tape of N frames, where N = (span − 256)/4. A frame is an L,R
  int16 pair, so one LDR loads both channels.
- The write head is at P, which advances by v per frame (16.16 fixed
  point). The playback head reads at P − D.
- **T ≤ N/fs:** v = 1 and D = T·fs. A time change glides D, so the read
  head moves and the pitch bends.
- **T > N/fs:** D = N − guard and v = (N − guard)/(T·fs). The tape slows down
  and longer delays get darker.
- **Time law (D3, user): CrumpBit's `dly` curve, truncated the same way.**
  - `tim` rows are `crumpBit_delaySamples(row, bpm)`. The function is
    called, not copied:
    - rows 0–3 run geometrically from 2 samples to /64;
    - rows 4–123 are the tempo-synced StepScale stops from /64 to /2;
    - rows 124–127 run geometrically to 2.87 s.
  - `effective_max` (BUFFER_DEPENDENT) stops the knob at the first row
    that does not fit, by the same search as `crumpBit_delayLimit()`
    against this type's capacity.
- **The speed floor is 1/4,** which is CrumpBit's memory-to-time ratio: a
  16-bit stereo frame at quarter speed is 4 B per 4 output samples, 1 B per
  sample. The Delay therefore reaches 2.86 s with the full arena and 1.66 s
  with all units claimed (CrumpBit: 2.87 s and 1.67 s; the difference is
  the 256 B header).
- **`snc` is removed.** The curve is always tempo-synced, as CrumpBit's has
  been since S091 P6.
- **Both glides** are one-pole with a time constant of about 150 ms, the
  same as CrumpBit's tape glide.

**Write and read**

- Every frame writes to `floor(P)`, which needs no branch. The 2-pole record
  head LP (cutoff ≈ 0.42·v·fs) is the anti-alias filter.
- Playback is a 4-point Hermite read (D10) followed by a 1-pole playback LP.

**Loop, per channel**

```
rd   = playbackLP( read(P − D − wow) )
x    = rd·(1 − xfb) + rd_other·xfb          ; 0 true stereo … 127 ping-pong
x    = svfTone(x)                           ; mid-feedback tone
x    = softClip( drive·x + hiss ) · makeup  ; mid-feedback degradation
x    = dcBlock(x)
tape[P] = recordLP( in + fbk·x )            ; saturating 16-bit write
out  = dry·(1 − mix) + rd·mix               ; the wet is the clean read
```

**Rows and laws**

- **`fbk`:** 0..1.20, unity at row 106 (D4). Above unity the loop runs into
  the clip and stays bounded.
- **`deg`:** one macro control for drive (×1 to ×6), head bandwidth (down
  to −2 octaves) and hiss (−72 to −48 dBFS) (D5).
- **`wow`:** wow at about 0.6 Hz plus flutter at about 7 Hz plus a random
  walk, up to ±0.5 % of D. The L and R phases are offset.
- **`spr`:** the right channel's time as a musical ratio of the left
  (§12 O9). The right time is clamped to capacity like `tim`.

**Rows:** `mix tim spr fbk xfb ton deg wow hld brk rev hed dck`. The last four
are from §12.

**Lanes:** 1..15 are `mix tim fbk ton deg wow xfb spr brk rev hld hed dck
lvl pan`.

**Estimated cost (≈ 125 instructions per frame):**

| Part | Instructions |
|---|---|
| Read | 36 |
| Head LPs | 12 |
| Position and wow | 4 |
| `ton` | 24 |
| Clip | 15 |
| DC | 6 |
| Cross-feed | 4 |
| Write | 13 |
| Mix | 6 |
| Overhead | 5 |

**D10 extras (accepted):** tape compression (+8) and `hld` (+2). `hd2`
becomes the three-head `hed` (§12 O6).

The recommended §12 options add `brk`, `rev`, `hed` and `dck`, for a total
of **≈ 191** against the ≈ 203 cap.

### 4.2 Reverb (`rvb`, plate with shimmer)

**Tank**

- Dattorro figure-8:
  - predelay, then the bandwidth LP, then 4 input allpasses;
  - two halves, each: modulated allpass → delay → **2-band shelf** →
    ×decay → allpass → delay;
  - 6 taps per side.
- Everything lives in the power-of-two ring. The lengths are fitted by
  `gen_space_tables.py` at 22,054 Hz.

**Elastic use**

- The ring is 64 KB in any span of 96 KB or more, and 32 KB below that
  (`siz` clamped to about 0.5).
- The predelay line takes the rest of the span: up to 1.4 s with the full
  arena and 0.17 s at the minimum share. `pre` is clamped through
  `effective_max`.

**Controls**

- **Shelving (D7), in the loop:**
  - `lsh` sets the low decay multiplier, 0.5× to 2×;
  - `hsh` sets the high damping, 1× to 0.1×;
  - `lsf` and `hsf` set the corners, about 100–600 Hz and 1.5–9 kHz.
- **`siz` (D12):** 0.35× to 1×. The integer lengths glide by at most one
  sample per block, so the tail warbles while it grows. It is not a lane.
- **Freeze `frz`:**
  - On: the input ramps to 0, decay goes to 1.0, the shelves go flat and the
    shimmer feed goes to 0, all over 10 ms. The modulation keeps running.
  - Off: everything ramps back over 10 ms.
- **Shimmer:**
  - A 2,048-sample line, inside the ring, is fed from the tank's left tap
    through a 200 Hz HP.
  - Two `sk_shifter` voices read it at interval ± `dtn` cents.
    - `int` is in whole semitones, −24 to +24, default +12.
    - `dtn` is 0 to ±50 cents.
    - The window is 50 ms.
  - The sum × `shm` feeds the tank input, so every pass shifts again.
- **Other rows:**
  - `dcy`: RT60 from 0.3 s to 20 s;
  - `mod`: depth of the tank modulation;
  - `wid`: M/S width of the taps;
  - `dif`: the input allpass coefficient, 0.5 to 0.75.

**Rows:** `mix pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck`.
`env` and `dck` are from §12.

**Lanes:** 1..15 are `mix dcy frz shm int dtn env dck lsh hsh pre mod wid
lvl pan`.

**Estimated cost (≈ 183 instructions per frame):**

| Part | Instructions |
|---|---|
| Decimate | 6 |
| Predelay | 6 |
| Bandwidth LP | 1 |
| Input allpasses | 26 |
| Tank | 56 |
| Shimmer | 28 |
| Taps | 36 |
| Interpolate up | 14 |
| Width and mix | 6 |
| LFO and overhead | 4 |

**Trims, if needed:**

- 5 taps per side, −6;
- 3 input allpasses, −6;
- linear shifter reads, −10.

With the §12 options `env` and `dck` (+14) it is about 197 against the
≈ 194 cap. The first trim (5 taps per side) brings it to **≈ 191**.

### 4.3 Echo (`ech`, mono tape into the plate, no shimmer)

**Signal path (D11)**

- The input is summed to mono and goes into the mono `sk_tape`, with the
  same laws as the Delay.
- The echo feeds the plate, scaled by `e>r`. A little dry signal also feeds
  the plate, so the first hit has space.
- The plate is `sk_plate` without shimmer: 2 input allpasses, 4 taps per
  side, and a 32 KB ring at size 0.6. It has no predelay.
- Output = `dry + mix·( echo·(1 − bln) + plate·bln )`.

**Elastic use**

- The tape takes the span minus the header and the ring.
- `tim` uses the Delay's law, which is CrumpBit's curve with a 1/4
  floor.
  - With the full arena the tape holds 4.25 s, so all of the curve fits,
    up to 2.87 s.
  - With all units claimed it truncates at 1.84 s.

**Rows:** `mix tim fbk ton deg wow e>r bln dcy lsh hsh brk hed dck rtg`.
The last four are from §12.

**Lanes:** 1..15 are `mix tim fbk ton deg wow e>r bln dcy brk hed dck rtg lvl
pan`.

**Estimated cost (≈ 184 instructions per frame):**

| Part | Instructions |
|---|---|
| Mono tape | 60 |
| Decimate | 6 |
| Input allpasses | 13 |
| Tank | 56 |
| Taps | 24 |
| Interpolate up | 14 |
| Mix | 6 |
| Overhead | 5 |

- A linear tape read (−10) brings it to about 174.
- With the §12 options `brk`, `hed`, `dck` and `rtg` (+22), it is **≈ 196**
  against the ≈ 199 cap.

---

## 5. Type change: feed-through

**Decided (user, revision 3):**

- Handover is **C, feed-through**.
- **C1:** stereo is **summed** to mono.
- **C2:** the old tail is **injected into the new effect's loop**.

Revision 2's parallel playout of the old tail is replaced. It is kept only
for a switch into OFF (§5.5).

### 5.1 The rule

On any type change:

- The outgoing type's pending audio is read from the arena in its own format
  and converted.
- It is injected into the incoming type's loop, so it is **heard at its own
  due time and processed by the new effect**:
  - a new delay repeats it;
  - a new reverb washes it.
- The new effect starts at once.
- No tail is dropped in a hard cut.

**Covered:**

- every pairing of Delay, Reverb, Echo and CrumpBit (all five subtypes);
- both entry routes: a Scene switch (`effects_activateScene()`) and a menu
  type edit (`effects_changeType()`). Both go through
  `effects_switchRuntime()`.

**Not covered:**

- **Same-type Scene switches.** They already keep the runtime and its tail,
  and stay unchanged.
- **CrumpBit's own `sub` switch.** It keeps its user-encouraged glitch
  reinterpretation. It is not a type change.

The outgoing *effect* never runs again. Only the audio already in its buffer
is used. New input reaches only the new effect.

### 5.2 Handoff record: what the outgoing type exports

**What the record holds today:**

- share bounds;
- `effect_type`, `effect_channels`, `effect_bits` and `effect_rate_hz`;
- `state_flags` (`FXBUF_STATE_EFFECT_WRITTEN`);
- four Effect pointer slots, `read_offset`/`write_offset[12..15]`.

That gives no line bounds, no decay, no read pattern and no output gains.

**Additions (+16 B, SRAM1 `.bss`; the record lives at `0x200210e4`,
outside DTCM; approved, D18):**

| Field | Type | Meaning |
|---|---|---|
| `effect_line_offset` | u32 | arena offset of the outgoing line or ring |
| `effect_line_bytes` | u32 | its length |
| `effect_feed_decay_q15` | u16 | gain per 32-frame block: from RT60 for a ring; from `fbk` per pass for a tape (used only into OFF, §5.5) |
| `effect_tape_speed_q15` | u16 | the outgoing tape's speed v at exit (1.0 for CrumpBit and for rings), so the feed keeps the recorded pitch |
| `effect_dry_q15` | u16 | dry gain at exit |
| `effect_feed_shape` | u8 | `NONE`, `TAPE`, `RING16` or `RING8X4` |
| (pad) | u8 | |

**Pointer-slot meanings:**

- **`TAPE`:**
  - `write_offset[12]`: the write head;
  - `read_offset[12]`: the pending read head (L, or mono);
  - `read_offset[13]`: the R head, if `spr` makes it different;
  - bits 8 or 16; channels 1 or 2 (interleaved).
*(The two `RING*` shapes below are open: §13 A1.)*

- **`RING16`** (plate):
  - `write_offset[12]`: the ring pointer;
  - `read_offset[12]` and `[13]`: the L and R tap offsets;
  - rate 22,054.
- **`RING8X4`** (CrumpBit `rvb`): four 8-bit FDN lanes per word; the word
  pointer and lanes L and R.

**Per outgoing type:**

| Outgoing type | Export |
|---|---|
| Delay | `TAPE`, 16-bit, stereo |
| Echo | `TAPE`, 16-bit, mono: the echo's pending repeat. The plate is not exported, because one reader is enough (D15); the plate's tail is the part lost. |
| Reverb | `RING16`; decay from RT60, or 1.0 when frozen. The ceiling bounds the feed (§5.3). |
| CrumpBit `dly`, `cmb`, `pit` | `TAPE`, 8-bit, mono |
| CrumpBit `grn` | `TAPE` on its delay half |
| CrumpBit `rvb` | `RING8X4` |

### 5.3 What the incoming type does at `init`

1. **Read the handoff.** No feed (seat in the whole share as normal) when any
   of these holds:
   - `FXBUF_STATE_EFFECT_WRITTEN` is clear;
   - the shape is `NONE`;
   - the outgoing type is OFF.
2. **Feed window W** (the bytes the adapter will still read):
   - `TAPE`: from the pending read head forward to the write head. That is
     the next repeat, oldest byte first, and the same for the R head.
   - `RING*`: the whole old ring.
3. **Seat.**
   - The span is the largest contiguous piece of the share outside W, at
     least the type's `seat_min`:

     | Incoming type | `seat_min` |
     |---|---|
     | Delay | 32 KB |
     | Reverb | **40 KB** (header 256 B + 32 KB ring + 7,936 B predelay, 0.18 s; was 32 KB, which cannot hold the header and the ring, §H.6) |
     | Echo | 48 KB |
     | CrumpBit | 32 KB |

   - If no piece is large enough, W is cut:
     - `TAPE`: from its far end. The feed carries the start of the pending
       repeat and fades out before the cut.
     - `RING*`: the feed is shortened (step 4).
4. **Feed length.**
   - `TAPE`: one pass of the window, which is the outgoing time (or the cut
     window). The old feedback is not emulated; the new effect's feedback
     makes the repeats.
   - `RING*`: until the RT60 envelope reaches −60 dB, or until a ring read
     position would reach the incoming span (less 10 ms), whichever comes
     first. The ceiling is 6 s (D16), the envelope is steepened to land on
     time, and the minimum is 50 ms.
   - A cut tape window fades over its last 20 ms.
5. **Levels.**
   - The feed is the old effect's **wet-normalized** tail, taken before the
     old `mix`. It enters the new effect as the new effect's own wet signal,
     so it is heard at the **new** `mix`. If the new Scene has the wet off,
     the tail is not heard, and is not written into the new effect's loop
     either.
   - The incoming dry gain ramps from `effect_dry_q15` to its own value over
     10 ms, so the dry part of the return does not jump.
6. **Clearing.**
   - The incoming span is cleared incrementally (`sk_clearStep`, 4 KB per
     block).
   - Until the clear has passed the region its own reads need, the new
     effect's own wet stays muted. The feed is not muted: it comes from W,
     which is never cleared or written while the feed runs.
7. **When the feed ends**, the seat grows to the whole share:
   - tapes and predelay at their next wrap (§3.3);
   - the plate at its next quiet reseat.

### 5.4 The adapter (`sk_feed`, in the kit)

**Read and convert** (once per frame, ahead of the incoming engine):

| Old format | Conversion | Instructions |
|---|---|---|
| 8-bit | padded to 16: code × 256, in float code/128 | ≈ 3 |
| 16-bit | read as is: code/32,768 | ≈ 3 (≈ 4 for an interleaved pair) |
| Stereo into a mono effect (Echo, CrumpBit) | **summed**: (L + R)·½, with the ½ folded into the feed gain | +1 over dropping a channel |
| Mono into the stereo Delay or Reverb | the same signal fed to L and R | 0 |
| Old varispeed tape | the read position advances by the exported v per frame, linear read, so the recorded pitch holds | |
| `RING*` | the ring pointer keeps rotating at the old rate; the L and R taps are read at their exported offsets; nothing recirculates; the RT60 envelope applies. **Open (§13 A1): this reads coherent audio for one line's length only.** | |

**Inject (C2)**, one or two adds per channel:

| Incoming | Where the feed goes |
|---|---|
| **Delay, Echo's tape** | Added to the playback-head signal: `rd' = rd + feed`. It is heard in the wet, then goes through cross-feed, `ton`, `deg` and the clip, and is written back at `fbk`. The new tape repeats it with its own colour. |
| **Reverb, Echo's plate** | Added to the wet output, and into the tank input **after the predelay**. It is heard on time and washed by the new tank. Into Echo, the feed enters the tape, and `e>r` carries it on into the plate. |
| **CrumpBit `dly`, `cmb`, `pit`** | Added to the head/read signal ahead of the 8-bit write. CrumpBit's quantizer does the **16-to-8 truncation**. |
| **CrumpBit `grn`** | Into the record line input and the wet. |
| **CrumpBit `rvb`** | Into the FDN input and the wet. |

**Cost:** about 18 instructions per frame or fewer, while a feed runs: about
16 for the read, conversion and envelope, plus about 2 for the injection.

**A second switch during a feed:** the newer type's handoff describes only its
own line. Its loop already holds the converted first tail, so that material
carries on into the third type through the second feed. Only the
not-yet-fed part of the older window is cut (D17).

### 5.5 Into and out of OFF

*(Open: §13 A2 proposes a drain of the outgoing type instead.)*

- **Into OFF.** There is no effect to feed, so OFF gets an ops row with a
  playout process only. The tail plays straight out, wet-normalized, at the
  outgoing `effect_dry`/wet balance, and fades:
  - a `TAPE` replays its window under `effect_feed_decay_q15`, which is
    capped at 0.9 per pass so a self-oscillating delay still ends;
  - a `RING*` plays out under its envelope;
  - the ceiling is 6 s.

  `effects_activeIoFlags()` reports STEREO_IN|STEREO_OUT while the playout is
  live and 0 afterwards, so the mixer keeps the return path until the tail
  ends.
- **Out of OFF.** There is nothing to feed.

### 5.6 CrumpBit changes (both rules)

**Already in place:** CrumpBit is elastic up to the full arena, and it keeps
its tail on a shrink when its write position fits.

**Changes:**

1. **`crumpBit_exportHandoff()`:** fill the §5.2 fields for every subtype.
2. **`crumpBit_init()`:**
   - read the handoff and seat in the §5.3 span: the loop takes a span base
     offset instead of assuming share offset 0, so `b->base` becomes
     `share->base + span_offset`;
   - run `sk_feed` and inject per §5.4.
3. **`crumpBit_seat()` on growth:** use `sk_growAtWrap` instead of cutting
   `valid` to `write_pos`. A voice release then no longer mutes CrumpBit's
   older history.
4. **Export `crumpBit_delayLimit()`** (static today, `CrumpBitEffect.c:478`;
   it already takes `max_samples`). Declare it in `CrumpBitEffect.h` so the
   Space types share the knob-limit search (D3) instead of copying it. This
   is a declaration-only change for CrumpBit.
5. **CPU:** §5.7. At 180.8 against 181, `grn` has no room of its own. The
   slot lock pays for the feed.

### 5.7 Paying for the adapter: one locked slot plus an Advanced re-budget (D19)

**What it costs:** about 18 instructions per frame, about 1,215 cycles per
block, only while a feed runs. The budget is set by the worst block.

**Where it matters:** CrumpBit `grn`. It has about 350 cycles spare today,
so a feed would put it about 865 over.

**Decision (user): lock one of the two interpolation slots while a feed
runs, and take the rest from the future Advanced budget, split between the
two instances.**

**What a slot is:** `OSC_WAVE_INTERP_MAX_ACTIVE` gives two per block,
shared by wavetable frame blends (`wtb`) and itp crossfades
(`modNode_interpClaim()`, `instrumentManager_itpFadeSlotAvailable()`).

**What a slot frees:**

- A held slot costs the Alkaline about 13 instructions per sample: the
  blend loop is 234 and the one-row loop 221 (S094 P4). That is about
  **880 cycles per block**.
- The voice budgets already include slot use: Basic 19,620 was set from the
  Alkaline's `wtb` blend worst case. So a locked slot frees cycles the
  Effect may use in the same block.
- This is an estimate from the Alkaline. The other types' blends are not
  measured.

**The re-budget:**

- The rest of the feed is 1,215 − 880 = **336 cycles**, rounded to an even
  split.

| Line | Today (S094 P2 §21) | Proposed |
|---|---:|---:|
| Basic ×4 | 19,620 | 19,620 |
| Advanced ×2 | 21,803 | **21,635** (−168 each) |
| Effect | 16,634 | **16,970** (+336) |
| Shared mixer | 12,784 | 12,784 |
| Slack | 500 | 500 |
| **Total** | 152,004 (97 %) | 152,004 (97 %) |

- In a feed block, CrumpBit `grn` uses ≈ 16,284 + 1,215 = 17,499, against
  16,970 + 880 = 17,850 available. That leaves **≈ 351 spare**, the same
  margin as today.
- The new types' caps in §1 are recomputed on 16,970. A feed block must
  fit within cap + 13 (one slot) − 18 (the feed), so the engine caps are
  unchanged at ≈ 203 / 194 / 199.

**How the lock works:**

- **When it applies:**
  - `init` sets it when a feed starts, and the feed's last block clears it.
  - `init` runs between blocks, so the next block's voices already see it.
  - It lasts as long as the feed: up to 2.87 s for a tape (one pass), up to
    6 s for a ring.
  - Into OFF there is no lock, because the playout has the whole share.
- **What the voices do while it is set:**
  - **One slot stays available.** The usual claim order applies to it:
    ongoing itp first, then `wtb`, then new itp.
  - A second claimant is refused. A `wtb` blend steps to the nearest frame,
    and a new itp falls back to `fre`. These are the existing fallback
    paths.
  - An itp fade already running in the locked slot keeps it until it ends
    (at most 132 samples, about 4 blocks).
- **Mechanism (checked against the code, 2026-11-11):**
  - **Adding the lock to the block-start reserve
    (`modNode_setItpReserved()`) is not enough.**
    - `modNode_interpClaim()` (`modulationNode.c:135-158`) has two grant
      paths: a sticky one for last block's holders (it consumes
      `modNode_interpReserved`) and a newcomer one (`active + reserved <
      MAX`).
    - The reserve is recomputed as last block's grants plus the itp
      reserve (`:544-546`). So two holders from last block would still
      keep both slots.
  - **Both paths compare `modNode_waveInterpActiveCount` against a limit
    of `OSC_WAVE_INTERP_MAX_ACTIVE − effects_feedSlotLock()`:**
    - the sticky path grants only while `active < limit`;
    - the newcomer path tests `active + reserved < limit`.
  - **`instrumentManager_itpFadeSlotAvailable()`**
    (`InstrumentManager.c:2036`) returns `count < OSC_WAVE_INTERP_MAX_ACTIVE
    − effects_feedSlotLock()`.
  - **`effects_feedSlotLock()`** returns 0 or 1 from the spare
    `effects_state.reserved` byte. The Space kit sets it at `init` when a
    feed starts and clears it on the feed's last block.
- **RAM: 0 B.** The lock lives in the existing spare byte of the asserted
  80 B `effects_state_t`. Update that struct's comment, which today calls
  the byte spare.

**Residual risk:**

- For up to 4 blocks after a switch, an itp fade already running in the
  locked slot can overlap the feed. The worst-block overshoot is at most
  about 880 cycles, inside the 3 % (4,701) above the 97 % cap. That is not
  an overrun.
- If the device run shows a slot frees less than 880, the difference comes
  from Advanced by the same rule, split evenly.

**What is heard:** for the length of a feed, only one voice at a time can
blend a wavetable or crossfade a waveform. Others step or snap. This
affects only `wtb` and itp voices, and only in the seconds after an Effect
type change.

**Not chosen:**

- **M4** (S094 P2: run `effects_service()` only on change, −2,000 to
  −3,000):
  - `effects_service()` is a full rescan by design, so its many writers
    (menu, MIDI, sequencer, overlays, LFOs, Morph, Scene loads, share
    changes) need not notify it (its own comment says so).
  - M4 would need every one of them to flag changes, and a missed one is a
    row that silently stops updating.
  - It stays "to investigate".
  - A low-risk M4-lite (half the rows on alternate blocks, about −1,500
    for CrumpBit) remains available later.
- **Locking both slots** (about 1,760 freed, no re-budget): not chosen. It
  removes all blending during a feed.

---

## 6. Integration steps (when approved)

1. **`FxBuffer.h/.c`:**
   - add the §5.2 fields to `fxbuf_handoff_t`;
   - `fxbuf_handoffBeginExit()` resets them to `NONE`/0;
   - document the per-shape meaning of `read/write_offset[12..13]`.
2. **`EffectTypes.h`:** ids 3/4/5, `EFFECT_TYPE_COUNT` 6.
3. **`EffectsManager.c`:**
   - three registry rows (stereo/stereo, §4 lanes, min/pref
     `buffer_*_bytes` = seat_min/arena);
   - the `SpaceRuntime` union member (≤ 32 B) plus its assert; the union
     stays at 100 B;
   - OFF's playout-only ops (§5.5);
   - `effects_activeIoFlags()` reports the playout while it is live.
4. **`Core/DSP/Effects/Space/*`:** per §2.1. Every change carries a
   What/Why/Inputs/Outputs/Callers comment block in the CrumpBit style.
5. **CrumpBit:** §5.6, items 1 to 4.
6. **Slot lock (§5.7):**
   - `effects_feedSlotLock()` (0 or 1), stored in `effects_state.reserved`;
   - `modNode_interpClaim()`: both grant paths use the lowered limit;
   - `instrumentManager_itpFadeSlotAvailable()`: the lowered limit.
7. **`gen_space_tables.py`:**
   - (time: none needed; `tim` calls `crumpBit_delaySamples()`);
   - `dcy` to gain per size;
   - shelf and SVF rows;
   - the tank layout for both ring sizes;
   - halfband taps;
   - fbk/RT60 to `feed_decay_q15`.
8. **UI:** `format_value3` shows `tim` as CrumpBit's `dly` labels do,
   semitones for `int`, cents for `dtn`, and named states for `frz`, `hld`,
   `rev`, `hed`, `env` and `rtg`. No layout change is expected; confirm while implementing.
9. **Makefile:** host builds and the gates (§8).

---

## 7. Phasing

| Phase | Content | Gate |
|---|---|---|
| F1 | Kit (seat, grow, feed) + Delay + handoff fields + OFF playout + slot lock | D-1..D-6, S-1..S-4, H-1..H-8 (Delay and OFF), L-1, L-2, O-1..O-4 and O-8, CPU, ELF, break builds |
| F2 | Reverb | R-1..R-6, O-4..O-6, H-* for `RING16` |
| F3 | Echo | E-1..E-3, O-1, O-3, O-4, O-7 |
| F4 | CrumpBit §5.6 | the existing CrumpBit suite unchanged, H-* with CrumpBit as both outgoing and incoming, `crumpbit_cpu.py` with the feed |
| F5 | Compressor adjustments (user scope, §H.1) | Shared mixer budget 12,784 holds |
| Device | **one run** (§9) | `fx` + `fxsvc` ≤ 16,970 per type, plus the slot in feed blocks |

---

## 8. Host tests and gates (`tools/dsp_test/modules/space.c`)

### Delay

- **D-1 Time:** the impulse lands at T ± 1 sample, and at the right sync
  division at 60, 120 and 180 BPM. Clamps follow the span.
- **D-2 Varispeed:** a 0.5 → 1.0 s step glides the pitch monotonically and
  settles within 1 %. No step exceeds 4× the steady state.
- **D-3 Feedback above unity:** `fbk` 127 for 10 s. Peak ≤ 1.0, no NaN or
  Inf, no int16 wrap.
- **D-4 True stereo:** with input on L only and `xfb` 0, R stays at or below
  −90 dB. With `xfb` 127 the repeats alternate.
- **D-5 Tone:** `ton` 0, 63 and 127 give LP, flat and HP.
- **D-6 Degradation:** at `deg` 127 the 3rd harmonic rises by at least
  20 dB, and the hiss sits at about −48 dBFS.

### Reverb

- **R-1 Decay:** RT60 within ±15 % of the table.
- **R-2 Shelves:** the RT60 split follows `lsh` and `hsh`.
- **R-3 Freeze:** holds within ±1 dB over 30 s, and the input is ignored.
- **R-4 Shimmer:** `int` +12 gives 2× ±1 %. `dtn` 25 gives two peaks
  ±25 ± 3 cents.
- **R-5 Stability:** 60 s at the extremes. No NaN or Inf, and the output
  stays bounded.
- **R-6 Halfband:** ≤ 0.1 dB ripple to 9 kHz, ≥ 50 dB image rejection.

### Echo

- **E-1:** timing as D-1.
- **E-2:** `bln` 0 equals a mono Delay. `bln` 127 equals the plate alone.
- **E-3:** stability as R-5.

### Elastic seating (every buffer user)

- **S-1:** seated in the full arena, the longest time matches §0. With 12
  units claimed, it matches the minimum-share figure.
- **S-2 Growth at the wrap:**
  1. Release units while a delay rings.
  2. Expect no mute and no step above 4× steady state.
  3. Expect `tim` to glide to a target it could not reach before.
- **S-3 Shrink:** with the write head inside the new span the tail is kept.
  Outside it, the wet comes back with a 10 ms fade and no NaN.
- **S-4 Quiet reseat:** the plate grows only once the tank is below −80 dBFS.

### Feed-through

| Test | Check |
|---|---|
| **H-1 Coverage** | Every ordered pair of {Delay, Reverb, Echo, CrumpBit × 5 subtypes, OFF}. After an impulse and a switch, the old tail appears in the output within its due time (correlation ≥ 0.7 against the converted reference over the first pass). **No hard cut.** |
| **H-2 Processed** | The fed tail is processed by the new effect. Into a delay, it recurs at the new D, scaled by the new `fbk`. Into a reverb, it shows the new RT60. |
| **H-3 Conversions** | 8-bit code c reads as c/128 exactly. Stereo into mono gives (L + R)/2. Mono into stereo gives identical L and R. Into CrumpBit, the fed audio leaves 8-bit codes. An outgoing tape at v = 0.5 keeps its recorded pitch (±1 %). |
| **H-4 Continuity** | Delay into Delay at equal `mix`: the first fed sample continues the last wet sample (the step stays ≤ 4× the steady-state delta). |
| **H-5 Integrity** | The incoming type never writes inside W while the feed is live: a canary written into W from the host side survives the feed. |
| **H-6 Tight space** | At the minimum share with an outgoing 64 KB ring, the feed shortens (≥ 50 ms) and nothing faults. |
| **H-7 Into OFF** | The tail plays out through OFF's playout. The io flags drop to 0 when it ends. A tape with feedback above unity still ends. |
| **H-8 Levels** | With the new `mix` at 0, the tail is neither heard nor written into the new loop. The dry ramps from `effect_dry_q15` with no step. |

### Slot lock and time law

- **L-1:** with a feed live, at most one slot is granted per block, and
  `instrumentManager_itpFadeSlotAvailable()` allows one fade. A running itp
  fade completes. The lock clears on the feed's last block.
- **L-2:** `tim` rows equal `crumpBit_delaySamples()` exactly. The knob
  limit equals the first row over capacity at 60, 120 and 180 BPM, at the
  full and minimum shares.

### Every type

- After CrumpBit garbage in the arena, the wet stays muted until the clear
  is done. No stale audio plays except the deliberate feed.

### Gates

- **`space_cpu.py`:** same counting method as `crumpbit_cpu.py`. Per-type
  engine caps from §1 (≈ 203 / 194 / 199), with the feed path counted
  separately (≤ 18).
- **`space_elf_check`:** no `__aeabi_*`, divide, double or libm symbol in
  the process, feed or playout loops.
- **Break builds:** one per gate, in the Makefile style.

---

## 9. Device measurement (one run)

1. Flash once.
2. Use the worst scene, with all units claimed for part of the run and
   released for the rest.
3. Cycle the Effect, about 10 s per type:
   1. Delay (`hed`, `rev`, `fbk` high);
   2. Reverb (`shm`, `dtn`, `frz`);
   3. Echo;
   4. CrumpBit `grn`;
   5. OFF.
4. Each change is made while a tail is ringing, so every switch is also a
   feed block.

**What the probe gives:**

- `fx` and `fxsvc` per type, both steady and in feed blocks;
- the voice sections with the slots locked, which measures what the lock
  frees;
- the real cycles per instruction for this code.

**Pass:** `fx` + `fxsvc` ≤ 16,970 for every type in steady blocks, worst block. In feed blocks it may also use what the locked slot freed. If a type is
over, the §4 trims are applied by rule. If the slot frees less than 880, the
difference comes from Advanced, split evenly (§5.7). No second run is
scheduled.

---

## 10. Decisions

Settled by the user in revisions 2 to 4:

| # | Decision |
|---|---|
| D1 | Tokens `dly`/`rvb`/`ech`; abbrevs `Delay`/`Revrb`/`Echo`; full names `Delay`/`Reverb`/`Echo` (accepted) |
| D2 | Every buffer user is elastic |
| D3 | `tim` uses CrumpBit's `dly` curve and is truncated by the share the same way; speed floor 1/4 (CrumpBit's memory-to-time ratio); `snc` removed |
| D4 | `fbk` 0..1.20, unity at row 106 (accepted) |
| D5 | `deg` is a macro (drive, bandwidth, hiss), `wow` is separate, and the wet is the clean read (accepted) |
| D6 | The plate runs at half rate (accepted) |
| D7 | The shelves are in the loop (accepted) |
| D8 | Shimmer has 2 voices (±`dtn`) and is muted in freeze, except in the §12 O12 mode (accepted) |
| D9 | State lives in a 256 B header that moves with the seat (accepted) |
| D10 | Tape compression and `hld` are in; `hd2` becomes `hed` (§12) (accepted) |
| D11 | Echo feeds the plate (`e>r`, `bln`), and §12 O16 adds the other routings (accepted) |
| D12 | `siz` glides (accepted) |
| D13 | Folder `Space/`, prefixes `sk_` and `space_` (accepted) |
| D14 | A new instrument claim cuts and truncates the tape |
| D15 | Echo exports its tape only for the feed (accepted) |
| D16 | Ring feed and OFF playout are capped at 6 s (accepted) |
| D17 | A second switch mid-feed cuts only the part of the old tail not yet fed in (accepted) |
| D18 | The handoff record grows by +16 B in SRAM1 `.bss` (approved) |
| D19 | One interpolation slot is locked while a feed runs, and the rest of the feed's cost comes from Advanced: −168 each, Effect 16,970 (§5.7) |
| D20 | Handover is C, feed-through |
| D21 | Stereo is summed to mono |
| D22 | The feed is injected into the loop |

**Open:** D23, which §12 options to take, and §13 A1–A3.

- The recommended set (**R**) is marked there, and the §4 estimates assume
  it.
- The next session asks the user before scheduling. If the user changes
  the set, recompute the rows, lanes, service estimates (§1) and totals
  (§12.5).

---

## 11. Statements that become out of date when this lands

- **`EFFECT_TYPE_COUNT`** 3, and the registry comments in `EffectsManager.c`
  and `EffectTypes.h`.
- **`FxBuffer.h`:**
  - the handoff contract text, which says nobody adopts the Effect
    pointers and lists only today's fields;
  - the meaning of pointer slots 12..15.
- **`CrumpBitEffect.c`:**
  - `crumpBit_init()`: "the handoff is not adopted in v1: re-entering
    CrumpBit never plays a previous owner's tail";
  - `crumpBit_exportHandoff()`: "nobody adopts the Effect pointers today";
  - `crumpBit_seat()`'s description of growth, which cuts `valid`.
- **`EffectsManager.c`:** `effects_activeIoFlags()`, and the OFF registry
  row, which has no ops.
- **The slot comments:**
  - `modulationNode.h` (`modNode_interpClaim()`, `modNode_setItpReserved()`);
  - `InstrumentManager.h` (`instrumentManager_itpFadeSlotAvailable()`);
  - `config.h` `OSC_WAVE_INTERP_MAX_ACTIVE`.

  Each says two slots per block. While a feed runs there is one.
- **`STORAGE_SRAM_MANIFEST.md`:** the handoff record size, and the per-type
  use of the share.
- **`S094_P2_ALKALINE_SAVINGS.md` §21:**
  - the budget lines: Effect 16,634 → 16,970, and Advanced 21,803 → 21,635;
  - the Effect line naming CrumpBit `grn` as the worst case.
- `EffectsManager.c` `effects_state_t` comment: the `reserved` byte is no
  longer spare (§5.7).
- `CrumpBitEffect.h`: `crumpBit_delayLimit()` is now exported (§5.6).
- Any user docs or menu help that list the Effect types.

---

## 12. Musical options (D23)

Each option is costed in instructions per frame. **R** marks the
recommended set, which fits the §1 caps. The §4 totals assume it.

All of them are built from the kit. None needs a new engine, and each is a
row that can be a sequencer lane and is morphable.

### 12.1 Shared

| # | Option | What it does | Cost | Types | R |
|---|---|---|---:|---|:---:|
| O1 | **Ducking `dck`** | An envelope follower on the Effect's input (2 ms attack, 200 ms release) pulls the wet down while you play, so repeats and tails bloom in the gaps. | 6 | all | **R** |
| O2 | Tempo-locked wobble | `wow`'s slow component and the plate's `mod` lock to the bar (1, 2 or 4 bars per cycle), so the modulation breathes with the pattern. It is a law, not a row. | 0 | all | optional |

### 12.2 Tape (Delay, Echo)

| # | Option | What it does | Cost | R |
|---|---|---|---:|:---:|
| O3 | **Tape brake `brk`** | Row 0: the tape runs. Above 0: the tape stops over 50 ms–2 s, so the pitch dives and the repeats freeze. Back to 0, it spins up over the same time. Write and read both stop, the wet gain follows √v (no thump), and the feedback holds. Lock it on a sequencer step for tape-stop drops. | 2 | **R** |
| O4 | **Reverse `rev`** (Delay) | The playback head reads backwards through the last D in two crossfaded windows (`sk_shifter` at ratio −1). With feedback, each repeat flips direction again. | 24 | **R** |
| O5 | Pitch in the loop `pit` | `sk_shifter` in the feedback path, ±12 semitones per repeat. Repeats climb or fall (crystal delay); into Echo, the rising repeats then wash through the plate. | 26 mono, 40 stereo | optional (Echo: replaces `hed`) |
| O6 | **Multi-head `hed`** | Three playback heads at D/3, 2D/3 and D, equally spaced like a three-head tape echo. The row has seven stops, the head combinations, and the feedback takes the summed heads, so patterns gallop and build. The extra heads use linear reads. | 24 stereo, 12 mono | **R** |
| O7 | Smear `smr` | Two kit allpasses per channel in the feedback path, so each repeat diffuses further toward a wash. | 52 stereo, 26 mono | optional (over the cap with R) |
| O8 | Tape compression | Level-dependent HF loss: louder repeats dull, like tape saturation (from D10). | 8 | **R** (in §4) |
| O9 | **Musical `spr`** (Delay) | The right channel's time is a ratio of the left, with stops at 1/2, 2/3, 3/4, 1, 4/3, 3/2 and 2 and log glides between them. Straight against dotted or triplet ping-pong stays on the grid. | 0 | **R** |

### 12.3 Reverb

| # | Option | What it does | Cost | R |
|---|---|---|---:|:---:|
| O10 | **`env`: gate and bloom** | A bipolar row. Above 63, a gate: the tail cuts 30–600 ms after each transient (80s gated drums). Below 63, a bloom: the wet swells in over 20–800 ms after each transient (a reverse-reverb feel). 63 is off. One follower (fast against slow) serves both. | 8 | **R** |
| O11 | Ducking | O1 | 6 | **R** |
| O12 | **Freeze plus shimmer** | `frz` gets a third state: freeze with the shimmer on. The loop gain is held at 1 by taking the shimmer's share from the decay, so a frozen chord keeps rising an octave at a time into a pad. Stability test R-7. | 0 | **R** |
| O13 | Lock `siz` to the tempo | `siz` stops at lengths where the tank's longest loop equals a note division. The tail's flutter-echo then sits on the beat. | 0 | optional |

### 12.4 Echo

| # | Option | What it does | Cost | R |
|---|---|---|---:|:---:|
| O14 | Ducking | O1 | 6 | **R** |
| O15 | Brake and heads | O3 and O6 | 2 + 12 | **R** |
| O16 | **Routing `rtg`** | Three modes: 0, echo into plate (blend, as §4.3); 1, plate into echo, so each repeat is a room; 2, plate inside the echo loop, so each repeat is more diffuse than the last (a spring in the feedback). `e>r` sets the amount in each mode. The engines run anyway, so only the summing points move. | 2 | **R** |

### 12.5 Totals with R (estimates)

| Type | Base | + R options | Total | Engine cap |
|---|---:|---|---:|---:|
| Delay | 125 | tape compression 8, `hld` 2, `brk` 2, `rev` 24, `hed` 24, `dck` 6, `spr` 0 | **191** | ≈ 203 |
| Reverb | 183 | `env` 8, `dck` 6, freeze+shimmer 0, then the first trim −6 | **191** | ≈ 194 |
| Echo | 184 | linear tape read −10, `brk` 2, `hed` 12, `dck` 6, `rtg` 2 | **196** | ≈ 199 |

**Tests for the options** (added to §8):

| Test | Check |
|---|---|
| O-1 | `brk` drops the pitch monotonically to 0 within its time and spins back up; no NaN, no step over 4× the steady state. |
| O-2 | `rev` output correlates (≥ 0.9) with the reversed window. |
| O-3 | `hed` taps land at D/3, 2D/3 and D ± 1 sample, for every stop. |
| O-4 | `dck` 127 pulls the wet down at least 12 dB under a −12 dBFS input, and it recovers within 200 ms ± 20 %. |
| O-5 | `env` gate closes within ±10 % of its time; bloom rises within ±10 %. |
| O-6 (R-7) | Freeze plus shimmer stays bounded for 60 s, with no NaN. |
| O-7 | `rtg` modes give distinct impulse responses: mode 1's first repeat is already diffuse, and mode 2's diffusion grows with each repeat. |
| O-8 | `spr` stops give exact time ratios. |

---

## 13. Open architectural questions (S095 review, 2026-11-11)

These were found by checking the plan against the tree (§H.6). Each one
changes code structure or what is heard, so it is settled before the
schedule is written. The recommendation is marked **(rec.)**.

### A1. A ring's tail cannot be fed as §5.2/§5.4 describe

**Finding.**

- In a ring, every line is a fixed offset from the one write pointer. With
  the pointer rotating and nothing written, a tap plays its own line's
  stored samples, oldest first. That is the correct continuation, **but only
  for that line's length.**
- After that, the tap reads other memory:
  - **plate (`RING16`):** the neighbouring line. That is a click and a jump in
    time at every line boundary, and after one rotation of the ring (1.49 s
    at 64 KB) the same content repeats;
  - **CrumpBit `rvb` (`RING8X4`):** each lane is a ring of the whole share,
    and only its last `d_k` words are in the line. Past `d_k`, the tap reads
    the lane's **older, louder** history.
- **Coherent length per tap:**
  - plate: one tank delay, about 0.05–0.15 s at Dattorro's proportions over
    `siz` 0.35–1 (estimate; the fitted tables decide);
  - CrumpBit `rvb`: the longest line is `d_3` = 2,053 × size (0.25–2.5),
    which is 513–5,133 samples, **12–116 ms**.
- So the plan's ring feed (to −60 dB, up to 6 s, one tap per side) plays
  0.01–0.15 s of real tail and then unrelated material.

**Options.**

| | What | Feed cost | What is heard |
|---|---|---|---|
| **A1-a (rec.)** | One pass of one coherent line per side, oldest sample first, faded over its last 20 ms: the plate's two longest tank delays (one per half), CrumpBit `rvb`'s two longest lanes. W is those lines only (plate ≤ about 13 KB; CrumpBit `rvb` ≤ 20.5 KB), so the incoming type seats in nearly the whole share | ≤ 18 (unchanged) | Into Reverb or Echo: the new tank washes the slice into a full tail, close to seamless. Into Delay or CrumpBit: the repeats carry a short slice of the old tail; the rest of the old tail is lost |
| A1-b | As A1-a, but each line loops with a crossfade under the RT60 envelope, up to 6 s | about +6 (a second read and the crossfade; re-cost against §5.7) | A full-length tail, with the loop period audible: a flutter on the plate, a 9–83 Hz buzz on CrumpBit `rvb` |

**What A1-a changes in this plan:**

- §5.2: `RING16`/`RING8X4` export a line's bounds (`effect_line_offset`,
  `effect_line_bytes`) per side instead of a tap offset. The feed reads them
  like a `TAPE` window. `effect_feed_decay_q15` is then used only for the
  OFF playout (and not at all under A2-a).
- §5.3 step 4: a ring's feed length becomes one line pass.
- §8 H-6 ("the minimum share with an outgoing 64 KB ring") no longer has a
  tight case. It becomes "an outgoing tape at the longest time, full share:
  W is cut, and the feed fades before the cut".

### A2. Into OFF: drain the outgoing type, or the playout adapter

**Finding.**

- §5.5 gives OFF a playout. That replays a `TAPE` window as a loop with
  decay. It would replay a `RING` with A1's problem.
- Into OFF there is no incoming effect, so the whole Effect share is free.
  The outgoing type's own process already fits that share.

**Options.**

| | What | Cost | What is heard |
|---|---|---|---|
| **A2-a (rec.)** | **Drain:** the outgoing type keeps running with its input muted, under a manager envelope that ends it at −80 dBFS or at 6 s (the last 500 ms fade, so a delay with `fbk` above unity still ends). `effects_service()` skips the descriptor loop while draining: the rows stay frozen, because the active Scene's record is OFF's. Then the union clears and the io flags drop to 0 | The outgoing type's own cost, which its budget already holds, plus a few instructions per frame for the mute and envelope. No OFF ops row, and no playout path in `sk_feed` | The exact tail of the real tank or tape, every type including CrumpBit |
| A2-b | The plan's playout (§5.5) | ≤ 18 | An emulated tail: a looped tape window, and a ring as in A1 |

**Under A2-a:**

- A switch out of OFF during a drain is an ordinary type change. The
  draining type exports its handoff and the incoming type feeds from it.
- A switch back to the draining type keeps the runtime and its tail, as a
  same-type Scene switch does.
- RAM stays at 0 B. The drain flag shares `effects_state.reserved` with the
  slot lock (§5.7): bit 0 is the lock, bit 1 the drain.
- §8 H-7 tests the drain instead of the playout.

### A3. The voice-claim paths of the elastic seating have no trigger today

**Finding.**

- `fxbuf_voiceAcquire()` has no caller (§H.4). The share is always the whole
  arena, and the minimum share (73,664 B) cannot occur until a voice buffer
  user exists. The Colloid (P2) needs no voice units.
- The only seat changes this plan causes are its own: seating in a sub-span
  during a feed, then growing when the feed ends.

**Options.**

| | What gets built | Tested |
|---|---|---|
| A3-a | Everything in §3.3: feed seating and growth, plus the voice-claim paths (keep-newest on a shrink, growth on a release, header moves on both) for the three new types | Host only for the voice-claim paths (S-1 at the minimum share, S-3); they cannot be reached on the device |
| **A3-b (rec.)** | What the feed uses: sub-span seating, growth at the next wrap when the feed ends, the plate's quiet reseat. On a shrink, the three new types reseat and fade the wet in over 10 ms (D14 already accepts a cut). CrumpBit keeps its existing shrink rule | All of it on the host and the device (S-2 and S-4 run through a feed's end). S-1 and S-3 wait for the first voice buffer user |

Every buffer user still takes the whole share, so D2 holds under either
option.

### Inputs needed before the schedule (not architectural)

- **D23:** the §12 option set.
- **§H.2 device results:** the worst Scene with CrumpBit `grn` and the SVF
  restored. §5.7 rests on the 16,284 estimate.
