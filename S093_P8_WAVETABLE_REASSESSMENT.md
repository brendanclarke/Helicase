# S093 P8 — Alkaline Wavetables and Samples: Reassessment and Plan

- **Session:** 093, phase 8. An initial assessment and plan. **No firmware
  code is changed by this document.** One measurement record was added:
  `tools/dsp_test/baselines/s093p8/` (§12).
- **Brief (user, S093):** play wavetables or samples through the Alkaline
  ladder **inside the Basic budget** (the Drum ceiling). The P6 parameter
  tweaks and moves stand. The P7 implementation
  (`S093_P7_WAVETABLE_SAMPLES_OSC_IMPLEMENTATION.md`) is **discarded**.
- **Reference figures:**
  - Alkaline `wav` (Saw/Sqr, the P6 image): fused loop **214** + **1,230**
    per block / 32 = **252.4 per sample**;
  - Drum ceiling: **261.3**;
  - margin: **8.9 per sample**.
- **Binding rules:**
  - the CPU ceiling, whole voice with per-block work, VDIV counted;
  - constant per-sample cost (the longest path counts);
  - RAM sign-off;
  - no new ITCM code;
  - the S087 Oscillator loops and the ZDF twins are not edited, **except the
    8-bit wavetable removal (§13, D9)**;
  - no backward compatibility;
  - nothing needed goes in `build/`.
- **Revision 2 (user decisions, 2026-11-10): §13 overrides §0–§11 where they
  differ.**
  - Wavetables only: one-shot and looped samples are not offered.
  - One 1,024-point 16-bit copy of each table, shared by every instrument.
  - The install buffer is borrowed from the browser name cache.
  - Wavetables are 16-bit only; the 8-bit wavetable code is removed.

  The linked measurements behind these are in `S093_P8_IMPLEMENTATION.md`
  §0, which is amended next.

---

## 0. Summary

**The idea:** change the data to fit the loop we already have, instead of
writing new loops for the data.
- **Wavetables:** each table gets a **1,024-point 16-bit copy**, written at
  install time. One frame of it is exactly the shape of a `sawTable` row, so
  it plays through **today's fused loop, `DiodeLadder_renderB2Osc()`**:
  - same function, same instructions, 214 per sample;
  - no new audio loop and no new loop flash;
  - the frame is chosen once per block.
- **One-shot samples:** one new lean fused loop. Measured standalone at
  **215**, against the reader-identical forms P7 built (217–262).
- **Looped samples:** measured at **222–227**. They do not fit without a
  saving elsewhere in the voice, so they are a decision (§6).

| Mode | Loop (measured, standalone) | Per block | Whole voice (est.) | Against 261.3 |
|---|---:|---|---:|---:|
| `wav` (today) | 213 (214 linked) | 1,230 | 252.4 | −8.9 |
| **`wtb`** | **213 (the same function)** | 1,230 + typed control (target ≤ +120) | **≈ 253–256** | **−5…−8** |
| **`smp`** | **215** | 1,230 + typed control (target ≤ +120) | **≈ 255–257** | **−4…−6** |
| `slp` | 222 (seam not exact) – 226 (exact) | 1,230 + typed control | ≈ 262–268 | **+1…+7** |
| `wtb` blend | not offered (two reads per sample; P7 measured 252–262) | — | — | — |

- **RAM: 0.** The bind cache already in `OscInfo` holds the typed state (§7).
- **Flash:** about +3–4 KB of program code (estimated, §11 gate).
- **Sample flash:** +2,048 B per wavetable frame for the copy, under the
  recommended storage option (§4.2).
- **Before any tree code:** a linked spike confirms the `wtb` and `smp` rows
  (P8-1, §11). The P7 failure was planning on estimates; the per-block
  "est." cells above are gated, not assumed.

---

## 1. Why P7 failed

| # | What P7 did | Effect (P7 §0, measured) |
|---|---|---|
| F1 | Required the fused loops to be **bit-identical to Drum's readers** (law AL-X2). This put the readers' int16 truncation, item gain multiply, exact `slp` seam, 64-bit `wtb` index (`(ph × len) >> 32`), 8/16-bit depth test and blend segment switch into the ladder loop | Loops 217 / 229 / 235–239 / 252–262, against a plan of 214–218 |
| F2 | Read wavetables at **any frame length** (16..2,048 points, 8 or 16 bits) inside the loop | The `wtb` read costs +21…+25 over the table read; the compiler split each `wtb` loop into an 8-bit and a 16-bit copy |
| F3 | Reused **Drum's generic per-block machinery**: `osc_userBind()` every block, `osc_typedPositionApply()` with a 64-bit cap division (213 instructions), the blend claim and plan | +537 per block, against an estimate of +115 |
| F4 | Built **all four modes and the blend at once**, with the slot reserve sized from estimates | Every typed mode over the ceiling (+11 to +56) |
| F5 | Recommended moving typed modes to an **Advanced** place | Contradicts the brief, which is Basic |

**Underlying constraint (measured here, §12):**
- The B2R loop already keeps its float registers full. The `wav` loop has
  14 stack references per sample.
- So each extra live value in the loop costs spills, not just its own
  instructions.
- Example: a loop wrap (`if (ph >= end) ph -= len`) costs **+7** over an end
  clamp (`ph = min(ph + inc, end)`), although both are three integer
  instructions. The difference is two extra float spill/reload pairs.
- Any new variant must be compiled and counted. It cannot be estimated from
  its source.

**What P7 got right** and P8 keeps:
- the row and menu analysis (P7 §3, re-verified at schedule time, §8);
- the tool methods (`control_cost.py --elf`, `elf_looppath.py`, the
  `elf_p4_check` gates);
- the finding that the per-block cost of Drum's typed path is large.

---

## 2. The budget as allowances

Whole voice = loop + per block / 32 ≤ 261.3. With today's per-block 1,230,
the extra per-block work a typed mode may add is:

| Loop (linked) | Typed per-block allowance |
|---:|---:|
| 214 (`wtb` through `renderB2Osc`) | ≤ 283 |
| 216 (`smp`, standalone 215 + 1 link margin) | ≤ 219 |
| 217 | ≤ 187 |
| 222 (`slp`, seam not exact) | ≤ 27 |
| 226 (`slp`, exact seam) | none (over by 4.8 on the loop alone + 1,230) |

The plan's per-block design target is **≤ +120** (≈ 3.75 per sample), which
leaves `wtb` and `smp` a clear margin. `slp` cannot pay for any typed control
work unless the existing 1,230 shrinks (§6).

---

## 3. Principles for P8

1. **The data matches the loop.** Do format conversion at install time
   (audio suspended, blocking already allowed: `filesystem.c` l. 25681, "one
   blocking pass, audio suspended by the caller"). It then costs nothing at
   run time.
2. **No bit-identity with Drum's readers.** The new modes get their own laws
   (sound and behaviour). The `wav` path keeps AL-X (bit-identical, as
   today).
3. **The Alkaline owns its typed control:**
   - bind only on a change;
   - the increment straight from the frequency the voice already computes;
   - no generic dispatch, no 64-bit division, no blend plan.
4. **Measure first.** Count every loop variant compiled (done here,
   standalone) and linked (P8-1) before a literal schedule is written.
5. **Deliver in stages.** `wtb` and `smp` first. `slp` and the blend only by
   decision.

---

## 4. Wavetables through today's loop

### 4.1 What the loop reads

`DiodeLadder_renderB2Osc()` (`DiodeLadder.c` l. 414–420, body l. 273–313)
reads:
- one `const int16_t *row` of **1,024 points**, chosen per block;
- `itg = ph >> 22`, next point `(itg + 1) & 0x3FF`;
- the fraction `(ph & 0x3FFFFF) × 2^-22`;
- `ph += phaseInc`.

The voice passes `sawTable[tableOffset]` or `alkSqrTable[tableOffset]`
(`AlkalineVoice.c` l. 254–279). **Any 1,024-point int16 frame can be passed
in their place.**

### 4.2 The 1,024-point copy

Today's stored wavetables are **any length from 16 to 2,048 points**, in 8 or
16 bits:
- the limits are `WAVETABLE_FRAME_MIN` / `WAVETABLE_FRAME_MAX` in
  `SampleMemory.h` l. 116–117;
- the length is the source file's (`filesystem.c` l. 25508);
- the card's own table, `SD_CARD/wavetables/ak_hvox`, is **104 frames of 600
  points, 16-bit** (AKWF files).

So "1,024-point frames only" would refuse your own content. The frames must
be resampled. Two storage options:

| | **N2, a copy beside the raw frames (recommended)** | N1, replace the raw frames |
|---|---|---|
| What | The installer writes the raw frames as today, then a 1,024 × int16 copy of every frame right after them. It resamples by reading the raw frames back from flash (memory-mapped) | The installer stores every wavetable as 1,024 × int16 only |
| Drum | **Unchanged**: same data, same S087 loops | Reads the new data through its generic reader (len 1,024, 16-bit). Its sound changes by the resampling |
| RAM | **0**: the source is in flash | A whole-frame buffer, up to 2,048 × int16 = 4 KB, during install: a new allocation or a loan (for example `fs_list_cache_name`, 9,000 B, which copy/clear already borrows). Needs sign-off |
| Sample flash | +2,048 B per frame. `ak_hvox`: 124,800 B raw + 212,992 B copy | Per frame 2,048 B. `ak_hvox` 212,992 B in place of 124,800 B; a 2,048-point table halves |
| Catalogue | **No new field**: the copy's offset is `offset + roundup4(frame_len × frame_count × bits / 8)`, because `sampleInstall_flush()` pads every item to a whole word (`SampleInstall.c` l. 151–165) | `frame_len` is always 1,024 and `bits` always 16 |

- **Fit check:** `sampleInstall_beginWavetable()` checks free space for the
  raw frames (l. 372–374). Under N2 it must count the copy too.
  - Recommended: the copy is part of the item, so a table that cannot store
    both is refused.
  - The alternative, a flag bit marking a table without a copy, adds a
    "not playable on Alkaline" state to the menu.
- **Resampler:** periodic (a frame is one cycle, so the kernel wraps), as a
  windowed sinc.
  - Upsampling (N < 1,024) keeps every harmonic.
  - Downsampling (N > 1,024) removes the harmonics at and above 512, as it
    must.
  - Cost: 1,024 × taps per frame, well under a millisecond at 216 MHz. This
    is install time only.
  - The output saturates to int16, since the Gibbs overshoot of a sharp edge
    can exceed full scale.
  - The tap count and window are set by a host law (§11 P8-2).
- **8-bit sources:** the copy is always 16-bit (`<< 8`, then resample).

### 4.3 Playing it

- **Pitch:** exactly the Saw's. `osc_setTableFreq()` (`Oscillator.c`
  l. 1225–1239) gives the 1,024-point cycle law, `freq2PhaseIncr1024(f)`. Its
  `tableOffset` is not used: there is one row per frame.
- **Frame (`pos`):** the frame index follows `pos`, moving **at most one
  frame per block** toward the target. Adjacent frames are similar, so a move
  is a series of small steps; this is Drum's no-slot behaviour.
  - The row is the copy's base + frame × 1,024.
  - The `pos` → frame scale is computed at bind time, so there is no
    per-block division.
- **Level:** a full-scale frame enters at the Saw's scale
  (`ALK_SAW_SCALE_V`, ±32,625 LSB = 5.35 V), times the block's drive.
- **Legato / slide:** the phase runs on, as for Saw/Sqr.
- **`pos` on a non-legato note:** sets the frame. The phase start-reset
  (P6's `pos` on Saw/Sqr) does not apply, because under `wtb` `pos` is the
  frame.
- **Aliasing:**
  - The stock waves have per-octave rows (`[11][1024]`); a user frame has one
    row.
  - High notes alias on a bright frame, as Drum's `wtb` does today.
  - The catalogue reserves "mip levels" in `flags` bits 4–7
    (`SampleMemory.h` l. 164); nothing implements them (searched).
  - Per-octave copies would multiply the copy's flash by up to 11. Not
    planned. It is a listening item.
- **Blend between frames:** not offered. It needs two reads per sample (P7
  measured 252–262). Stepping is the price of the budget.

---

## 5. One-shot samples: one lean loop

### 5.1 The loop (measured, §12)

```c
/* lean smp: Q20.12, gain folded into sScale, playhead clamped at the end */
const uint32_t idx = ph >> 12;
const float a = (float)d16[idx], b = (float)d16[idx + 1u];
s = (a + (float)(ph & 0xFFFu) * (1.0f / 4096.0f) * (b - a)) * sScale;
const uint32_t nx = ph + phaseInc;
ph = (nx > endPh) ? endPh : nx;
```

- **215** per sample standalone (`wav` 213 on the same basis), 1,344 B.
- Keeping the readers' int16 truncation also measured 215, so the lean form
  is a choice, not a need.
- The end reads the item's two zero pad frames (`SampleMemory.h` l. 180–181; written by `sampleInstall_finishSample()`, `SampleInstall.c` l. 290),
  so a finished one-shot outputs exact silence and holds.
- P7's `smp` loop stayed a separate function in the link (two call sites):
  standalone = linked. P8 has one call site, so LTO may inline it into the
  sync block. P8-1 measures the linked loop.

### 5.2 Control

- **Pitch:** Drum's law, `phaseInc = f × 2^22 / REAL_FS` (`Oscillator.c`
  l. 1647–1650), so a sample plays at the same pitch on both instruments.
  - The item plays at its recorded speed at `f = REAL_FS / 1024`.
  - One multiply in place of `osc_setTableFreq()`.
- **`pos` = start point** (recommended, P8-D3): a non-legato note starts at
  pos/128 of the item. This is set at the trigger, in the main loop, not on
  the block path.
  - A `pos` change during a note takes effect at the next note.
  - Drum's moving playhead bend (with its 64-bit cap) is not offered.
- **Legato / slide:** the playhead runs on, so a slide bends the pitch of the
  playing sample, as the 303's VCO keeps running.
- **Item gain:** the catalogue gain code is folded into `sScale` per block (a
  multiply at bind time), not per sample.

---

## 6. Looped samples: over by the loop alone

Measured standalone (§12):

| Form | Loop |
|---|---:|
| Exact seam (next point = loop start at the seam), absolute phase | 226 |
| Exact seam, loop-relative phase | 226–227 |
| Seam not exact (reads the next stored frame after the loop end) | 222 |

- With today's 1,230 per block, `slp` is 260.4 (222) or 264.4 (226) **before
  any typed control work**.
- The inexact seam also clicks at the loop rate when the frame after the
  loop end differs from the loop start, and reads a zero pad when the loop
  ends at the sample's end.
- A per-block wrap (the plain `smp` loop between wraps) fails on loops
  shorter than one block's travel, and needs a split render on a wrap block,
  which is worse in the longest path.

**Options (P8-D2):**

| | What | Consequence |
|---|---|---|
| **A (recommended)** | `slp` not offered on the Alkaline in P8 | `wtb` and `smp` ship as Basic |
| B | Find ≥ 5–8 per sample in the `wav` per-block work (1,230), then add the 226 loop | Needs its own study; the per-block path was already reduced in S093 P1/P3 |
| C | `slp` admitted only in an Advanced place (P7's Option A, `slp` only) | Not Basic, against the brief |

---

## 7. Per-block design (all modes)

**Async (`Alkaline_calcVoiceAsync()`, after the frequency):**

| Mode | Work |
|---|---|
| `wav` | Unchanged: `osc_setTableFreq()` and the P6 `pos` bend |
| typed, every block | Bind check: `(oscType, waveform, sampleMemory_getGeneration())` against the cache. On a change, a **lean Alkaline bind**: entry pointer, the copy's base (`wtb`) or data/end (`smp`), the `pos` scale, the gain into the cached scale. No generic `osc_userBind()` |
| `wtb` | `osc_setTableFreq()` (the increment only); frame step toward `pos` (one compare, one add); row = base + frame × 1,024 |
| `smp` | `phaseInc = f × k` |

- **Bind on a change is in the longest path.** An LFO on `wav` could rebind
  every block, so the gate counts the bind as if it runs every block. It is
  pointer and integer arithmetic, with no division and no 64-bit operation.
- **RAM 0.** `OscInfo` already has the cache fields:
  - `sampleCacheIndex`, `sampleCacheLooped` (the kind),
    `sampleCacheGeneration`, `sampleCacheSize`;
  - `sampleCacheData` (the row or data pointer);
  - `positionApplied` (the frame being played).

  `osc_userBind()` uses them for Drum (`Oscillator.c` l. 1609–1625), and an
  Alkaline oscillator never calls it. The `AlkalineVoice.wave` byte freed by
  moving Saw/Sqr into `osc.waveform` (P7 K6) stays free.

**Sync (`Alkaline_calcVoiceSyncBlock()`):** one branch per block, outside
every loop:
- `wav` / `wtb` → `DiodeLadder_renderB2Osc()` with the row and scale;
- `smp` → `DiodeLadder_renderB2Smp()`.

The `wav` arguments are unchanged.

---

## 8. Rows and menu

P7 §3 (the `typ` row, row 0 becoming Drum's `osc1_wave` shape, the three
wave helpers, the `typ` writer clamp, `init.alk`) was not where P7 failed.
It is re-verified against the tree when the schedule is written, with these
changes:
- **Type list:** `wav` / `smp` / `wtb` only (no `slp`, under P8-D2 A).
  `MENU_OSC_TYPE` is Drum's four-entry list, so the schedule picks one of:
  - an Alkaline-only three-entry list whose writer maps 0/1/2 to
    `OSC_TYPE_WAV` / `SMP` / `WTB`;
  - the shared list with `slp` skipped for an Alkaline slot in the edit,
    clamp and writer paths.

  The first keeps the shared menu code untouched. Recommended, subject to
  the special-tag check (S073: a new row form needs its `IM_SPECIAL_*` tag).
- **`wav` cell:** Saw/Sqr under `wav`, the sample list under `smp`, the
  wavetable list under `wtb`.

---

## 9. Considered and not taken

| Approach | Why not |
|---|---|
| P7's fused readers at any frame length | Measured 235–262 (F1, F2) |
| A per-voice RAM row, filled when the frame changes | 2 KB per voice (sign-off). The fill (≈ 1,024 × several cycles) lands in the block where `pos` moves, so an LFO on `pos` makes it every block. Not constant cost |
| A runtime-shift loop for power-of-two frame lengths | Listed under Failed Approaches (S086: "new variable-shift reader loops"). It is also unnecessary once the data is 1,024 points |
| Advanced class for typed modes (P7 Option A) | Against the brief |
| The interpolation-slot lock (P6 §7.5) | A slot is worth ≈ 10–12 per sample of blend premium; there is no blend in P8 |

---

## 10. Decisions

| # | Question | Recommendation (revision 1) | Decided (revision 2, §13) |
|---|---|---|---|
| **P8-D1** | Wavetable storage | N2: a copy beside the raw frames | **N1:** one 1,024-point 16-bit copy, used by every instrument; no raw frames (D1′) |
| **P8-D2** | Looped samples | A: not offered in P8 | **Not offered** |
| **P8-D3** | `pos` on `smp` | Start point at the note | **Void:** one-shot samples are not offered (D7) |
| **P8-D4** | `pos` on `wtb` | One frame per block toward the target | **As recommended** |
| **P8-D5** | `typ` list | Alkaline-only three-entry list | **Alkaline-only two-entry list: `wav`, `wtb`** |
| **P8-D6** | P7 document | Marked discarded, kept as a record | **As recommended** |

---

## 11. Work order and gates

| Phase | Content | Gate |
|---|---|---|
| **P8-0** | Decisions P8-D1…D6 | — |
| **P8-1** | **Linked spike, outside the tree** (scratch, as P7 §0 did): the async/sync typed paths and `DiodeLadder_renderB2Smp()` on the P6 tree, linked `-Ofast -flto` | `elf_looppath.py` on the sync block and `renderB2Smp`; `control_cost.py --elf` (typed per block); **whole voice ≤ 261.3 for `wav`, `wtb`, `smp`**; `wav` loop 214 and its path 1,230 unchanged. Only if this passes is the literal schedule written |
| **P8-2** | Installer: the 1,024-point copy (N2), the resampler, the fit check; a `sampleMemory_` accessor for a frame's row | Host law for the resampler: harmonic levels against an FFT reference for N = 16, 600, 1,024, 2,048, 8- and 16-bit; the copy's offset and alignment; `test_catalogue` |
| **P8-3** | Rows and menu (§8) | `special_tags`; menu check on the device |
| **P8-4** | Voice: async bind and control, sync dispatch, `renderB2Smp()` | Bench: **AL-X** unchanged (`wav`); **AL-W** (new): `wtb` = `renderB2Osc()` on the copy's row, bit for bit, with the frame stepping law; **AL-SM** (new): start point, end silence and hold, pitch law, legato |
| **P8-5** | Linked measurement on the image | G3 loops, G4 per block, G5 whole voice per mode; `elf_p4_check` (0 MISSING, 0 grew); the inlined-set diff (`mixer_calcNextSampleBlock()`); `link_budget.py` ≥ 16 KiB |
| **P8-6** | `init.alk`, documents (INSTRUMENTS_DSP_REFERENCE §5.5, CPU_USE_DSP_AUDIT, FILESYSTEM_SPEC, STORAGE_SRAM_MANIFEST sample flash, MIDI_NRPN_TABLE, the 093 log, MEMORY.md Alkaline rule) | — |
| **Device** | Four Alkalines in `smp` and `wtb`, Res and drive full; the worst-case Scene; listening (frame steps, aliasing at high notes, `smp` end) | User |

---

## 12. Verified in the tree and measured for this document

| Fact | Where |
|---|---|
| The fused loop reads one 1,024-point int16 row per block, Q10.22 | `DiodeLadder.c` l. 273–313, 414–420 |
| The voice picks the row per block from `tableOffset` | `AlkalineVoice.c` l. 254–279 |
| `osc_setTableFreq()`: `freq2PhaseIncr1024`, octave index | `Oscillator.c` l. 1225–1239 |
| Wavetable geometry 16..2,048 points, 8/16-bit, ≤ 128 frames; "mip levels" reserved, unused | `SampleMemory.h` l. 116–117, 158–166; search for `mip` in `Core/SampleRom`, `Oscillator.c` |
| Frame length from the source file; all frames the same length | `filesystem.c` l. 25508–25526 |
| Install is a blocking pass with audio suspended; 512 B reads | `filesystem.c` l. 23845, 25378–25400, 25681 |
| Items end word-padded; the fit check | `SampleInstall.c` l. 151–165, 360–386 |
| `ak_hvox`: 104 files, 600 points, 16-bit mono 44.1 kHz | `SD_CARD/wavetables/ak_hvox` (chunks read) |
| `osc_userBind()`: generic dispatch every block; the bind cache fields; Drum's sample increment law | `Oscillator.c` l. 1552–1651 |
| Nearest frame (Drum) | `Oscillator.c` `osc_wtbFrameOf()` |
| `fs_list_cache_name` 9,000 B with a copy/clear loan | `STORAGE_SRAM_MANIFEST.md` l. 802 |

**Measured (2026-11-09):**
- **Method:** scratch variants of `dl_renderB2Body()`, compiled standalone
  (`-Ofast`, no LTO) and counted with
  `elf_looppath.py --cond-heads --in-range`.
- **Record:** `tools/dsp_test/baselines/s093p8/loops_standalone.txt` and
  `DiodeLadder_p8_variants.c` (a record, not built).

| Loop | Per sample | .text |
|---|---:|---:|
| `renderB2` (plain) | 198 | 1,244 B |
| `renderB2Osc` (`wav`; 214 linked in the P6 image) | 213 | 1,340 B |
| `smp` lean | 215 | 1,344 B |
| `smp`, int16 truncation kept | 215 | 1,344 B |
| `slp`, exact seam | 226 | 1,388 B |
| `slp`, seam not exact | 222 | 1,372 B |
| `slp`, loop-relative, exact / not exact | 227 / 222 | 1,388 / 1,368 B |

The `wav` loop has 14 stack references per sample and the `slp` loops 18
(§1). Standalone matches the published basis: 198 and 213 as in P7 §5.

**Not yet measured (P8-1):**
- the linked loops: LTO may inline `renderB2Smp()` into the sync block;
- the typed per-block path.

---

## 13. Decisions, revision 2 (user, S093 P8, 2026-11-10)

These override §0–§11 where they differ. The linked spike that informed them
is `S093_P8_IMPLEMENTATION.md` §0; that schedule is amended to match.

| # | Decision | Detail |
|---|---|---|
| **D7** | **Wavetables only** on the Alkaline | One-shot samples fit the voice budget (loop 215, per block 1,364), but linking their render grew two mixer hot loops of other engines by 1 (a decimation loop 10 → 11, HiHat's `calcSampleOscFmBlock` 36 → 37; bisected in the spike). Looped samples are over the ceiling on their loop alone (§6). The Alkaline `typ` list is `wav`, `wtb` |
| **D1′** | **One 1,024-point 16-bit copy per frame, shared by every instrument** (N1; replaces N2) | The installer stores each wavetable only as its resampled 1,024 × int16 frames. Drum, Snare, Cymbal and HiHat read them through their existing wavetable readers, which take any frame length; the Alkaline reads them through its fused table loop. The raw frames are not stored |
| **D8** | **Install buffer: borrow `fs_list_cache_name` (9,000 B)** during the modal install | Holds the frame being resampled (up to 2,048 × int16 = 4,096 B). No new RAM. It follows the existing copy/clear loan (`STORAGE_SRAM_MANIFEST.md` l. 802: lent while an operation runs, cleared on return; Load/Save reloads its index) |
| **D9** | **Wavetables are stored 16-bit only; the 8-bit wavetable code is removed** | An 8-bit wavetable folder is ignored (not installed); a 24-bit folder is converted to 16-bit and installed (O1, §13.6) |
| **D10** | **8-bit support is off throughout: samples, loops and wavetables, for every instrument** | The installer does not read 8-bit files: an 8-bit file is skipped like any other refused format, and the 8-bit → 16-bit conversion is removed (§13.5) |
| **D11** | **24-bit and stereo sources stay supported, through the existing conversion** | 24-bit → 16-bit and stereo → mono averaging are kept for samples and loops (≈ 270 B and ≈ 100 B, §13.5). Wavetables too (O1, §13.6) |

### 13.1 D1′: what it means

- **Sample flash per frame:** 2,048 B, whatever the source length. The card's
  `ak_hvox` (104 frames of 600 points) takes 212,992 B, against 124,800 B
  stored today and 337,792 B under N2.
- **Other instruments' sound:**
  - frames of 1,024 points or fewer (`ak_hvox`) play their harmonics within
    0.0001 dB (spike, host law). The top 5 % of a short frame's band rolls off
    (−1.2 dB at 0.483, −5.3 dB at 0.498 of its Nyquist);
  - longer frames lose harmonics 512 and up. Above about 43 Hz those lie
    beyond the output's Nyquist and alias in today's playback.
- **Other instruments' CPU:** unchanged or lower.
  - The readers' code is not changed except the 8-bit removal (D9).
  - Frame length does not enter their loops (one `(phase × length) >> 32`
    multiply for any length).
  - Every table takes the 16-bit path, the cheaper one (§13.3).
- **Catalogue:** each wavetable entry describes the copy itself (`frame_len`
  1,024, `bits` 16, `offset` its first row). The amended schedule settles
  whether those two fields stay as checked constants or are removed (the entry
  size is asserted at 20 B).
- **No backward compatibility:** a card installed by older firmware reads
  STALE and must be reinstalled (catalogue version bump, as in the schedule).

### 13.2 D9: scope of the 8-bit removal

**Installer and catalogue:**
- `SampleInstall.c`:
  - `sampleInstall_beginWavetable()` accepts `bits` 16 only (today 8 or 16,
    l. 370);
  - the 8-bit store branch in `sampleInstall_writeFrame()` goes (l. 398–403).
- `filesystem.c` `filesystem_installWavetableFolder()`: the folder's depth
  test.
  - Today `bits = (wav.fmt.bits == 8u ? 8u : 16u)` (l. 25506), so a 24-bit
    source is converted to 16-bit and installed.
  - **Under D9/D10 an 8-bit folder is ignored; a 24-bit folder is converted
    to 16-bit and installed (O1).**
- `SampleMemory.c` `sampleMemory_wavetableRangeValid()`: `bits` must be 16
  (today 8 or 16, l. 114).

**Oscillator (`Oscillator.c` / `.h`):**
- `OSC_BIND_WTB8` (`Oscillator.h` l. 204) and its uses:
  - the bind's depth choice (`Oscillator.c` l. 1604–1605) and its kind tests
    (l. 1616, 1647);
  - the render dispatch cases (l. 2399, 2422, 2445, 2467, 2490, 2583).
- `osc_wtbRead()`'s 8-bit branch (l. 2709–2711) and `osc_wtbFrame()`'s
  per-depth stride (l. 2719–2725).
- The **8-bit loop copies** the compiler makes of every wavetable reader. The
  readers test the depth inside the loop; it is loop-invariant, so each
  reader links as two loops (§13.3).

**Unaffected:**
- **The channel rule:** a stereo wavetable source is still averaged to mono.
  D9 decides the depth only.
- **8-bit sample files** are covered by D10 (§13.5).

### 13.3 The readers' two copies, measured on the P6 image

`elf_looppath.py`; in each reader the 8-bit copy is the one with the `ldrsb`
loads.

| Reader | 8-bit copy (removed) | 16-bit copy (kept) |
|---|---:|---:|
| `osc_wtbPlainBlock` | 26 | 24 |
| `osc_wtbBlendBlock` | 38 | 34 |
| `osc_wtbFmBlock` | 34 | 32 |
| `osc_wtbFmBlendBlock` | 46 | 42 |
| `osc_wtbRingBlock` | 34 | 32 |
| `osc_wtbRingBlendBlock` | 47 | 43 |
| `osc_wtbSyncBlock` | 36 | 34 |
| `osc_wtbSyncBlendBlock` | 48 | 44 |
| `osc_wtbFmRampBlock` (S089 P3) | 35 | 33 |
| `osc_wtbFmBlendRampBlock` (S089 P3) | 47 | 43 |

- **The worst case drops** (measured in `S093_P8_IMPLEMENTATION.md` §0.4):
  - removing the 8-bit branch alone left one loop per reader, but 1–4
    instructions longer than its former 16-bit copy (codegen, S7 there);
  - the schedule therefore gives the readers the fixed 1,024-point read
    (every stored frame is 1,024 points, O3). Each reader's worst case then
    falls by 3–5, and its loop is below the former 16-bit copy in nine
    readers (+1 in the sync blend).
- **CPU audit:** the Drum/Snare/Cymbal/HiHat `wtb` figures in
  `CPU_USE_DSP_AUDIT.md` are re-measured.

### 13.4 Consequences for the gates

- **Binding rule:** "the S087 Oscillator loops are not edited" is lifted for
  this removal only. The ten readers above are edited to read 16-bit frames;
  no other loop changes.
- **`elf_p4_check`:**
  - against `baselines/s093p4`, the ten 8-bit copies report MISSING, by
    design;
  - the ten 16-bit copies and every other loop must stay exact;
  - after that check, the baseline is re-snapshotted as
    `baselines/s093p8/osc.txt` with the reason recorded;
  - the mixer gate stays 0 grew.
- **ITCM:**
  - the readers live in `.itcm.s087` and `.itcm.s089p3`, so ITCM shrinks
    (today 14,552 B; ITCM ends at `0x38d8`);
  - subsections after `.itcm.s087` move to lower addresses;
  - no section attribute changes;
  - the gate compares loop text, not addresses.
- **Host tests:**
  - `samples` law 3 (an 8-bit frame stays 8-bit) becomes "an 8-bit or a
    24-bit wavetable is refused";
  - the `oscillator` bench's W3 (8-bit frames) is removed;
  - the law sets and break proofs are listed in the amended schedule.

### 13.5 D10: no 8-bit sources

**Scope:**
- `SampleInstall.c` `sampleInstall_acceptFormat()` refuses `bits` 8 for
  every kind (l. 95: today 8, 16 or 24).
- `sampleInstall_sourceSample()` loses its 8-bit branch (l. 187–188).
- A refused file is skipped as today: an unaccepted `fmt` chunk fails
  `filesystem_parseWav()` (`filesystem.c` l. 25249).
- The `samples` host laws for 8-bit sources (install law 2: "8-bit stereo
  average/peak", l. 280–305) become refusal laws.

**Program space, measured** (linked `.text` of today's tree with the source
handling cut back step by step, `arm-none-eabi-size`; scratch builds):

| Removed (cumulative) | `.text` | Δ |
|---|---:|---:|
| — (P6 tree) | 689,856 B | — |
| 8-bit sources (D10) | 689,944 B | +88 (LTO inlined the remaining helper into `filesystem_installFrames()`; no real saving) |
| also the 24-bit → 16-bit conversion | 689,672 B | −272 |
| also the stereo → mono sum | 689,576 B | −96 |

- **Decided (D11, user):** the 24-bit conversion and the stereo sum stay.
- **The mixer moved:** the D10 build alone changed
  `mixer_calcNextSampleBlock` by 4 B (LTO layout), so even an installer-only
  change runs `elf_p4_check`.

### 13.6 Decisions before the implementation is amended (all decided, user, 2026-11-10)

| # | Question | Options | Recommendation → **decided** |
|---|---|---|---|
| **O1** | **24-bit wavetable sources** | (a) converted to 16-bit and installed, like samples and loops (D11); (b) ignored, as D9 first said ("16-bit only") | **Decided (a)** (user). D11 keeps 24-bit support "through the conversion", and the 16-bit stored format of D1′ is the same either way. Stereo wavetable sources stay averaged under both |
| **O2** | **The built-in 8-bit ROM sounds**: the stock crash wave `crashSample` (`Samples.c` l. 67: unsigned 8-bit, 32,769 B, read by the CRASH wave, `Oscillator.c` l. 529–554) and the 12 transient waveforms `transientData` (`transientTables.c` l. 63: signed 8-bit, 12 × 2,205 B) | (a) out of scope: D10 covers installed content (samples, loops, wavetables); (b) in scope: store both as 16-bit and change their readers | **Decided (a)** (user). They are firmware data, not installed content; (b) would double them in program flash (≈ +32.8 KB and +26.5 KB, against 41,940 B of headroom after P8) and edit the stock oscillator and transient loops, with no gain in the formats you load |
| **O3** | **The catalogue's wavetable `frame_len` and `bits` fields** under D1′ (every stored table is 1,024 points, 16-bit) | (a) keep them, written as 1,024 and 16 and checked by `sampleMemory_wavetableRangeValid()`; (b) remove them (the entry size is asserted at 20 B, so they become reserved bytes) | **Decided (a)** (user): no layout change. The other instruments' readers read `frame_len` (their index is `(phase × frame_len) >> 32`), so they keep working unchanged; `bits` stays only as a checked format mark once the 8-bit branch is gone (D9), and a corrupt entry is refused |

**Carried, not open:**
- the catalogue version bump to v3 (older cards read STALE: no backward
  compatibility);
- the `elf_p4_check` re-baseline procedure (§13.4);
- the borrowed name cache (D8). Load/Save reloads its index after the loan,
  as after copy/clear.

