# S095 P1 — Delay, Reverb, Echo: Implementation Schedule

- **Status:** schedule, written 2026-11-11 from the tree as it stands
  (`dev-ph7-alkaline`, uncommitted S094 P3/P4/AS-13 work; image
  `build/LXRV2_lxr02.img`, flash 691,100 B, ITCM 13,984 / 16,384 B). **No
  code has changed.** Every cited line is the line in that tree before the
  step. When an earlier step in this schedule moves a line, the later step
  still cites the original line number and names the statement, so the
  implementer can find it by its text.
- **Re-implementation (2026-11-12): read §S18 first.** The first
  implementation (logged in §S17, kept in `Helicase-failed-P1/`) came out at
  +45.8 KB flash and over its CPU caps and was reset. §S18 is the second
  implementation: its phase budgets, the design changes that supersede rows
  of this schedule (SD5, SD7, SD8, §1, S5.2, S6's feed API, S10.8, the CPU
  gates; D19's Advanced remainder and D22's loop injection are withdrawn),
  and only measured figures. Where §S18 and an earlier section disagree,
  §S18 holds.
- **Source plan:** `S095_P1_FX_DELAY_RVB_ECHO.md`, revision 5:
  - D1–D26;
  - D23 is the recommended option set, with `xfb` and `spr` continuous;
  - §13 F1–F3 take their defaults: F1 one line per reverb side; F2 no feed
    after a drain ends; F3 the 6 s drain ceiling.
- **Binding rules that apply:**
  - **CPU (S089 / S094 P2 §21, D19):**
    - no per-sample cost may rise without a measured saving;
    - each Space type's Effect total (`fxsvc` + `fx`) must fit **16,970**
      cycles per block in steady blocks;
    - in a feed block it may also use what the locked slot frees (≈ 880);
    - **D19:** any measured remainder of the feed's cost comes from
      Advanced, split evenly, reported as one net total.
  - **RAM:** 0 B except the approved +16 B handoff (D18). ITCM 0 B.
  - **Constant CPU:** budget the dearest mode. Every selector is an
    algorithm choice with the same loop cost, or both forms are gated.
  - **Loop rules:** no division, libm call or double in any per-frame loop
    (§3 of the plan; the `space_elf_check` gate).
  - **Documents:** no step edits `MEMORY.md` or `knowledge_files/`. §16
    lists what goes out of date. Commits belong to the user.
  - **Comment blocks:** every change carries an accurate What / Why /
    Inputs / Outputs / Callers-Accessors / Affiliates block in CrumpBit's
    style. The blocks below are the text to paste; variables, loops and the
    arithmetic carry their own comments.
  - **Nothing needed goes in `build/`.** Records go in
    `tools/dsp_test/baselines/s095p1/`.

---

## 0. Schedule-level findings (decided here; the plan's intent unchanged)

The code dive turned up the points below. Each is decided in this schedule
so that no step is left open. Where it changes a plan figure, the plan
figure is superseded for implementation. §16 lists them for the record.

| # | Finding in the tree | Decision |
|---|---|---|
| **SD1** | CrumpBit's runtime is exactly 100 B (asserted `CrumpBitEffect.c:176`). Its one spare byte is `drv_reserved`. It cannot hold a span base, the feed state or a drain counter, and the union cannot grow (RAM rule). | **A fixed arena prefix:** the first **512 B** of the share (`SK_PREFIX_BYTES`). It holds the 64 B control block `sk_ctl_t` (seat, feed, drain) and a 448 B type state header. Every buffer user's audio lives at share offset ≥ 512. The Space state header therefore **never moves**, which removes D9's header copy. RAM stays at 0 B. The cost is 512 B of arena for CrumpBit: `CRUMPBIT_LOOP_MAX_BYTES` 126,656 → 126,144 and `CRUMPBIT_DELAY_CURVE_MAX` 126,654 → 126,142. `dly` row 127 then plays 2.860 s (was 2.871 s), the same top as the Delay (D3, "truncated the same way"). |
| **SD2** | `effect_type_ops_t.init(rt, handoff)` (`EffectsManager.h:234`) has no share. `init` is the only foreground hook before the first render block. | `init` gains `const fx_share_t *share`. The manager passes `fxbuf_effectShare()`. Init then seats, packs the feed and clears in the foreground. StereoFilter's init ignores it. |
| **SD3** | A same-type Scene switch re-exports the handoff while the runtime keeps running (`EffectsManager.c:1958`). | Export stays **side-effect free**: it only describes. The re-ordering D24 allows (summing a reverb's lines, packing them) happens in the incoming type's `sk_feedBegin()` at init. By then the outgoing engine has stopped for good. |
| **SD4** | The menu and MIDI Learn read `effects_activeType()` (`menu.c:5752`, `:17049`, `:17361`; `MidiLearn.c:86`, `:111`). | `effects_activeType()` returns the **logical** type: OFF while a drain runs. `effects_state.runtime_type` keeps the draining type internally. |
| **SD5** *(ring packing superseded by §S18 R1)* | Growing the plate ring from 32 KB to 64 KB (the quiet reseat) needs a 32 KB clear inside the render path. That is ≈ 8,000 stores in one block, about 5 % of the cap. | **The Reverb ring is always 64 KB.** `seat_min` = 73,152 B, exactly the minimum share's span, so the ring always fits as §3.3 promises. There is no 32 KB mode, no quiet reseat and no `siz` clamp (`siz` 0.35–1 always), and S-4 is dropped. Ring sources are packed small (SD7), so a Reverb still seats during any feed. |
| **SD6** | §5.3 step 6's 4 KB-per-block clear (≈ 1,000 cycles a block) is not in any cost estimate. | The plate rings (Reverb 64 KB, Echo 32 KB) are **cleared synchronously at `init`**, in the foreground (≈ 0.1–0.2 ms, under the 1 ms rule). Tapes, predelay and CrumpBit need no clear: their reads are gated by `valid`, CrumpBit's existing rule (`crumpBit_runReverb()` `gate`). No clear runs in the render. |
| **SD7** *(withdrawn, §S18 R1)* | A ring's valid audio is spread over the ring, so §5.3's "largest contiguous piece outside W" fails for Reverb into Reverb (W ≈ 35 KB scattered over 64 KB, plus the 73 KB seat). | `sk_feedBegin()` **packs ring sources:** each side's lines are summed **start-aligned** (oldest with oldest, the order they would have played) and halved, then copied unwrapped into one small window. That is ≤ 20.5 KB, placed at the share top, or just above the prefix when the top overlaps the source. Tape sources stay in place: one contiguous range per head. |
| **SD8** *(withdrawn, §S18 R2/R3)* | Plan §5.4 costs the feed at ≤ 18 per frame. A stereo feed with interpolated reads costs more: two heads, each with two loads, conversions and a lerp. | **Feed injection forms:** <br>• Space engines compile two loop variants from one `always_inline` body: no-feed and feed. Steady blocks run the no-feed loop at the plan's cost. The feed reads into a stack block first (`sk_feedRead()`). <br>• CrumpBit gets one post-pass, `crumpBit_feedPost()`. It adds the feed to the wet and into the cells written this block. **No CrumpBit engine loop changes**, so `crumpbit_elf_check` holds. <br>• Revised feed estimates per frame: mono ≈ 14, stereo ≈ 24, the Reverb's half-rate injection ≈ 12, the CrumpBit post-pass ≈ 20–31. <br>• The worst case is CrumpBit `grn` fed by a stereo Delay, an estimated 190–530 cycles over 17,850. D19 takes the measured remainder from Advanced (estimate: 21,635 → about 21,370–21,540). §15 measures it. |
| **SD9** | **Growth at the wrap.** If the ring length changes while the read head is behind the old end, reads that wrap land in the wrong place. | The length (and base) changes in **the block in which the write head crosses the old end**, and the write index is re-based by `old_off − new_off`. One rule serves upward and downward growth. While a growth is pending, D is held ≤ old length − 2 blocks − guard. Exception: CrumpBit `grn` (two half-lines) keeps today's rule, because a half-line cannot grow without moving the other. |
| **SD10** | An 11-tap halfband cannot meet R-6 (≤ 0.1 dB to 9 kHz, ≥ 50 dB). Measured on a Kaiser design (β 4.1): −0.1 dB at 5.5 kHz, −1 dB at 7.6 kHz, −3 dB at 9.5 kHz, 47.4 dB image rejection above 17.06 kHz. To 9 kHz within 0.1 dB at ≥ 50 dB needs about 35 taps. | Keep **11 taps** (the cost basis). **R-6 becomes:** ≤ 0.1 dB to 5 kHz, ≥ −3.5 dB at 9 kHz, ≥ 45 dB above 17 kHz. That still matches "band-limited to about 10 kHz". The 19-tap alternative (≤ 0.1 dB to 7 kHz, 54 dB) costs about +6 per frame on Reverb and Echo and would need trims. |
| **SD11** | StereoFilter has no buffer and is not in plan §5.1's coverage list. | A change into or out of StereoFilter cuts the tail, as today. The drain applies only when the outgoing type has a buffer (`export_handoff` non-NULL). |
| **SD12** | Into OFF, `effects_service()` would resolve OFF's record with the draining type's descriptors. | While draining, `effects_service()` still consumes the FX latch (`effects_seqConsume()`, `effects_seqStepFor()`). It then returns before the descriptor loop, so every row, including the return route/level/pan, stays at the outgoing Scene's values. |
| **SD13** | Plan §3.3 limits the Space union member to 32 B. | It may use up to the union's 100 B. There is no growth (asserted). |
| **SD14** | File keys must be unique per type (`effects_descriptorByKey()`). | Keys use the prefixes `dly_`, `rvb_` and `ech_`. Code uses `sk_` (kit) and `space_` (types), per D13. |

**F1–F3 as implemented:**

- **F1:** a reverb source feeds one start-aligned pass of its pre-summed
  lines per side.
- **F2:** at the drain's end the record keeps the final export with
  `FXBUF_STATE_EFFECT_WRITTEN` cleared.
- **F3:** the drain ends at −80 dBFS held for 10 ms, or at 6 s, with the
  last 500 ms faded.

---

## 1. Net resources against the current build (estimates; measured in S13)

*Superseded by §S18 (phase budgets and measured figures).*

| | Change |
|---|---|
| Flash | **≈ +18 KB** (range 14–22 KB): the plan's ≈ +16 KB plus the second loop variant of each Space engine (≈ 3–4 KB, SD8). Headroom is 62,564 B. |
| SRAM1 `.bss` | **+16 B**: `fxbuf_handoff_t` 180 → 196 B (D18, approved) |
| DTCM statics / ITCM | **0 / 0** |
| FX arena use | The first 512 B of the share become the fixed prefix (SD1). There is no new allocation; it is Effect-owned arena. |
| CPU budget | Effect 16,634 → **16,970**; Advanced 21,803 → **21,635** ×2 (D19); in a feed block, plus the locked slot (≈ 880). Any measured feed remainder comes from Advanced by D19 (SD8). |
| Stack | `sk_feedRead()`'s block, `float[2][32]` = 256 B, in the Space and CrumpBit processes, only while a feed runs (a local of `process`, not a VLA) |

---

## 2. Order and gates

The steps follow the plan's phasing: F1 is S1–S9 plus the Delay, F2 the
Reverb, F3 the Echo, F4 CrumpBit. Each step's gate must pass before the next
step starts.

| Step | What | Gate |
|---|---|---|
| **S0** | Baselines into `tools/dsp_test/baselines/s095p1/` | Files written (§S0) |
| **S1** | `FxBuffer.h/.c`: the +16 B fields, the segment contract, the reset | Builds; assert 196; host `crumpbit` unchanged |
| **S2** | `EffectsManager.h`, `EffectTypes.h`: ids 3/4/5, `init` with the share, the lock and drain API | Builds after S3 and S4 |
| **S3** | `EffectsManager.c`: registry rows, the union member, init with the share, the drain, the logical type, the lock | `make all` builds; DEV self-check code 0 |
| **S4** | `StereoFilterEffect.c`: init signature | Builds |
| **S5** | Slot lock in `modulationNode.c` and `InstrumentManager.c` | Host `modulation` with new law L5; `elf_p4_check` (mixer and oscillator loops unchanged) |
| **S6** | `Core/DSP/Effects/Space/SpaceKit.h/.c` | Host `space` laws K-1..K-9 |
| **S7** | `tools/plots/gen_space_tables.py` plus `SpaceTables.h/.c` | `space_tables` (`--check`) |
| **S8** | `SpaceParameters.h/.c` | DEV self-check 0; host `space` law P-1 |
| **S9a** | `SpaceEffect.h/.c`: the common ops plus the **Delay** | Host D-1..D-6, S-1..S-3, H-1..H-9 (Delay and OFF), L-2, O-1..O-4, O-8; `space_elf_check`; `space_cpu_check` (Delay) |
| **S9b** | **Reverb** | R-1..R-6, O-4..O-6, H-* for rings |
| **S9c** | **Echo** | E-1..E-3, O-1, O-3, O-4, O-7 |
| **S10** | **CrumpBit** (§5.6 of the plan plus SD1/SD8/SD9) | Existing CB suite (updated for the prefix) plus CB24; `crumpbit_elf_check` unchanged; `crumpbit_cpu_check` with the feed |
| **S11** | Root `Makefile` | `make all && make img` |
| **S12** | Host Makefile targets, `space_cpu.py`, break builds | `make -C tools/dsp_test all` exits 0, with every break caught |
| **S13** | Full gate run and record | §S13 |
| **S14** | Device: one run (plan §9) | §S14 |

S11 and S12 are written alongside the steps that need them: each new source
joins the root `Makefile` in the step that adds it, so `make all` builds at
every gate. They are listed separately so their full text is in one place.

---
## S0. Baselines (no source change)

Run on the current tree, before any edit, after `make all`:

```
mkdir -p tools/dsp_test/baselines/s095p1
python3 tools/diagnostic_tools/link_budget.py arm-none-eabi-nm build/lxr02.elf \
  > tools/dsp_test/baselines/s095p1/link_budget_before.txt
make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s095p1
python3 tools/dsp_test/crumpbit_cpu.py --elf build/lxr02.elf --cap 181 \
  > tools/dsp_test/baselines/s095p1/crumpbit_cpu_before.txt
make -C tools/dsp_test crumpbit_elf_check \
  > tools/dsp_test/baselines/s095p1/crumpbit_elf_before.txt
```

- **What:**
  - the flash, ITCM, DTCM and FXBUF figures;
  - the oscillator and mixer loop dumps (`osc.txt`, `mixer.txt`) that S5 and
    S13 compare against;
  - CrumpBit's per-sub-type instruction totals and loop limits.
- **Why:** S5 changes code next to inlined oscillator paths, and S10 moves
  CrumpBit's base. The before/after must be exact files in the tree, not
  `build/` (user rule).
- **Gate:** the five files exist. `crumpbit_cpu_before.txt` shows `grn`
  180.8 and `rvb` 179.4 (S094 P2 §23). If either differs, the §5.7
  arithmetic is re-done before S10.

---

## S1. `Core/DSP/Effects/FxBuffer.h` / `FxBuffer.c`: the handoff segments (+16 B, D18, D24)

### S1.1 `FxBuffer.h`: after line 120 (`#define FXBUF_STATE_VOICE_WRITTEN 0x02u`), **add**

```c
/*
 * Handoff segment sides and the unsigned Q1.15 unit (S095 P1, D24).
 *
 * What: FXBUF_SEG_SIDE_* are the 2-bit codes in effect_seg_sides, one per
 * Effect segment i (bits 2i..2i+1): the segment is a left (L), right (R) or
 * mono (BOTH, fed to both sides) part of the outgoing Effect's tail.
 * FXBUF_UQ15_ONE is 1.0 in the unsigned Q1.15 fields (effect_speed_uq15,
 * effect_dry_uq15, effect_feed_gain_uq15), so 0x8000 = 1.0 and the range is
 * 0 .. 1.99997.
 * Why: an entering type must be able to read the outgoing tail exactly,
 * without knowing the outgoing type's engine (plan §5.2, D24).
 * Accessors: every buffer-using Effect's export_handoff (writes), the Space
 * kit sk_feedBegin() (reads).
 */
#define FXBUF_SEG_SIDE_L       0u
#define FXBUF_SEG_SIDE_R       1u
#define FXBUF_SEG_SIDE_BOTH    2u
#define FXBUF_SEG_SIDE_MASK    3u
#define FXBUF_SEG_SIDE_SHIFT(i_) (2u * (uint32_t)(i_))
#define FXBUF_UQ15_ONE         0x8000u
```

### S1.2 `FxBuffer.h` lines 137–166: **modify** the handoff comment and struct

Replace the comment at 137–152 with the text below, and append the seven
fields after `unit_owner_slot` (line 165). The existing fields keep their
order and offsets, so the 180 B prefix is unchanged and the new fields land
at offsets 180–195 with no padding.

```c
/*
 * Arena handoff record between Scene switches and Effect type changes.
 *
 * What: a snapshot of how the arena was used when the previous Effect (and,
 * from Phase 7, voice buffer users) exited: the exited Effect's type, channel
 * count, bit depth and store rate; the share bounds and the twelve unit owners
 * at exit; each unit's store rate; and up to 16 read and 16 write positions
 * (arena-relative byte offsets: [0..11] voice units, [12..15] Effect).
 * Written-content flags say which regions hold real audio.
 *
 * S095 P1 (D24): the Effect slots [12..15] are SEGMENTS of the outgoing
 * Effect's valid audio, one per slot i = 0..3 at FXBUF_HANDOFF_EFFECT_POINTER_
 * BASE + i:
 * - read_offset[12 + i]:  the segment's OLDEST valid byte;
 * - write_offset[12 + i]: one past its NEWEST byte;
 * - both FXBUF_OFFSET_NONE: no segment i.
 * Read rule, the same for every segment of every type: read forward from
 * start to end, oldest first; step effect_stride bytes per sample at
 * effect_speed_uq15 x effect_rate_hz / 44,108 samples per output frame; the
 * sample is at byte lane (i x effect_bits / 8) mod effect_stride of each
 * step; a position reaching effect_wrap_offset + effect_wrap_bytes continues
 * at effect_wrap_offset; length = (end - start) mod effect_wrap_bytes. Every
 * byte inside a segment is valid audio (the exporter clamps the start to what
 * it has written); a zero-length segment is not exported. effect_seg_sides
 * says which side each segment feeds; the summed segments of one side x
 * effect_feed_gain_uq15 are the outgoing type's wet-normalized tail (before
 * its mix). effect_dry_uq15 is its dry gain at exit.
 * The record only describes: an exporter never moves or rewrites audio (the
 * same-type Scene switch exports a live runtime). Any re-ordering the entering
 * type needs is its own (the Space kit's sk_feedBegin()).
 *
 * Why: Phase 5 has no system-wide buffer disposal, even across a type change
 * (user decision F6). Buffers persist; the entering owner decides whether to
 * adopt or dispose of them. This record is the only information exchange for
 * that decision. Offsets instead of pointers keep it meaningful if the arena
 * moves between builds.
 * Size: 196 B (S095 P1: +16 B, D18), SRAM1 .bss, FxBuffer-owned.
 * Accessors: fxbuf_handoffBeginExit() (writable, at an exit),
 * fxbuf_handoff() (read-only), the types' export_handoff/init ops.
 * Affiliates: FXBUF_SEG_SIDE_*, FXBUF_UQ15_ONE, SpaceKit.h sk_feedBegin(),
 * STORAGE_SRAM_MANIFEST.md.
 */
```

New fields, after `uint8_t unit_owner_slot[FXBUF_VOICE_UNIT_COUNT];`:

```c
    /*
     * S095 P1 segment description (D24; +16 B, D18). wrap_*: the arena
     * region the segments live in (the outgoing tape, loop or ring), so a
     * reader wraps exactly where the writer did. 0 bytes = no segments.
     */
    uint32_t effect_wrap_offset;
    uint32_t effect_wrap_bytes;
    /* Read speed of the recorded audio, uQ1.15 (0x8000 = 1.0): a tape's
     * speed v at exit (varispeed, plan §4.1), so the feed keeps the recorded
     * pitch; 1.0 for CrumpBit and for rings. */
    uint16_t effect_speed_uq15;
    /* The outgoing dry gain at exit, uQ1.15: the incoming dry ramps from it
     * over 10 ms (plan §5.3 step 5). */
    uint16_t effect_dry_uq15;
    /* Gain from one side's summed segments to the outgoing wet level,
     * uQ1.15 (the outgoing type's wet-normalization constant). */
    uint16_t effect_feed_gain_uq15;
    /* Bytes from one sample of a segment to the next: 1, 2 or 4. */
    uint8_t  effect_stride;
    /* Two bits per segment: FXBUF_SEG_SIDE_L / _R / _BOTH. */
    uint8_t  effect_seg_sides;
```

### S1.3 `FxBuffer.c` line 56: **modify** the assert

`_Static_assert(sizeof(fxbuf_handoff_t) == 180u,` → `== 196u,`. The message is unchanged.

### S1.4 `FxBuffer.c` line 77: **modify** the comment

`/* Handoff record (SRAM1 .bss, 180 B); audio contents remain in the arena. */`
→ `/* Handoff record (SRAM1 .bss, 196 B since S095 P1, D18); audio contents remain in the arena. */`

### S1.5 `FxBuffer.c` lines 116–130 (`fxbuf_handoffResetAll()`): **modify** the comment only

The `memset()` already zeroes the new fields (zero bytes in the wrap
domain means no segments). Change line 116 to:

```c
/*
 * Reset every handoff field to the safe boot state: no valid audio.
 * S095 P1: the memset also clears the segment description (wrap bytes 0,
 * speed/dry/gain 0, sides 0); the loop below sets the 16 position pairs to
 * FXBUF_OFFSET_NONE, which is what "no segment" means to a reader.
 */
```

### S1.6 `FxBuffer.c` after line 416 (`h->effect_rate_hz = 0u;`, in `fxbuf_handoffBeginExit()`): **add**

```c
    /*
     * S095 P1 (D24): a fresh exit describes no segments until the outgoing
     * type's export_handoff fills them (the position loop below clears
     * slots 12..15). Without this, a type that exports none (StereoFilter,
     * OFF) would leave the previous owner's wrap domain and gains behind,
     * and an entering type would feed stale audio.
     */
    h->effect_wrap_offset = 0u;
    h->effect_wrap_bytes = 0u;
    h->effect_speed_uq15 = 0u;
    h->effect_dry_uq15 = 0u;
    h->effect_feed_gain_uq15 = 0u;
    h->effect_stride = 0u;
    h->effect_seg_sides = 0u;
```

- **Gate S1:** `make all` builds (S2–S4 build together with it).
  `fxbuf_handoffRecord` is `0xc4` bytes in `nm -S`. Host `crumpbit` is
  unchanged: it extracts the typedef, so it picks up the fields and its laws
  do not read them.

---

## S2. `EffectsManager.h` and `EffectTypes.h`

### S2.1 `EffectsManager.h` lines 61–68: **modify** the ids

```c
/*
 * Registry ids are append-only; persisted identity is the three-byte token.
 * EFFECT_TYPE_CRUMPBIT (S074): the first buffer-using type (`cbt`, 8-bit
 * data lines and an 8-bit tape delay in the FxBuffer share).
 * S095 P1: the Space types, clean counterparts to CrumpBit, sharing one
 * kit (Core/DSP/Effects/Space/): EFFECT_TYPE_DELAY (`dly`, true-stereo
 * 16-bit varispeed tape), EFFECT_TYPE_REVERB (`rvb`, half-rate plate with
 * shimmer), EFFECT_TYPE_ECHO (`ech`, a mono tape into the plate). The
 * tokens are the persisted identity (AutoSave, `.fx`, MIDI Learn); the ids
 * only index the registry.
 */
#define EFFECT_TYPE_STEREO_FILTER        1u
#define EFFECT_TYPE_CRUMPBIT             2u
#define EFFECT_TYPE_DELAY                3u
#define EFFECT_TYPE_REVERB               4u
#define EFFECT_TYPE_ECHO                 5u
#define EFFECT_TYPE_COUNT                6u
```

### S2.2 `EffectsManager.h` line 234: **modify** `init`

`void (*init)(void *rt, const fxbuf_handoff_t *handoff);` becomes:

```c
    /*
     * S095 P1 (SD2): init receives the live share, so a buffer user can
     * seat, pack an outgoing tail and clear its rings in the foreground
     * before the first render block (the manager calls it between blocks).
     */
    void (*init)(void *rt, const fxbuf_handoff_t *handoff,
                 const fx_share_t *share);
```

Add one sentence to the comment block at 217–232, after "optional hooks for
types affected by a changing FxBuffer share.": "S095 P1: init also receives
the share (SD2); export_handoff must not modify the arena (it also runs for
a live runtime on a same-type Scene switch)."

### S2.3 `EffectsManager.h` after line 625 (`const effects_common_runtime_t *effects_commonRuntime(void);`): **add**

```c
/*
 * The feed slot lock (S095 P1 plan §5.7, D19).
 *
 * What: effects_feedSlotLock() is 1 while an Effect type-change feed runs,
 * else 0; OSC_WAVE_INTERP_MAX_ACTIVE - effects_feedSlotLock() is the number
 * of interpolation slots (wavetable blends and itp fades) granted per block.
 * effects_setFeedSlotLock() is the Space kit's writer: set by sk_feedBegin()
 * at init, cleared on the feed's last block or when a share change ends the
 * feed. The manager also clears it when a type exits.
 * Why: a feed adds work to the Effect for its length (one tape pass, or one
 * pass of a reverb's lines, at most 6 s); one interpolation slot pays for it
 * (≈ 880 cycles a block), the rest comes from Advanced.
 * Storage: bit 0 of effects_state.reserved (0 B new RAM).
 * Context: foreground only (render and the type-switch path, both in the
 * main loop).
 * Accessors: modNode_interpClaim(), instrumentManager_itpFadeSlotAvailable()
 * (readers); sk_feedBegin(), sk_feedEnd() (writers).
 */
uint8_t effects_feedSlotLock(void);
void effects_setFeedSlotLock(uint8_t on);
```

### S2.4 `EffectTypes.h`: no change

`effect_record_t` and the type id width are unchanged: the plan says the
record format does not change.

---

## S3. `Core/DSP/Effects/EffectsManager.c`

### S3.1 Includes, after line 24 (`#include "CrumpBitEffect.h"`): **add**

```c
/* S095 P1: the Space types (Delay, Reverb, Echo) and their kit (the arena
 * prefix, the feed and the drain). */
#include "SpaceParameters.h"
#include "SpaceEffect.h"
#include "SpaceKit.h"
```

### S3.2 The runtime union, lines 52–61: **modify** (add a member)

After `CrumpBitRuntime crump_bit;` (line 60):

```c
    /*
     * S095 P1: the three Space types share one member (SpaceRuntime: the
     * raw rows, the type tag and the control-rate values; at most the
     * union's 100 B, SD13). Their DSP state is the 448 B state header and
     * their audio is the share above the 512 B prefix (SpaceKit.h), not
     * this union.
     */
    SpaceRuntime space;
```

### S3.3 Registry, before line 138 (`};` closing `effects_registry`): **add** three rows

```c
    /*
     * S095 P1: Delay (`dly`), Reverb (`rvb`), Echo (`ech`). Stereo in and
     * out. Every one is an elastic buffer user (D2): it seats in the whole
     * share above the arena prefix, or in the largest piece a feed leaves
     * (plan §5.3), and grows back at its next wrap. buffer_min_bytes is the
     * type's seat_min plus the prefix; buffer_pref_bytes the arena
     * (declarative, as CrumpBit's). No sub-types: one fixed lane row each
     * (plan §4, lanes 14/15 = the common vol and pan). The default layout
     * places rows 3.. on SELECT 2.. (menuEffects.c, no select_layout); each
     * type's ui hooks label its times, ratios, states and pitches (one
     * hook set per type: format_value3 receives no type id).
     */
    {
        space_delayToken3, space_delayAbbrev5, space_delayFull8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        space_delayDescriptors, SPACE_DELAY_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          SPACE_DLY_MIX, SPACE_DLY_TIME, SPACE_DLY_FEEDBACK, SPACE_DLY_TONE,
          SPACE_DLY_DEGRADE, SPACE_DLY_WOW, SPACE_DLY_XFEED, SPACE_DLY_SPREAD,
          SPACE_DLY_BRAKE, SPACE_DLY_REVERSE, SPACE_DLY_HOLD, SPACE_DLY_HEADS,
          SPACE_DLY_DUCK, EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN },
        NULL, &space_delayOps, &space_delayUi,
        (uint16_t)sizeof(SpaceRuntime),
        SPACE_DELAY_SEAT_MIN + SK_PREFIX_BYTES, SK_ARENA_PREF_BYTES
    },
    {
        space_reverbToken3, space_reverbAbbrev5, space_reverbFull8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        space_reverbDescriptors, SPACE_REVERB_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          SPACE_RVB_MIX, SPACE_RVB_DECAY, SPACE_RVB_FREEZE, SPACE_RVB_SHIMMER,
          SPACE_RVB_INTERVAL, SPACE_RVB_DETUNE, SPACE_RVB_ENV, SPACE_RVB_DUCK,
          SPACE_RVB_LOW_SHELF, SPACE_RVB_HIGH_SHELF, SPACE_RVB_PREDELAY,
          SPACE_RVB_MOD, SPACE_RVB_WIDTH,
          EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN },
        NULL, &space_reverbOps, &space_reverbUi,
        (uint16_t)sizeof(SpaceRuntime),
        SPACE_REVERB_SEAT_MIN + SK_PREFIX_BYTES, SK_ARENA_PREF_BYTES
    },
    {
        space_echoToken3, space_echoAbbrev5, space_echoFull8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        space_echoDescriptors, SPACE_ECHO_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          SPACE_ECH_MIX, SPACE_ECH_TIME, SPACE_ECH_FEEDBACK, SPACE_ECH_TONE,
          SPACE_ECH_DEGRADE, SPACE_ECH_WOW, SPACE_ECH_TO_REVERB,
          SPACE_ECH_BLEND, SPACE_ECH_DECAY, SPACE_ECH_BRAKE, SPACE_ECH_HEADS,
          SPACE_ECH_DUCK, SPACE_ECH_ROUTING,
          EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN },
        NULL, &space_echoOps, &space_echoUi,
        (uint16_t)sizeof(SpaceRuntime),
        SPACE_ECHO_SEAT_MIN + SK_PREFIX_BYTES, SK_ARENA_PREF_BYTES
    },
```

The self-check (`effects_registrySelfCheck()`, line 1752) applies
unchanged:

- code 5: lane 0 is the Morph source;
- code 6: lanes are unique and in range;
- code 3: automatable rows are below index 63;
- code 7: defaults lie inside [min, max].

The three rows have `ops->lane_map` NULL, so `variants` is 0 and the
fixed `lanes[]` are checked.

### S3.4 After line 142 (the CrumpBit size assert): **add**

```c
/* S095 P1: the Space member fits the union; the union itself stays 100 B
 * (the assert at line 154 is unchanged). */
_Static_assert(sizeof(SpaceRuntime) <= sizeof(effects_runtime_t),
               "SpaceRuntime exceeds the Effect runtime union");
```

### S3.5 `effects_state_t` comment, lines 160–170: **modify** one sentence

"The spare `reserved` byte keeps the layout; the former held Morph latch was
removed (S081 §5.3, one-step Morph lane)." becomes:

```c
 * `reserved` (S095 P1) holds two flags and no longer pads: bit 0
 * EFFECTS_STATE_FEED_LOCK (one interpolation slot locked while a type-change
 * feed runs, plan §5.7) and bit 1 EFFECTS_STATE_DRAIN (the runtime type is
 * draining its tail into OFF, plan §5.5). The former held Morph latch was
 * removed (S081 §5.3, one-step Morph lane).
```

Add after line 186 (the closing `_Static_assert` of the scale contract):

```c
/*
 * effects_state.reserved flags (S095 P1).
 *
 * EFFECTS_STATE_FEED_LOCK: set by sk_feedBegin() (through
 * effects_setFeedSlotLock()) when an entering type starts a feed; cleared on
 * the feed's last block, when a share change ends the feed, and whenever a
 * type exits (effects_switchRuntime()).
 * EFFECTS_STATE_DRAIN: set when a buffer-using type is switched to OFF
 * (D25); while it is set, runtime_type is the draining type, the logical
 * type is OFF (effects_logicalType()), effects_service() resolves no rows,
 * and effects_process() runs the draining type on a muted input under the
 * drain envelope. Cleared at the drain's end, by a switch back to the
 * draining type, or by a switch to another type.
 */
#define EFFECTS_STATE_FEED_LOCK   0x01u
#define EFFECTS_STATE_DRAIN       0x02u
```

### S3.6 New helpers, after `effects_runtimeMember()` (lines 1846–1850): **add**

```c
/*
 * Drain and logical-type helpers (S095 P1 plan §5.5, D25; SD4).
 *
 * effects_draining(): 1 while the runtime type drains into OFF.
 * effects_logicalType(): the type the user and the record see, OFF while
 * draining, else the runtime type. Every reader outside this file
 * (effects_activeType(): menu, MIDI Learn) and the rebind test use it, so a
 * draining tail is never offered the draining type's rows.
 */
static inline uint8_t effects_draining(void)
{
    return (uint8_t)((effects_state.reserved & EFFECTS_STATE_DRAIN) != 0u);
}

static inline effect_type_id_t effects_logicalType(void)
{
    return effects_draining() ? (effect_type_id_t)EFFECT_TYPE_OFF
                              : effects_state.runtime_type;
}

uint8_t effects_feedSlotLock(void)
{
    return (uint8_t)(effects_state.reserved & EFFECTS_STATE_FEED_LOCK);
}

void effects_setFeedSlotLock(uint8_t on)
{
    if (on)
        effects_state.reserved |= EFFECTS_STATE_FEED_LOCK;
    else
        effects_state.reserved &= (uint8_t)~EFFECTS_STATE_FEED_LOCK;
}
```

### S3.7 `effects_exportHandoff()`, lines 1864–1876: **modify** to return the record

```c
/*
 * (comment block at 1852-1863 unchanged, plus:)
 * S095 P1: returns the record just written, so the drain's end can clear
 * FXBUF_STATE_EFFECT_WRITTEN on it (F2: a tail heard to its end is not fed
 * again). export_handoff only describes (SD3).
 */
static fxbuf_handoff_t *effects_exportHandoff(void)
{
    ... body unchanged ...
    return handoff;
}
```

The two existing callers (lines 1894 and 1958) ignore the result
(`(void)effects_exportHandoff();`).

### S3.8 `effects_switchRuntime()`, lines 1885–1900: **replace** the body

```c
/*
 * Switch the live Effect runtime to another type (S074; S095 P1 drain and
 * share).
 *
 * What, in order:
 * 1. an unknown id becomes OFF;
 * 2. while draining (D25): a switch back to the draining type cancels the
 *    drain and keeps the runtime and its tail (as a same-type Scene switch);
 *    a switch to OFF again leaves the drain running;
 * 3. into OFF from a type that has a buffer (export_handoff non-NULL, SD11)
 *    and is not already OFF: the drain starts (sk_drainBegin() resets its
 *    counters in the arena control block) and nothing else changes; the
 *    runtime keeps rendering on a muted input until its tail ends;
 * 4. otherwise the outgoing type (or the draining one) exports its live
 *    handoff, both reserved flags clear (its feed and drain end with it), the
 *    union is zeroed, and the incoming type's init receives the handoff and
 *    the share (SD2), so it can seat, pack the old tail and clear its rings
 *    before the next block.
 * Why: plan §5 (feed-through, D20-D22) and §5.5 (drain, D25); the order keeps
 * the handoff a description of the old arena at the moment init reads it.
 * Inputs: incoming type id. Outputs: effects_runtime, effects_state.
 * Callers: effects_activateScene(), effects_changeTypeScene().
 * Affiliates: sk_drainBegin(), effects_drainProcess(), fxbuf_handoff().
 */
static void effects_switchRuntime(effect_type_id_t incoming)
{
    const effect_registry_entry_t *new_entry = effects_registryEntry(incoming);
    const effect_registry_entry_t *old_entry =
        effects_registryEntry(effects_state.runtime_type);
    fx_share_t share;

    if (!new_entry) {
        incoming = EFFECT_TYPE_OFF;
        new_entry = effects_registryEntry(EFFECT_TYPE_OFF);
    }
    fxbuf_effectShare(&share);
    if (effects_draining()) {
        if (incoming == effects_state.runtime_type) {
            /* Back to the draining type: its runtime and tail stay live. */
            effects_state.reserved &= (uint8_t)~EFFECTS_STATE_DRAIN;
            effects_state.force_all = 1u;
            return;
        }
        if (incoming == EFFECT_TYPE_OFF)
            return;   /* still OFF: the drain carries on */
    } else if (incoming == EFFECT_TYPE_OFF &&
               effects_state.runtime_type != EFFECT_TYPE_OFF &&
               old_entry && old_entry->ops &&
               old_entry->ops->export_handoff &&
               sk_drainBegin(&share, effects_state.runtime_type)) {
        effects_state.reserved |= EFFECTS_STATE_DRAIN;
        return;
    }
    /* The outgoing type describes its arena use before the union clears. */
    (void)effects_exportHandoff();
    effects_state.reserved &=
        (uint8_t)~(EFFECTS_STATE_DRAIN | EFFECTS_STATE_FEED_LOCK);
    memset(&effects_runtime, 0, sizeof(effects_runtime));
    effects_state.runtime_type = incoming;
    if (new_entry && new_entry->ops && new_entry->ops->init)
        new_entry->ops->init(effects_runtimeMember(), fxbuf_handoff(), &share);
    effects_state.force_all = 1u;
}
```

### S3.9 `effects_activateScene()` line 1958 (the same-type `effects_exportHandoff();`): **modify**

```c
        /*
         * S095 P1: a Scene of the draining type ends the drain (the tail
         * keeps ringing into the new Scene's settings, as any same-type
         * switch). A Scene whose type is OFF reaches effects_switchRuntime()
         * instead, which leaves the drain running.
         */
        effects_state.reserved &= (uint8_t)~EFFECTS_STATE_DRAIN;
        (void)effects_exportHandoff();
```

**Note:** while draining type X, a Scene of type X compares equal to
`runtime_type` at line 1949. It takes this branch, which is the intended
"back to the draining type".

### S3.10 `effects_rebindSourceScene()` line 2006: **modify**

`if (type != effects_state.runtime_type)` → `if (type != effects_logicalType())`.

Comment, added above it:

```c
    /* S095 P1: while draining, the logical type (OFF) is what the Scene
     * record must match; the draining runtime is left untouched. */
```

### S3.11 `effects_service()` after line 2097 (`active_step = effects_seqStepFor(record);`): **add**

```c
    /*
     * S095 P1 (D25, SD12): while draining, the record is OFF's and its rows
     * mean nothing to the draining type. The FX latch above is still
     * consumed (the FX clock stays aligned, MEMORY: FX sequence = master
     * clock), then every row, including the return route/level/pan, stays
     * at the outgoing Scene's values. This also skips the descriptor loop's
     * cost (≈ 142 cycles a row) for the drain's length.
     */
    if (effects_draining()) {
        effects_state.force_all = 0u;
        return;
    }
```

### S3.12 `effects_process()`, lines 2194–2203: **replace**

```c
/*
 * Process one Effect block (S095 P1: with the drain).
 *
 * What: the live type's process on the mixer's normalized bus. While
 * draining (D25), effects_drainProcess() zeroes the input first, runs the
 * draining type, scales its output by the drain envelope and ends the drain
 * when the tail is done: -80 dBFS held for 10 ms, or the 6 s ceiling (D16,
 * F3).
 * Why: into OFF the outgoing type keeps rendering its own exact tail; no
 * emulation (plan §5.5).
 * Inputs: io (the mixer's bus, the share). Output: io in place.
 * Caller: mixer_calcNextSampleBlock() after effects_service().
 */
void effects_process(effect_io_t *io)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    if (!entry || !entry->ops || !entry->ops->process || !io)
        return;
    if (effects_draining()) {
        effects_drainProcess(entry, io);
        return;
    }
    entry->ops->process(effects_runtimeMember(), io);
}
```

`effects_drainProcess()` is new, added immediately above `effects_process()`:

```c
/*
 * One drain block (S095 P1 plan §5.5, D25, F2, F3).
 *
 * What:
 * 1. zero the block's input (both bus channels): the draining type hears
 *    silence, so it plays only what is already in its buffer;
 * 2. run the draining type's process at its frozen rows;
 * 3. sk_drainStep() scales the output by the envelope (1.0 until the last
 *    500 ms of the 6 s ceiling, then linear to 0) and returns 1 when the
 *    drain is over (the block peak stayed below -80 dBFS for 14 blocks, or
 *    the ceiling was reached);
 * 4. at the end: export the final handoff, clear FXBUF_STATE_EFFECT_WRITTEN
 *    (F2: no later feed of a tail already heard), zero the union and make
 *    the runtime OFF; effects_activeIoFlags() then returns 0 and the mixer
 *    drops the bus from the next block.
 * Why: an exact tail costs exactly the draining type's own budget, which
 * it already fits (the skipped descriptor loop frees ≈ 2,000-3,000 more).
 * Cost: the zeroing (64 stores) plus sk_drainStep() (≈ 6 per frame: two
 * multiplies, two abs/max) per block.
 * Inputs: the draining entry, io. Outputs: io, effects_state, the handoff.
 * Caller: effects_process(). Affiliates: sk_drainBegin(), sk_drainStep().
 */
static __attribute__((noinline)) void effects_drainProcess(
    const effect_registry_entry_t *entry, effect_io_t *io)
{
    uint32_t n;

    for (n = 0u; n < io->frames; n++) {
        io->l[n] = 0.0f;
        if (io->r)
            io->r[n] = 0.0f;
    }
    entry->ops->process(effects_runtimeMember(), io);
    if (sk_drainStep(io)) {
        fxbuf_handoff_t *h = effects_exportHandoff();

        h->state_flags &= (uint8_t)~FXBUF_STATE_EFFECT_WRITTEN;
        effects_state.reserved = 0u;
        memset(&effects_runtime, 0, sizeof(effects_runtime));
        effects_state.runtime_type = EFFECT_TYPE_OFF;
        effects_state.force_all = 1u;
    }
}
```

**Note:** `effects_state.reserved = 0u` also clears a feed lock that the
draining type may still hold, because its feed ends with it.

### S3.13 `effects_activeType()`, lines 2205–2208: **modify**

```c
/* S095 P1 (SD4): the logical type, OFF while a drain runs. */
effect_type_id_t effects_activeType(void)
{
    return effects_logicalType();
}
```

`effects_activeIoFlags()` (lines 2210–2216) is **unchanged**. While
draining, `runtime_type` is the draining type, so it already reports that
type's io flags (plan §5.5). Add this comment above it: `/* S095 P1: while
draining, runtime_type is the draining type, so its io flags keep the
mixer's return path until the drain ends. */`

- **Gate S3** (with S1, S2, S4 and the S6–S9 stubs needed to link):
  `make all` builds. With `DEV_MODE_DIAGNOSTIC`,
  `effects_registryCheckResult()` is 0.

---

## S4. `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c`: init signature

Line 41 (`static void stereoFilter_init(void *rt_void, const fxbuf_handoff_t
*handoff)`): **modify** its parameter list to `(void *rt, const fxbuf_handoff_t *handoff,
const fx_share_t *share)` and add `(void)share;` after the existing
`(void)handoff;` (line 45). Comment:

```c
/* S095 P1 (SD2): the share is unused; StereoFilter has no buffer, so a
 * change into or out of it cuts any tail, as before (SD11). */
```

---

## S5. The slot lock (plan §5.7, D19)

### S5.1 `Core/DSPAudio/modulationNode.c` after line 54: **add** the include

```c
#include "EffectsManager.h"   /* S095 P1: effects_feedSlotLock() (plan §5.7) */
```

### S5.2 `modulationNode.c` lines 135–158 (`modNode_interpClaim()`): **modify**

Add this to the comment block at 114–134, after "S088's run mode shares the
slots.":

```c
 *             S095 P1 (plan §5.7, D19): while an Effect type-change feed
 *             runs, effects_feedSlotLock() is 1 and both grant paths use
 *             limit = OSC_WAVE_INTERP_MAX_ACTIVE - 1: a holder from last
 *             block keeps its slot only while fewer than `limit` are granted
 *             this block, and a newcomer only when grants plus unclaimed
 *             reservations stay under `limit`. The block-start reserve alone
 *             cannot do this: the sticky path consumes it, so two holders of
 *             the last block would keep both slots.
```

Body changes:

```c
	OscInfo* osc = (OscInfo*)oscp;
	const uint8_t stamp = modNode_interpStampOf(modNode_waveInterpGeneration);
	/* limit: slots this block may grant (2, or 1 while a feed runs). */
	const uint8_t limit =
		(uint8_t)(OSC_WAVE_INTERP_MAX_ACTIVE - effects_feedSlotLock());
	...
	if (osc->interpStamp == modNode_interpLastStamp &&
	    modNode_interpReserved != 0u &&
	    modNode_waveInterpActiveCount < limit) {
	...
	if ((uint16_t)modNode_waveInterpActiveCount + modNode_interpReserved <
	    limit) {
```

### S5.3 `Core/DSP/Instruments/InstrumentManager.c` line 2036: **modify**

```c
    /*
     * S095 P1 (plan §5.7): one slot fewer while an Effect feed runs; a fade
     * already running keeps its slot until it ends (<= 132 samples), the
     * accepted residual of D19.
     */
    return (uint8_t)(count < (uint8_t)(OSC_WAVE_INTERP_MAX_ACTIVE -
                                       effects_feedSlotLock()));
```

`EffectsManager.h` is already included (line 11).

### S5.4 `config.h` line 961: **modify** the comment only

Add above `#define OSC_WAVE_INTERP_MAX_ACTIVE 2u`: `/* S095 P1: one of the two
is locked while an Effect type-change feed runs
(effects_feedSlotLock()). */`

### S5.5 Host law L5, `tools/dsp_test/modules/modulation.c`

- **Add** a stub before the generated include:
  `static uint8_t mod_feed_lock; uint8_t effects_feedSlotLock(void) { return mod_feed_lock; }`
- **Add** a law after L4:

```c
    /*
     * L5 (S095 P1, plan L-1): with the feed lock set, at most one slot is
     * granted per block. Two holders of the last block: the first keeps its
     * slot and the second is refused; a newcomer is refused; with the lock
     * cleared the next block grants two again.
     */
```

The law runs `modNode_resetTargets()` between blocks, as L1–L4 do. Its
expects:

- lock set: holder a is granted, holder b refused, newcomer c refused;
- lock cleared: a and b are both granted.

- **Gate S5:**
  - `make -C tools/dsp_test modulation` passes L1–L5;
  - after `make all`, `make -C tools/dsp_test elf_p4_check
    P4BASE=baselines/s095p1` passes: no oscillator loop changed, and no
    mixer hot loop grew. The lock is read once per claim, outside every
    loop; LTO inlines the getter.

---
## S6. `Core/DSP/Effects/Space/SpaceKit.h` and `SpaceKit.c` (new)

Both files are **new**. Use the repository's file header (copyright and
licence block) as in `EffectTypes.h` lines 1–34. `SpaceKit.c` is a DSP
source: it carries the feed loops, so it goes into `DSP_SRCS` with an
explicit `-Ofast` rule (S11).

### S6.1 `SpaceKit.h`: module comment and includes

```c
/*
 * Space kit (S095 P1): what every FX buffer user shares.
 *
 * What:
 * - the arena prefix: the first SK_PREFIX_BYTES (512 B) of the Effect share,
 *   reserved by every buffer-using type (CrumpBit, Delay, Reverb, Echo).
 *   Bytes 0..63 are the control block sk_ctl_t (the seat, the type-change
 *   feed, the drain); bytes 64..511 are the live type's state header (the
 *   Space types' filters, LFOs, shifters, halfband; CrumpBit uses none). No
 *   type writes audio below offset 512, so the prefix is never part of an
 *   outgoing tail (SD1) and it never moves;
 * - static-inline DSP primitives (rings, reads, one-poles, shelf, SVF tone,
 *   soft clip, DC block, LFO, halfband, shifter, fast exp2), all float, no
 *   division, libm call or double (plan §3.1);
 * - the elastic seat (plan §3.3): seat in a span, grow at the next wrap,
 *   keep the newest on a shrink (sk_growAtWrap(), sk_shrinkKeep());
 * - the type-change feed (plan §5, D20-D24): read the handoff, pack ring
 *   sources, choose the span outside the feed window W, read the old tail
 *   at its own speed (sk_feedBegin(), sk_feedRead());
 * - the drain into OFF (plan §5.5, D25): sk_drainBegin(), sk_drainStep();
 * - the export helpers that write the D24 segment description.
 * Why one kit: the three Space types and CrumpBit must hand over to each
 * other in one format, seat by one rule and pay for the feed the same way
 * (plan §2.2).
 * Context: foreground only. Init, export and share changes run between
 * render blocks; the per-block functions run inside effects_process() in
 * the main loop.
 * Accessors: SpaceEffect.c, CrumpBitEffect.c, EffectsManager.c (drain).
 * Affiliates: FxBuffer.h (the handoff contract), plan
 * S095_P1_FX_DELAY_RVB_ECHO.md §3, §5; this schedule SD1-SD9.
 */
#ifndef SPACE_KIT_H_
#define SPACE_KIT_H_

#include <stdint.h>
#include "EffectsManager.h"
#include "FxBuffer.h"
```

### S6.2 `SpaceKit.h`: constants

```c
/*
 * Rates (plan §2.3): tapes and the dry path at the codec rate; the plate
 * tank at half of it, after the halfband decimator.
 */
#define SK_FS                  44108.0f
#define SK_FS_HALF             22054.0f
#define SK_FRAMES_MAX          32u     /* OUTPUT_DMA_SIZE, one Effect block */

/*
 * The arena prefix (SD1): control block + type state header. 512 B is a
 * multiple of FXBUF_ALIGN_BYTES; every span starts at or above it.
 */
#define SK_PREFIX_BYTES        512u
#define SK_CTL_BYTES           64u
#define SK_STATE_BYTES         (SK_PREFIX_BYTES - SK_CTL_BYTES)   /* 448 */

/*
 * The arena this build links (link_budget.py: FXBUF 126,656 B). Declarative
 * buffer_pref_bytes of the buffer users (no code sizes a buffer from it;
 * every seat uses the live share).
 */
#define SK_ARENA_PREF_BYTES    126656u

/* Control-block owner stamp: 'S','K' in the high half, the type id low. */
#define SK_CTL_MAGIC           0x534B0000u
#define SK_CTL_MAGIC_MASK      0xFFFF0000u

/* Feed limits (plan §5.3, D16): the 20 ms end fade, the 6 s ceiling. */
#define SK_FEED_FADE_MAX       882u      /* samples at 44,108 Hz */
#define SK_FEED_CEIL_FRAMES    264648u   /* 6 s of output frames */
/* A ring source's packed feed window (SD7): two sides of one plate line,
 * 2 x 4,950 x 2 B, or two CrumpBit `rvb` lanes, 2 x 5,133 B, fit. */
#define SK_FEED_PACK_MAX       20480u

/* The incoming dry ramp (plan §5.3 step 5): 10 ms = 14 blocks of 32. */
#define SK_DRY_RAMP_BLOCKS     14u

/* Drain (D25, F3): -80 dBFS held 10 ms, or 6 s with a 500 ms final fade. */
#define SK_DRAIN_QUIET         1.0e-4f
#define SK_DRAIN_QUIET_BLOCKS  14u
#define SK_DRAIN_BLOCKS        8270u     /* 6 s / 32 frames */
#define SK_DRAIN_FADE_BLOCKS   689u      /* 500 ms / 32 frames */

/*
 * The tape glide (plan §4.1: one-pole, ≈ 150 ms, CrumpBit's
 * CRUMPBIT_GLIDE_K = 1.5113e-4 per sample) per 32-frame block:
 * 1 - (1 - k)^32. Per-block targets then ramp linearly inside the block.
 */
#define SK_GLIDE_K32           0.0048252f
```

### S6.3 `SpaceKit.h`: the control block

```c
/*
 * The arena control block (SD1), share offset 0, 64 B, DTCM.
 *
 * What: everything a buffer user must keep across blocks that its 100 B
 * runtime cannot hold:
 * - magic: SK_CTL_MAGIC | the owner's Effect type id, written by the
 *   owner's init; sk_ctlFor() trusts the block only for that owner, so a
 *   stale block left by an earlier type is never read as live;
 * - span_off/span_bytes: the live seat, share-relative, 32-byte aligned,
 *   always at or above SK_PREFIX_BYTES (plan §3.3); the whole usable share
 *   outside a feed, the largest piece outside W during one;
 * - the feed (plan §5.4): wrap_off/wrap_bytes (where the read wraps; the
 *   packed window for ring sources), pos[s] (the next byte of side s),
 *   left[s] (samples still to read), lane[s], side[s], bits, stride; frac
 *   (the shared sub-sample phase), step (source samples per output frame:
 *   speed x rate / 44,108), gain (the wet-normalization gain, including the
 *   pack's 2^shift and the mono fold's 1/2), fade[s] (end-fade length in
 *   samples, 0 for an uncut tape); feeding bit s = side s still running;
 * - drain_blocks/drain_quiet: the drain envelope's age and its quiet count;
 * - dry_from_q7/dry_blocks: the incoming dry ramp's origin and blocks left.
 * Why in the arena: CrumpBit's runtime is full (100 B asserted), the union
 * cannot grow, and the record must stay a pure description (SD3). The
 * arena is Effect-owned DTCM: zero-wait, no new RAM.
 * Lifetime: written by the entering type's init (sk_feedBegin()); updated
 * by its process and buffer_changed; rewritten by the next type's init.
 * Accessors: sk_feedBegin(), sk_feedRead(), sk_feedCut(), sk_spanFor(),
 * sk_drainBegin(), sk_drainStep(), sk_dryRamp().
 */
typedef struct {
    uint32_t magic;
    uint32_t span_off;
    uint32_t span_bytes;
    uint32_t wrap_off;
    uint32_t wrap_bytes;
    uint32_t pos[2];
    uint32_t left[2];
    float    frac;
    float    step;
    float    gain;
    uint16_t fade[2];
    uint16_t drain_blocks;
    uint8_t  drain_quiet;
    uint8_t  feeding;
    uint8_t  bits;
    uint8_t  stride;
    uint8_t  lane[2];
    uint8_t  side[2];
    uint8_t  dry_blocks;
    uint8_t  dry_from_q7;
} sk_ctl_t;

_Static_assert(sizeof(sk_ctl_t) == SK_CTL_BYTES,
               "the arena control block is 64 B (SD1)");
_Static_assert((SK_PREFIX_BYTES % FXBUF_ALIGN_BYTES) == 0u,
               "spans above the prefix stay 32-byte aligned");

/*
 * One block of fed audio (SD8): l and r at the output rate, or the first
 * frames/2 entries at the half rate (sk_feedRead(..., half = 1)). A
 * process's local (256 B of stack, never static); mono incomings use l.
 */
typedef struct {
    float l[SK_FRAMES_MAX];
    float r[SK_FRAMES_MAX];
} sk_feedBlock_t;

/* The control block of a share (the arena bottom; the share starts at 0). */
static inline sk_ctl_t *sk_ctl(const fx_share_t *share)
{
    return (sk_ctl_t *)(void *)share->base;
}

/* The type state header (bytes 64..511), for the Space types' unions. */
static inline void *sk_state(const fx_share_t *share)
{
    return (void *)(share->base + SK_CTL_BYTES);
}
```

### S6.4 `SpaceKit.h`: primitives (static inline)

Each primitive is short. The text below is the code to write, with its
comments.

```c
/*
 * One-pole low-pass s += k (x - s) (2 instructions). k = 1 - exp(-2 pi fc /
 * fs) from a table, so no exp at run time. Used by the head LPs, the
 * bandwidth LP, the plate damping and the envelope followers.
 */
static inline float sk_onePole(float *s, float x, float k)
{
    *s += k * (x - *s);
    return *s;
}

/*
 * Two-band shelf (plan §3.1, D7; 5 instructions): lp = one-pole(x);
 * y = g_lo lp + g_hi (x - lp). Inside the plate loop it sets the decay of
 * the low and high bands separately (lsh, hsh), with the corner from
 * lsf/hsf through k.
 */
static inline float sk_shelf2(float *s, float x, float k, float g_lo,
                              float g_hi)
{
    const float lp = sk_onePole(s, x, k);
    return g_lo * lp + g_hi * (x - lp);
}

/*
 * The tape tone SVF (plan §3.1, ≈ 12 per channel): the linear TPT
 * state-variable filter, one coefficient set per block from
 * space_toneCoef[ton] (a1 a2 a3, then the output mix m0 m1 m2: LP below 63,
 * flat at 63 (m0 = 1, m1 = m2 = 0, bit-exact pass), HP above; Q ≈ 0.8, a
 * slight head bump). No branch per frame: every `ton` is the same cost.
 *   v3 = x - s2
 *   v1 = a1 s1 + a2 v3          (band-pass)
 *   v2 = s2 + a2 s1 + a3 v3     (low-pass)
 *   s1 = 2 v1 - s1;  s2 = 2 v2 - s2
 *   y  = m0 x + m1 v1 + m2 v2
 */
typedef struct { float a1, a2, a3, m0, m1, m2; } sk_svfCoef_t;
static inline float sk_svfTone(float *s1, float *s2, float x,
                               const sk_svfCoef_t *c)
{
    const float v3 = x - *s2;
    const float v1 = c->a1 * *s1 + c->a2 * v3;
    const float v2 = *s2 + c->a2 * *s1 + c->a3 * v3;

    *s1 = 2.0f * v1 - *s1;
    *s2 = 2.0f * v2 - *s2;
    return c->m0 * x + c->m1 * v1 + c->m2 * v2;
}

/*
 * Soft clip 1.5 x - 0.5 x^3 on x clamped to +-1 (5 instructions): unity
 * slope at 0, a smooth knee, exactly +-1 at the rails, so a loop with
 * feedback above unity (fbk up to 1.20, D4) settles at a bounded level.
 */
static inline float sk_softClip(float x)
{
    x = (x > 1.0f) ? 1.0f : ((x < -1.0f) ? -1.0f : x);
    return x * (1.5f - 0.5f * x * x);
}

/* DC block y = x - x1 + r y1, r = 0.99857 (≈ 10 Hz at 44,108 Hz). */
static inline float sk_dcBlock(float *x1, float *y1, float x)
{
    const float y = x - *x1 + 0.99857f * *y1;
    *x1 = x;
    *y1 = y;
    return y;
}

/*
 * Linear and 4-point Hermite (Catmull-Rom) reads of four neighbours
 * y_1 y0 y1 y2 at fraction f in [0, 1) (≈ 10 / ≈ 20 with the loads):
 *   c1 = (y1 - y_1)/2;  c2 = y_1 - 2.5 y0 + 2 y1 - y2/2;
 *   c3 = (y2 - y_1)/2 + 1.5 (y0 - y1);  y = ((c3 f + c2) f + c1) f + y0.
 * The caller loads the neighbours (int16 to float with VCVT #15).
 */
static inline float sk_lerp(float y0, float y1, float f)
{
    return y0 + (y1 - y0) * f;
}
static inline float sk_hermite(float ym1, float y0, float y1, float y2,
                               float f)
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

/*
 * int16 sample conversions (plan §3.1): store with saturation (SSAT after
 * x 32768, so int16 never wraps; the ARM form is VCVT.S32.F32 #15 + SSAT),
 * load as code / 32,768.
 */
static inline int16_t sk_toS16(float x)
{
    const int32_t v = (int32_t)(x * 32768.0f);
    return (int16_t)__SSAT(v, 16);
}
static inline float sk_fromS16(int16_t v)
{
    return (float)v * (1.0f / 32768.0f);
}

/*
 * Power-of-two ring index (FV-1 style, plan §3.1; SD7: the pointer counts
 * UP and every line is read `off` samples behind it): (p - off) & mask.
 * Counting up makes every line's content read forward, oldest first,
 * which is the handoff read rule (D24), so the Reverb exports its lines
 * without re-ordering.
 */
static inline uint32_t sk_ringIdx(uint32_t p, uint32_t off, uint32_t mask)
{
    return (p - off) & mask;
}

/*
 * Fast 2^x for x <= 0 (per block only; no exp2f): the integer part goes
 * into the exponent, a cubic covers the fraction (|rel err| < 1e-4; RT60
 * and gain laws). The CrumpBit twin is crumpBit_exp2Neg(), file-static.
 */
static inline float sk_exp2Neg(float x)
{
    union { float f; int32_t i; } u;
    float xi;
    float f;

    x = (x < -126.0f) ? -126.0f : x;
    xi = (float)(int32_t)x - ((x < (float)(int32_t)x) ? 1.0f : 0.0f);
    f = x - xi;
    u.f = 1.0f + f * (0.6951786f + f * (0.2261424f + f * 0.0781454f));
    u.i += (int32_t)xi << 23;
    return u.f;
}

/*
 * Envelope follower (dck, env; 3 instructions): e += (|x| > e ? ka : kr)
 * (|x| - e), attack 2 ms and release 200 ms for the ducker (plan O1); the
 * Reverb's gate/bloom uses a fast and a slow one.
 */
static inline float sk_envFollow(float *e, float x, float ka, float kr)
{
    const float a = (x < 0.0f) ? -x : x;
    *e += ((a > *e) ? ka : kr) * (a - *e);
    return *e;
}
```

The **halfband** (`sk_hb*`) and the **shifter** (`sk_shifter*`) have
state, so they are a struct plus two inline functions each:

```c
/*
 * 11-tap halfband (plan §2.3, D6; SD10): taps h[0] = 0.5, h[+-1] = c1,
 * h[+-3] = c3, h[+-5] = c5, the even ones 0 (SPACE_HB_C1/C3/C5 from
 * gen_space_tables.py, Kaiser beta 4.1, normalised so 2 (c1 + c3 + c5) =
 * 0.5 and DC is unity).
 * Decimator, one output per two inputs x (oldest first), delayed 5 inputs:
 *   y[m] = c5 (x[2m] + x[2m-10]) + c3 (x[2m-2] + x[2m-8])
 *        + c1 (x[2m-4] + x[2m-6]) + 0.5 x[2m-5]
 * State: e[0..5] the last six even inputs, o[0..2] the last three odd ones.
 * Interpolator, two outputs per input u (gain 2 for the zero-stuffing):
 *   z[2m]   = 2 (c5 (u[m] + u[m-5]) + c3 (u[m-1] + u[m-4])
 *              + c1 (u[m-2] + u[m-3]))
 *   z[2m+1] = u[m-2]           (the centre tap, a pure delay)
 * State: u[0..5] per channel. Cost ≈ 6 per input frame down, ≈ 7 per
 * output channel up (plan §3.1).
 * Measured response (S095 review, numpy Kaiser): -0.1 dB at 5.5 kHz, -1 dB
 * at 7.6 kHz, -3 dB at 9.5 kHz, images below -47 dB above 17.06 kHz.
 */
typedef struct { float e[6]; float o[3]; } sk_hbDown_t;   /* 36 B */
typedef struct { float u[6]; } sk_hbUp_t;                 /* 24 B */

/*
 * Two-head rotating pitch shifter (plan §3.1, CrumpBit `pit`'s law in
 * float, Hermite reads): delay d = phase x W, phase advances by
 * (1 - ratio) / W per sample and wraps in [0, 1); head B reads at
 * phase + 1/2. Each head's gain is the triangle 1 - |2 phase - 1|, so the
 * two sum to 1 at every phase (no level dip). A ratio of -1 reads backwards
 * through the window (the Delay's `rev`, O4).
 * State: phase. Per block the caller supplies W and the ratio's
 * per-sample phase step (no division per frame).
 */
typedef struct { float phase; } sk_shifter_t;
```

The halfband and shifter **step functions** are inline and follow the
equations in their comments (decimate:
`sk_hbDownPush2(&st, x0, x1)` returns y; interpolate:
`sk_hbUpPush(&st, u, &z0, &z1)`; shift:
`sk_shiftRead(&st, ring, mask, wp, W, step)` returns the crossfaded
Hermite read). The host law **K-6** checks them against direct
convolution, and R-6 checks the halfband response.

### S6.5 `SpaceKit.h`: the kit API

```c
/*
 * The live span of a buffer user (plan §3.3).
 * What: the share-relative offset and length of the owner's seat: the
 * control block's span when the block is owned by `owner` (a feed may have
 * seated it in a sub-span), else the whole usable share [SK_PREFIX_BYTES,
 * share->bytes) (no live owner: a page label for a type that is not live,
 * a host test without init). 0 bytes when the share is smaller than the
 * prefix.
 * Accessors: every buffer user's process, effective_max and labels.
 */
uint32_t sk_spanBytes(const fx_share_t *share, uint8_t owner);
uint32_t sk_spanOffset(const fx_share_t *share, uint8_t owner);

/*
 * Begin life in the arena (plan §5.3, D24, SD3, SD6, SD7).
 *
 * What, at the entering type's init (foreground, between blocks):
 * 1. no feed when the handoff has no written Effect audio
 *    (FXBUF_STATE_EFFECT_WRITTEN clear), no segment, or wrap bytes 0: the
 *    span is the whole usable share;
 * 2. otherwise collect the segments (start, end, side, lane), fold the
 *    sides to the incoming shape (stereo: L <- L and BOTH, R <- R and BOTH;
 *    mono: one side <- everything), and clamp each segment to the 6 s
 *    ceiling at its read step;
 * 3. pack (SD7) when a folded side has more than one segment and the packed
 *    window (the longest segment of each side, bits/8 bytes a sample) fits
 *    SK_FEED_PACK_MAX: sum the side's segments START-ALIGNED (oldest with
 *    oldest: the order the lines would have played) into the window at the
 *    share top, or above the prefix when the top overlaps a source;
 *    shift = ceil(log2(count)) halves per doubling so the 8/16-bit sum
 *    cannot clip, and the gain carries 2^shift back. Sources are read
 *    before any window byte is written (the window never overlaps them).
 *    A side that cannot be packed keeps its segments in place and is read
 *    as two heads summed (a stereo tape into a mono type);
 * 4. W = the window, or the union of the in-place segments' byte ranges
 *    (each at most two ranges where it wraps);
 * 5. the span: the largest contiguous piece of the usable share outside
 *    W, if it is at least seat_min. Else W is cut: of the two candidate
 *    regions [PREFIX, PREFIX + seat_min) and [bytes - seat_min, bytes),
 *    the one losing fewer fed samples is freed by truncating every segment
 *    at its first byte (in reading order) inside it; the span is then the
 *    largest free piece containing that region. A cut segment fades over
 *    min(882 samples, a quarter of its length); an uncut tape segment ends
 *    at the switch instant and does not fade;
 * 6. fill the control block (magic | owner, span, feed fields, dry ramp
 *    from effect_dry_uq15) and set the slot lock when a feed runs.
 * Why: plan §5.3; the record stays a pure description (SD3).
 * Inputs: share, the handoff, owner type id, seat_min bytes, mono (1 for
 * Echo and CrumpBit). Outputs: the control block; returns the span bytes
 * (offset in ctl->span_off). Cost: foreground, at most ≈ 0.25 ms (a pack
 * of ≤ 20 KB; the W/span search is a few dozen compares).
 * Callers: space_init(), crumpBit_init().
 * Affiliates: sk_feedRead(), effects_setFeedSlotLock(), FxBuffer.h read
 * rule, host laws K-1..K-5, H-1..H-9.
 */
uint32_t sk_feedBegin(const fx_share_t *share, const fxbuf_handoff_t *h,
                      uint8_t owner, uint32_t seat_min, uint8_t mono);

/*
 * Read the next block of the feed (plan §5.4, SD8).
 *
 * What: for each live side, frames output samples (or frames / 2 at the
 * half rate, half = 1, with the step doubled: the Reverb injects at its
 * tank rate) by the read rule: linear interpolation between the sample at
 * pos and the next one, x the gain and the end fade (ramped from the
 * block's start to its end value, so there is no division per frame);
 * frac += step, and one carry advances pos by stride (wrapping at
 * wrap_off + wrap_bytes) and lowers left. A side that reaches left = 0
 * writes zeros for the rest. When every side has ended, the feed ends:
 * feeding = 0 and effects_setFeedSlotLock(0) (the lock's last block).
 * Four compiled loop forms by (bits 8/16) x (one side / two sides), chosen
 * per block; a mono incoming with two unpacked sides sums them (the 1/2
 * is in the gain).
 * Inputs: share (its control block), out block, frames, owner, half.
 * Output: 1 when out holds fed audio this block, 0 when no feed runs (out
 * untouched; the caller runs its no-feed loop).
 * Cost: ≈ 12 per frame for one side, ≈ 20 for two (estimates; gated by
 * space_cpu.py / crumpbit_cpu.py).
 * Callers: the Space processes, crumpBit_feedPost().
 */
uint8_t sk_feedRead(const fx_share_t *share, sk_feedBlock_t *out,
                    uint32_t frames, uint8_t owner, uint8_t half);

/*
 * A share change during a feed (plan §3.3 shrink, D14, D26): if the new
 * share no longer covers W, the feed ends at once (the cut bytes may now be
 * a voice unit's) and the slot lock clears. Called first by every buffer
 * user's buffer_changed. Output: 1 when the feed was cut.
 */
uint8_t sk_feedCut(const fx_share_t *share, uint8_t owner);

/*
 * Growth at the next wrap (plan §3.3, SD9).
 *
 * What: a ring at [*off, *off + *len) (in the caller's units: frames,
 * bytes or words) that should become [new_off, new_off + new_len), with
 * the old range inside the new one. It changes only in the block in which
 * the write index reaches the old end (*wpos + advance >= *len): then
 * *wpos += *off - new_off (the history stays contiguous behind the head,
 * whether the ring grows up or down), *off = new_off, *len = new_len, and it
 * returns 1. Before that block it returns 0 and the caller holds its delay
 * at or below *len - 2 x SK_FRAMES_MAX - guard, so no read in the
 * switching block wraps through the old end.
 * Inputs: the ring, its write index, the target, the block's write advance
 * (frames x speed, rounded up). Output: 1 when grown this block.
 * Callers: space_seatTape() (Delay, Echo, predelay), crumpBit_seatActive().
 * Affiliates: host law S-2.
 */
uint8_t sk_growAtWrap(uint32_t *off, uint32_t *len, uint32_t *wpos,
                      uint32_t new_off, uint32_t new_len, uint32_t advance);

/*
 * Grow the control block's span at the next wrap (S095 P1, SD9): for a
 * user whose ring IS its span (CrumpBit). When no feed runs and the span is
 * smaller than the usable share, sk_growAtWrap() is applied to the span in
 * the caller's unit (1: bytes, 4: words): in the block whose write index
 * reaches the old end, ctl->span_off/span_bytes become the usable share and
 * *wpos is re-based by (old - new offset) / unit. Output: 1 when grown this
 * block (the caller sets its engine length from the new span), 0 otherwise.
 * sk_spanGrowPending() is 1 while such a growth waits, so the caller holds
 * its delay at or below the old length - 2 x 32 - guard.
 * Caller: crumpBit_growSpan(). Affiliates: host law K-7, CB24.
 */
uint8_t sk_spanGrow(const fx_share_t *share, uint8_t owner, uint32_t unit,
                    uint32_t *wpos, uint32_t advance);
uint8_t sk_spanGrowPending(const fx_share_t *share, uint8_t owner);

/*
 * A shrink (a voice claim, D14, D26; CrumpBit's rule since S091 P2): keep
 * the tail when the new range still starts at *off and holds the write
 * index (bytes [off, off + wpos) are the newest audio, in order; *valid is
 * cut to *wpos), else reseat (wpos 0, valid 0, off/len = the new range) and
 * return 2 so the caller fades its wet back in over 10 ms. Output: 0
 * unchanged, 1 kept, 2 reseated.
 */
uint8_t sk_shrinkKeep(uint32_t *off, uint32_t *len, uint32_t *wpos,
                      uint32_t *valid, uint32_t new_off, uint32_t new_len);

/*
 * The incoming dry ramp (plan §5.3 step 5): this block's dry origin d0 and
 * end d1 between the outgoing dry (ctl->dry_from_q7 / 128) and the type's
 * own dry target, over SK_DRY_RAMP_BLOCKS blocks; d0 = d1 = target when no
 * ramp runs. The type ramps its dry gain d0 -> d1 inside the block.
 */
void sk_dryRamp(const fx_share_t *share, uint8_t owner, float target,
                float *d0, float *d1);

/*
 * Export helpers (D24): sk_exportBegin() writes the wrap domain, format and
 * gains; sk_exportSeg() adds segment i (start, end as share-relative
 * bytes; side); it skips a zero-length segment and sets
 * FXBUF_STATE_EFFECT_WRITTEN when it adds one. The caller clamps start to
 * its valid region first. They write only the record, never the arena (SD3).
 */
void sk_exportBegin(fxbuf_handoff_t *out, uint32_t wrap_off,
                    uint32_t wrap_bytes, uint8_t bits, uint8_t stride,
                    uint16_t rate_hz, float speed, float dry, float gain);
void sk_exportSeg(fxbuf_handoff_t *out, uint8_t i, uint32_t start,
                  uint32_t end, uint8_t side);

/* Clear a byte range of the share (foreground; the rings at init, SD6). */
void sk_clear(const fx_share_t *share, uint32_t off, uint32_t bytes);

/*
 * The drain (D25, F3). sk_drainBegin(): 1 when the share's control block
 * is owned by `owner` (the outgoing buffer user; a stale block left by an
 * earlier type never starts a drain), then resets drain_blocks/drain_quiet;
 * 0 otherwise (no drain: the switch cuts, as for StereoFilter, SD11).
 * sk_drainStep(): per drain block, scales io by the envelope (1.0, then
 * linear to 0 over the last SK_DRAIN_FADE_BLOCKS of SK_DRAIN_BLOCKS),
 * tracks the block peak, and returns 1 when the drain is over.
 * Callers: effects_switchRuntime(), effects_drainProcess().
 */
uint8_t sk_drainBegin(const fx_share_t *share, uint8_t owner);
uint8_t sk_drainStep(effect_io_t *io);

#endif /* SPACE_KIT_H_ */
```

### S6.6 `SpaceKit.c`: the implementation

Includes: `SpaceKit.h`, `cmsis_intrinsics.h` (`__SSAT`), `<string.h>`.

The functions follow their header blocks above. The points below are the
ones an implementer must not get wrong; each goes into the function's
comments.

**`sk_spanBytes()` / `sk_spanOffset()`**

```c
    const sk_ctl_t *c = sk_ctl(share);
    /* owned: the block was written by this owner's init (live seat). */
    const uint8_t owned = (uint8_t)(share && share->base &&
        share->bytes >= SK_PREFIX_BYTES &&
        (c->magic & SK_CTL_MAGIC_MASK) == SK_CTL_MAGIC &&
        (uint8_t)c->magic == owner);
    /* The span never reaches past the live share (a shrink may have cut it
     * before buffer_changed ran). */
```

**`sk_feedBegin()`, the segment walk** (step 2):

```c
    for (i = 0u; i < FXBUF_EFFECT_POINTER_COUNT; i++) {
        /* slot: the handoff index of segment i (12 + i). */
        const uint32_t slot = FXBUF_HANDOFF_EFFECT_POINTER_BASE + i;
        const uint32_t s = h->read_offset[slot];
        const uint32_t e = h->write_offset[slot];
        /* bytes: the segment length, wrapping in the wrap domain. */
        uint32_t bytes;

        if (s == FXBUF_OFFSET_NONE || e == FXBUF_OFFSET_NONE)
            continue;
        bytes = (e >= s) ? e - s : e + h->effect_wrap_bytes - s;
        ...
    }
```

Each segment is checked against the share. A segment that is outside
`[SK_PREFIX_BYTES, share->bytes)`, or whose wrap domain is, is dropped. A
shrink between exit and init must never feed voice-unit bytes.

**Pack** (step 3): the destination test and the sum.

```c
    /*
     * dst: the window at the share top unless that overlaps a source, then
     * just above the prefix. The sources are at most 4 x 10 KB, the window
     * ≤ 20 KB, and the usable share ≥ 72 KB, so one of the two is free.
     */
    ...
    for (t = 0u; t < longest; t++) {
        /* acc: the start-aligned sum of this side's segments at time t. */
        int32_t acc = 0;
        for (k = 0u; k < count; k++)
            if (t < seg[k].samples)
                acc += sk_loadSample(share, &seg[k], t);   /* s8 or s16 */
        sk_storeSample(dst, t, acc >> shift, bits);         /* saturating */
    }
```

**Span choice** (step 5): a small interval list. W is at most eight
ranges (four segments, each split by one wrap). Sort by start, then walk the
gaps of `[SK_PREFIX_BYTES, share->bytes)`. Every gap is aligned **inward**
to 32 B (start rounded up, end rounded down), because spans are 32-byte
aligned.

**`sk_feedRead()`**: the inner loop of the two-side, 16-bit form.

```c
    for (n = 0u; n < frames; n++) {
        /* a/b: this and the next sample of each side (lane-offset loads). */
        const float l = sk_lerp(sk_ld16(w, pl), sk_ld16(w, sk_next(pl)), f);
        const float r = sk_lerp(sk_ld16(w, pr), sk_ld16(w, sk_next(pr)), f);
        out->l[n] = l * gl;          /* gl: gain x the left end fade */
        out->r[n] = r * gr;
        gl += dgl;                   /* the fades ramp inside the block */
        gr += dgr;
        f += step;
        /* carry: at most one per frame (step <= 1); select, no branch. */
        c = (f >= 1.0f);
        f -= c ? 1.0f : 0.0f;
        pl = c ? sk_next(pl) : pl;
        pr = c ? sk_next(pr) : pr;
    }
```

`sk_next(p)` is `p + stride`, minus `wrap_bytes` when it reaches the
domain end. `left[s]` is lowered by the carries counted for the block. A
side whose `left` would pass 0 inside the block has its gain end at 0 at
that frame: the fade's end value is taken at `min(frames, left / step)`.
The per-block code computes it with one division, outside the loop.

**`sk_drainStep()`**

```c
    c->drain_blocks++;
    /* g0, g1: the envelope at this block's start and end (linear in the
     * last 500 ms, 1.0 before). */
    ...
    for (n = 0u; n < io->frames; n++) {
        const float yl = io->l[n] * g;
        const float yr = io->r ? io->r[n] * g : 0.0f;
        io->l[n] = yl;
        if (io->r) io->r[n] = yr;
        peak = fmaxf(peak, fmaxf(fabsf(yl), fabsf(yr)));   /* VMAXNM, VABS */
        g += dg;
    }
    c->drain_quiet = (peak < SK_DRAIN_QUIET) ? c->drain_quiet + 1u : 0u;
    return (c->drain_quiet >= SK_DRAIN_QUIET_BLOCKS ||
            c->drain_blocks >= SK_DRAIN_BLOCKS);
```

`fmaxf`/`fabsf` compile to VMAXNM/VABS under `-Ofast`. The ELF gate checks
that no libm call is left (S12).

- **Gate S6:** host laws K-1..K-9 (§S12.2) pass:
  - K-1 the span helpers;
  - K-2 the segment walk and the share check;
  - K-3 the pack, start-aligned, with exact 8/16-bit sums;
  - K-4 the span choice and the cut;
  - K-5 the feed read against a double-precision reference;
  - K-6 the primitives;
  - K-7 growth at the wrap;
  - K-8 the shrink;
  - K-9 the drain envelope.

---
## S7. Tables: `tools/plots/gen_space_tables.py` (new) → `Core/DSP/Effects/Space/SpaceTables.h/.c` (generated)

### S7.1 The generator

It is modelled on `tools/plots/gen_crumpbit_tables.py`:

- a docstring listing every table and its law;
- `render_table()` / `render_u8()` / `render_u16()` writers;
- `--check` regenerates in memory, compares byte for byte with the
  committed files and runs the C-checks;
- **plain Python and `math` only.** It may not import numpy: the build does
  not depend on it.

```python
#!/usr/bin/env python3
"""Generate the Space Effect tables (S095 P1: Delay, Reverb, Echo).

What:       writes Core/DSP/Effects/Space/SpaceTables.c and SpaceTables.h:
              (128 entries, one per 0..127 row value, unless noted)
              space_fbkGain[r]      loop gain, D4: r/106 up to r = 106 (unity),
                                    then 1 + 0.2 (r - 106)/21 (1.20 at 127);
              space_toneCoef[t][6]  the tape SVF set {a1 a2 a3 m0 m1 m2}:
                                    t 0..62 LP, fc 800 Hz .. 20 kHz log;
                                    t 63 flat (m = 1 0 0, a = t 62's, so the
                                    state stays continuous across 63);
                                    t 64..127 HP, fc 20 Hz .. 2 kHz log;
                                    Q 0.8 (k = 1.25); g = tan(pi fc / fs),
                                    a1 = 1/(1 + g (g + k)), a2 = g a1,
                                    a3 = g a2; LP m = (0 0 1), HP (1 -k -1);
              space_degDrive[d]     drive 1 + 5 (d/127)^2 (x1 .. x6, D5);
              space_degHeadK[d]     head-LP cutoff factor 2^(-2 d/127)
                                    (down to -2 octaves of 0.42 v fs, D5);
              space_degHiss[d]      hiss amplitude: 0 at d 0, else
                                    10^((-72 + 24 d/127)/20) (-72..-48 dBFS);
              space_sprRatio[s]     2^((s - 63)/63): R time / L time, 1/2 .. 2
                                    (D23, continuous, equal at 63);
              space_wowDepth[w]     0.005 (w/127)^2: peak deviation of D
                                    (±0.5 %, plan §4.1);
              space_brkStep[b]      tape-brake speed change per block,
                                    32 / (T fs), T = 50 ms .. 2 s log over
                                    b 1..127; b 0 = 0 (the tape runs);
              space_hedGain[7][3]   the three heads D/3, 2D/3, D per `hed`
                                    stop, gain 1/(heads in use) (O6);
              space_dcyLog2[r]      log2 of the tank's per-sample gain at
                                    22,054 Hz for RT60 0.3 .. 20 s (log):
                                    -3 log2(10) / (RT60 22,054);
              space_lshGain[r]      low-band decay multiplier 0.5 .. 2
                                    (log, 1 at 63, D7);
              space_hshGain[r]      high-band damping 1 .. 0.1 (log);
              space_lsfK[r]/hsfK[r] shelf corners, one-pole k at 22,054 Hz:
                                    100 .. 600 Hz / 1.5 .. 9 kHz (log);
              space_difCoef[r]      input-allpass coefficient 0.5 .. 0.75;
              space_modDepth[r]     tank modulation excursion, 0 .. 16 x
                                    SCALE samples (Dattorro's 16 at siz 1);
              space_sizScale[r]     0.35 .. 1 (linear, D12);
              space_preSamples[r]   predelay at 22,054 Hz: 1.4 s (r/127)^2
                                    (uint16; the share clamps it, plan §4.2);
              space_semiRatio[49]   2^((s - 24)/12), the shimmer interval
                                    -24 .. +24 st;
              space_centRatio[r]    2^(50 r / 127 / 1200), the shimmer
                                    detune 0 .. 50 cents;
              space_envK[r]         gate hold / bloom rise per-block step for
                                    30 .. 600 ms (gate) and 20 .. 800 ms
                                    (bloom), by |r - 63| (O10);
            constants in SpaceTables.h:
              SPACE_HB_C1/C3/C5     the 11-tap halfband side taps (Kaiser
                                    beta 4.1, normalised 2 (c1+c3+c5) = 0.5);
              SPACE_PLATE_SCALE     the Dattorro-to-ring length scale, the
                                    largest of 1.0 / 1.25 / 1.5 / 1.75 for
                                    which every Reverb line at siz 1, plus
                                    the shimmer line (2,048), the
                                    modulation excursion and a 64-sample
                                    guard, fits 32,768 samples with >= 5 %
                                    spare (check C4);
              space_rvbLayout / space_echLayout  the plate line lengths at
                                    siz 1 (Dattorro's at 29,761 Hz x
                                    22,054 / 29,761 x SCALE), their ring
                                    offsets (the pointer counts up, every
                                    line `off` samples behind it), the taps
                                    (Reverb 5 per side after the first trim;
                                    Echo 4), the shimmer line base;
              SPACE_FEED_GAIN_DLY / _RVB / _ECH / SPACE_FEED_GAIN_CBT
                                    each type's wet-normalization gain of
                                    one side's summed segments (uQ1.15),
                                    from the tap law (a plate side: the RMS
                                    of its taps over the RMS of two summed
                                    lines; a tape: 1.0).
Checks (--check):
  C1 space_fbkGain[106] == 1.0 exactly and [127] == 1.2;
  C2 space_toneCoef[63] gives y == x (m0 1, m1 0, m2 0);
  C3 the halfband: DC 1.0; <= 0.1 dB to 5 kHz; >= -3.5 dB at 9 kHz;
     >= 45 dB rejection above 17.06 kHz (direct sum of the 11 taps over
     4,096 frequencies; SD10);
  C4 the plate fit above, for both rings (Reverb 32,768 samples; Echo
     16,384 at its fixed siz 0.6);
  C5 every table strictly monotonic where its law is.
Why:        no exp, pow, tan or division in a frame loop; every law has one
            source (plan §3, the ELF gate).
Inputs:     none.  Outputs: the two files, or a --check report (exit 1 on
            any difference or failed check).
Accessors:  tools/dsp_test/Makefile space_tables; S095_P1_IMPLEMENTATION.md
            §S7.
"""
```

### S7.2 `SpaceTables.h` (generated)

The header carries the externs and the constants listed above. Its
generated top comment says "Generated by tools/plots/gen_space_tables.py —
do not edit". Each table's law repeats as a one-line comment beside its
extern, so the header documents itself in place. The plate layout type:

```c
/*
 * One plate's geometry at siz 1 (generated). Lines in signal order: the
 * input allpasses ap_in[k]; per tank half h: the modulated allpass m[h],
 * delay d1[h], the decay allpass a2[h], delay d2[h]. off_* are each line's
 * input offset behind the ring pointer; a line of length L reads its
 * output at off + L. taps: the output tap offsets and signs per side.
 * siz scales every length at run time by one multiply (lengths glide by at
 * most one sample a block, D12).
 */
typedef struct {
    uint16_t ap_in_len[4], ap_in_off[4];
    uint16_t m_len[2], m_off[2], d1_len[2], d1_off[2];
    uint16_t a2_len[2], a2_off[2], d2_len[2], d2_off[2];
    uint16_t tap_off[2][5];
    int8_t   tap_sign[2][5];
    uint16_t shimmer_off;
    uint16_t ring_mask;          /* ring samples - 1 */
} space_plateLayout_t;
```

- **Gate S7:** `make -C tools/dsp_test space_tables` (S12) passes C1–C5
  and the byte compare.

---

## S8. `Core/DSP/Effects/Space/SpaceParameters.h/.c` (new): rows, tokens, page hooks

### S8.1 `SpaceParameters.h`

```c
/*
 * Space Effect parameter rows (S095 P1 plan §4, D1, D23).
 *
 * What: the descriptor indices of the three Space types. Rows 0..2 are the
 * common rows (out, vol, pan); 3.. are each type's own, in the plan's row
 * order. The indices are type-relative (a Delay row 6 is not a Reverb row
 * 6), as every Effect's are (VOLATILE_MEMORY: Effect-system invariants).
 * Why: the registry lanes, the DSP's write_param switch and the page hooks
 * name the same rows.
 * Accessors: EffectsManager.c (registry rows), SpaceEffect.c,
 * SpaceParameters.c.
 */
#ifndef SPACE_PARAMETERS_H_
#define SPACE_PARAMETERS_H_

#include "EffectsManager.h"

typedef enum {
    SPACE_DLY_MIX = EFFECT_COMMON_PARAM_COUNT,  /* 3  wet/dry */
    SPACE_DLY_TIME,       /* 4  CrumpBit's `dly` curve, share-truncated (D3) */
    SPACE_DLY_SPREAD,     /* 5  R time = L time x 2^((spr-63)/63) (D23) */
    SPACE_DLY_FEEDBACK,   /* 6  0..1.20, unity at 106 (D4) */
    SPACE_DLY_XFEED,      /* 7  L/R feedback cross-feed, continuous (D23) */
    SPACE_DLY_TONE,       /* 8  mid-loop SVF: LP < 63 = flat < HP */
    SPACE_DLY_DEGRADE,    /* 9  drive, head bandwidth, hiss (D5) */
    SPACE_DLY_WOW,        /* 10 wow + flutter + drift depth */
    SPACE_DLY_HOLD,       /* 11 on: input 0, loop gain 1 (D10) */
    SPACE_DLY_BRAKE,      /* 12 0 runs; > 0 stops over 50 ms..2 s (O3) */
    SPACE_DLY_REVERSE,    /* 13 reverse playback head (O4) */
    SPACE_DLY_HEADS,      /* 14 seven head stops, D/3 2D/3 D (O6) */
    SPACE_DLY_DUCK,       /* 15 wet ducked by the input (O1) */
    SPACE_DELAY_PARAM_ENUM_COUNT
} space_delay_param_t;
#define SPACE_DELAY_PARAM_COUNT   16u

typedef enum {
    SPACE_RVB_MIX = EFFECT_COMMON_PARAM_COUNT,  /* 3 */
    SPACE_RVB_PREDELAY,   /* 4  0..1.4 s, share-truncated */
    SPACE_RVB_SIZE,       /* 5  0.35..1 x, glides (D12) */
    SPACE_RVB_DECAY,      /* 6  RT60 0.3..20 s */
    SPACE_RVB_DIFFUSION,  /* 7  input allpass 0.5..0.75 */
    SPACE_RVB_LOW_SHELF,  /* 8  low-band decay x0.5..x2 (D7) */
    SPACE_RVB_LOW_FREQ,   /* 9  100..600 Hz */
    SPACE_RVB_HIGH_SHELF, /* 10 high-band damping x1..x0.1 */
    SPACE_RVB_HIGH_FREQ,  /* 11 1.5..9 kHz */
    SPACE_RVB_MOD,        /* 12 tank modulation depth */
    SPACE_RVB_WIDTH,      /* 13 M/S width of the taps */
    SPACE_RVB_FREEZE,     /* 14 off / on / on with shimmer (O12) */
    SPACE_RVB_SHIMMER,    /* 15 shimmer feed amount */
    SPACE_RVB_INTERVAL,   /* 16 -24..+24 st, rows 39..87 (63 = 0) */
    SPACE_RVB_DETUNE,     /* 17 0..50 cents, two voices +-dtn */
    SPACE_RVB_ENV,        /* 18 bloom < 63 = off < gate (O10) */
    SPACE_RVB_DUCK,       /* 19 (O11) */
    SPACE_REVERB_PARAM_ENUM_COUNT
} space_reverb_param_t;
#define SPACE_REVERB_PARAM_COUNT  20u

typedef enum {
    SPACE_ECH_MIX = EFFECT_COMMON_PARAM_COUNT,  /* 3 */
    SPACE_ECH_TIME,       /* 4  the Delay's law, mono tape */
    SPACE_ECH_FEEDBACK,   /* 5 */
    SPACE_ECH_TONE,       /* 6 */
    SPACE_ECH_DEGRADE,    /* 7 */
    SPACE_ECH_WOW,        /* 8 */
    SPACE_ECH_TO_REVERB,  /* 9  `e>r`: the amount between tape and plate */
    SPACE_ECH_BLEND,      /* 10 `bln`: echo .. plate in the wet */
    SPACE_ECH_DECAY,      /* 11 plate RT60 */
    SPACE_ECH_LOW_SHELF,  /* 12 */
    SPACE_ECH_HIGH_SHELF, /* 13 */
    SPACE_ECH_BRAKE,      /* 14 (O15) */
    SPACE_ECH_HEADS,      /* 15 (O15) */
    SPACE_ECH_DUCK,       /* 16 (O14) */
    SPACE_ECH_ROUTING,    /* 17 e>r / r>e / plate in the loop (O16) */
    SPACE_ECHO_PARAM_ENUM_COUNT
} space_echo_param_t;
#define SPACE_ECHO_PARAM_COUNT    18u

/* Stepped rows' values (the DSP and the labels read the same codes). */
#define SPACE_FREEZE_OFF          0u
#define SPACE_FREEZE_ON           1u
#define SPACE_FREEZE_SHIMMER      2u   /* O12 */
#define SPACE_HEADS_STOPS         7u   /* hed 0..6 */
#define SPACE_ROUTING_E2R         0u   /* echo into plate (§4.3) */
#define SPACE_ROUTING_R2E         1u   /* plate into echo */
#define SPACE_ROUTING_LOOP        2u   /* plate inside the echo loop */
#define SPACE_INTERVAL_ROW_MIN    39u  /* -24 st */
#define SPACE_INTERVAL_ROW_MAX    87u  /* +24 st */

extern const effect_param_descriptor_t space_delayDescriptors[];
extern const effect_param_descriptor_t space_reverbDescriptors[];
extern const effect_param_descriptor_t space_echoDescriptors[];
extern const char space_delayToken3[], space_delayAbbrev5[], space_delayFull8[];
extern const char space_reverbToken3[], space_reverbAbbrev5[], space_reverbFull8[];
extern const char space_echoToken3[], space_echoAbbrev5[], space_echoFull8[];
extern const effect_ui_hooks_t space_delayUi;
extern const effect_ui_hooks_t space_reverbUi;
extern const effect_ui_hooks_t space_echoUi;

#endif /* SPACE_PARAMETERS_H_ */
```

### S8.2 `SpaceParameters.c`: tokens (D1)

```c
/*
 * Registry identity (D1): the persisted tokens (AutoSave, `.fx`, MIDI
 * Learn match these three bytes), the 5-cell abbreviation of the type
 * browse, and the 8-cell full name.
 */
const char space_delayToken3[]  = "dly";
const char space_delayAbbrev5[] = "Delay";
const char space_delayFull8[]   = "Delay";
const char space_reverbToken3[]  = "rvb";
const char space_reverbAbbrev5[] = "Revrb";
const char space_reverbFull8[]   = "Reverb";
const char space_echoToken3[]  = "ech";
const char space_echoAbbrev5[] = "Echo ";
const char space_echoFull8[]   = "Echo";
```

### S8.3 `SpaceParameters.c`: descriptor tables

Includes: `SpaceParameters.h`, `SpaceEffect.h`, `SpaceTables.h`,
`EffectParamRows.h`, `menu.h`, `MenuText.h`, `menuEffects.h`,
`FxBuffer.h`, `CrumpBitParameters.h` (the shared `tim` label, S10.9),
`<string.h>`.

Flag conventions (CrumpBit's):

- continuous rows are `EFFECT_FLAGS_IMAGE` (Morph, LFO, Pattern) with
  `EFFECT_MOD_0_127`;
- stepped rows are `INSTRUMENT_PARAM_FLAG_MORPHABLE |
  INSTRUMENT_PARAM_FLAG_AUTOMATABLE` with `EFFECT_MOD_NONE`, as CrumpBit's
  `crump_filter_type`;
- on/off rows are `DTYPE_ON_OFF`, automatable only, as CrumpBit's
  `crump_sync`;
- the share-dependent rows are `EFFECT_ROW_FLAGS(...,
  EFFECT_PARAM_FLAG_BUFFER_DEPENDENT)`.

```c
/*
 * Delay rows (plan §4.1; D3, D4, D5, D10, D23; options O1, O3, O4, O6, O8,
 * O9). Defaults: a musical starting point (mix 40, time row 64 = a
 * StepScale stop, feedback 64 ≈ 0.6, flat tone, clean tape).
 */
const effect_param_descriptor_t space_delayDescriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE, EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("dly_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    /* D3: CrumpBit's `dly` curve, clamped to the live tape (effective_max). */
    EFFECT_ROW_FLAGS("dly_time", "Delay", "Time", "tim", DTYPE_0B127,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
    /* D23: continuous R/L time ratio 1/2..2 (63 = equal). */
    EFFECT_ROW("dly_spread", "Delay", "Spread", "spr", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("dly_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    /* D23: continuous L/R feedback cross-feed (0 stereo .. 127 ping-pong). */
    EFFECT_ROW("dly_xfeed", "Delay", "XFeed", "xfb", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_tone", "Delay", "Tone", "ton", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("dly_degrade", "Delay", "Degrade", "deg", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_wow", "Delay", "WowFlutr", "wow", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_hold", "Delay", "Hold", "hld", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("dly_brake", "Delay", "Brake", "brk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_reverse", "Delay", "Reverse", "rev", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("dly_heads", "Delay", "Heads", "hed", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, 0u, SPACE_HEADS_STOPS - 1u),
    EFFECT_ROW("dly_duck", "Delay", "Ducking", "dck", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
};
```

**Reverb** (`space_reverbDescriptors[]`; plan §4.2, D7, D8, D12, O10–O12):

| Index | Key | Category, long, short | dtype | Flags | Mod | Default | Max |
|---|---|---|---|---|---|---:|---:|
| 3 | `rvb_mix` | Reverb, Mix, `mix` | 0B127 | IMAGE | 0_127 | 40 | 127 |
| 4 | `rvb_predelay` (**BUFFER_DEPENDENT**) | Reverb, PreDelay, `pre` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 5 | `rvb_size` | Reverb, Size, `siz` | 0B127 | IMAGE | 0_127 | 96 | 127 |
| 6 | `rvb_decay` | Reverb, Decay, `dcy` | 0B127 | IMAGE | 0_127 | 64 | 127 |
| 7 | `rvb_diffusion` | Reverb, Diffusn, `dif` | 0B127 | IMAGE | 0_127 | 96 | 127 |
| 8 | `rvb_low_shelf` | Reverb, LowShelf, `lsh` | PM63 | IMAGE | 0_127 | 63 | 127 |
| 9 | `rvb_low_freq` | Reverb, LowFreq, `lsf` | 0B127 | IMAGE | 0_127 | 40 | 127 |
| 10 | `rvb_high_shelf` | Reverb, HighDamp, `hsh` | 0B127 | IMAGE | 0_127 | 40 | 127 |
| 11 | `rvb_high_freq` | Reverb, HighFreq, `hsf` | 0B127 | IMAGE | 0_127 | 80 | 127 |
| 12 | `rvb_mod` | Reverb, Modulatn, `mod` | 0B127 | IMAGE | 0_127 | 32 | 127 |
| 13 | `rvb_width` | Reverb, Width, `wid` | 0B127 | IMAGE | 0_127 | 63 | 127 |
| 14 | `rvb_freeze` | Reverb, Freeze, `frz` | 0B127 | MORPH \| AUTO | NONE | 0 | 2 |
| 15 | `rvb_shimmer` | Shimmer, Amount, `shm` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 16 | `rvb_interval` | Shimmer, Interval, `int` | PM63 | IMAGE | RANGE(39, 87) | 75 | 87 |
| 17 | `rvb_detune` | Shimmer, Detune, `dtn` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 18 | `rvb_env` | Reverb, GateBlom, `env` | PM63 | IMAGE | 0_127 | 63 | 127 |
| 19 | `rvb_duck` | Reverb, Ducking, `dck` | 0B127 | IMAGE | 0_127 | 0 | 127 |

**Echo** (`space_echoDescriptors[]`; plan §4.3, D11, O14–O16):

| Index | Key | Category, long, short | dtype | Flags | Mod | Default | Max |
|---|---|---|---|---|---|---:|---:|
| 3 | `ech_mix` | Echo, Mix, `mix` | 0B127 | IMAGE | 0_127 | 40 | 127 |
| 4 | `ech_time` (**BUFFER_DEPENDENT**) | Echo, Time, `tim` | 0B127 | IMAGE | 0_127 | 64 | 127 |
| 5 | `ech_feedback` | Echo, Feedback, `fbk` | 0B127 | IMAGE | 0_127 | 64 | 127 |
| 6 | `ech_tone` | Echo, Tone, `ton` | PM63 | IMAGE | 0_127 | 63 | 127 |
| 7 | `ech_degrade` | Echo, Degrade, `deg` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 8 | `ech_wow` | Echo, WowFlutr, `wow` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 9 | `ech_to_reverb` | Echo, EchoRvb, `e>r` | 0B127 | IMAGE | 0_127 | 64 | 127 |
| 10 | `ech_blend` | Echo, Blend, `bln` | 0B127 | IMAGE | 0_127 | 48 | 127 |
| 11 | `ech_decay` | Plate, Decay, `dcy` | 0B127 | IMAGE | 0_127 | 48 | 127 |
| 12 | `ech_low_shelf` | Plate, LowShelf, `lsh` | PM63 | IMAGE | 0_127 | 63 | 127 |
| 13 | `ech_high_shelf` | Plate, HighDamp, `hsh` | 0B127 | IMAGE | 0_127 | 40 | 127 |
| 14 | `ech_brake` | Echo, Brake, `brk` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 15 | `ech_heads` | Echo, Heads, `hed` | 0B127 | MORPH \| AUTO | NONE | 0 | 6 |
| 16 | `ech_duck` | Echo, Ducking, `dck` | 0B127 | IMAGE | 0_127 | 0 | 127 |
| 17 | `ech_routing` | Echo, Routing, `rtg` | 0B127 | MORPH \| AUTO | NONE | 0 | 2 |

Each table ends with the asserts CrumpBit uses (`CrumpBitParameters.c:195-199`):

```c
_Static_assert(sizeof(space_delayDescriptors) /
                   sizeof(space_delayDescriptors[0]) == SPACE_DELAY_PARAM_COUNT,
               "SPACE_DELAY_PARAM_COUNT must match the descriptor table");
_Static_assert(SPACE_DELAY_PARAM_ENUM_COUNT == SPACE_DELAY_PARAM_COUNT,
               "space_delay_param_t must match the descriptor table");
/* ... the same pair for Reverb and Echo ... */
```

### S8.4 `SpaceParameters.c`: page hooks (one set per type)

```c
/*
 * Effect-page value text of the Space types (S095 P1 plan §6 item 8).
 *
 * What, per row (3 characters; 0 = the generic dtype text):
 * - `tim` (Delay, Echo): the row the DSP will play, clamped first to the
 *   live tape's limit (space_timeLimit(), the clamp effects_service()
 *   applies), shown as CrumpBit's `dly` labels (crumpBit_formatDelayRow():
 *   a StepScale stop's name, else the row number), D3;
 * - `spr`: the R/L ratio with one decimal, "0.5" .. "2.0" (D23);
 * - `fbk`: the loop gain in percent, 0 .. 120 (D4);
 * - `xfb`: the cross-feed in percent, 0 .. 100;
 * - `hed`: the heads in use, "  3" (D only) "  2" "  1" " 13" " 23" " 12"
 *   "123" (1 = D/3, 2 = 2D/3, 3 = D; O6);
 * - `brk`: "run" at 0, else the generic number;
 * - `pre`: the predelay the DSP plays (clamped like `tim`) in ms up to 999,
 *   then seconds "1.0" .. "1.4";
 * - `dcy`: RT60 seconds, "0.3" .. "9.9", then " 10" .. " 20";
 * - `frz`: "off", " on", "shm" (O12);
 * - `int`: signed semitones (value - 63);
 * - `dtn`: cents 0 .. 50;
 * - `env`: "off" at 63, else the generic signed number (O10);
 * - `rtg`: "e>r", "r>e", "lp " (O16).
 * Why: the raw byte stays underneath (pots, storage, automation, LFO);
 * only the text follows the law, as CrumpBit's labels (S091 P2).
 * Inputs: row index, value shown, out[3]. Output: nonzero when written.
 * Foreground; the share read is a struct copy plus the control block.
 * Callers: menuEffects (format_value3, format_target3 for the stepped
 * rows). Affiliates: SpaceTables (the laws), space_timeLimit(),
 * crumpBit_formatDelayRow().
 */
static uint8_t space_delayFormatValue3(uint8_t index, uint8_t value, char *out);
static uint8_t space_reverbFormatValue3(uint8_t index, uint8_t value, char *out);
static uint8_t space_echoFormatValue3(uint8_t index, uint8_t value, char *out);

/* The stepped rows' names in lane and automation target text. */
static uint8_t space_delayFormatTarget3(uint8_t index, uint8_t value, char *out);
static uint8_t space_reverbFormatTarget3(uint8_t index, uint8_t value, char *out);
static uint8_t space_echoFormatTarget3(uint8_t index, uint8_t value, char *out);

/*
 * The hooks: values only. No SELECT/TRACK/BAR ownership, no LED row, no
 * custom row 0 and no layout (the default page layout places rows 3.. on
 * SELECT 2.., menuEffects.c header), no label variants.
 */
const effect_ui_hooks_t space_delayUi = {
    .select = NULL, .track = NULL, .bar = NULL, .render_leds = NULL,
    .paint_row0 = NULL, .format_value3 = space_delayFormatValue3,
    .flags = 0u, .labels = NULL, .layout = NULL,
    .format_target3 = space_delayFormatTarget3,
};
/* space_reverbUi, space_echoUi likewise. */
```

- **Gate S8:**
  - the DEV registry self-check returns 0;
  - host law **P-1** (§S12.2): every key is unique within its type; every
    short label is ≤ 3 characters and every long label ≤ 8; every default
    lies within [min, max]; every lane names a real row, uniquely; the
    lane rows equal plan §4.

---
## S9. `Core/DSP/Effects/Space/SpaceEffect.h/.c` (new): the three types

### S9.1 `SpaceEffect.h`

```c
/*
 * Space Effect types (S095 P1): Delay, Reverb, Echo.
 *
 * What: one module, three registry types (plan §2.2): three op sets that
 * share init, export, write_param, buffer_changed and effective_max
 * through a type tag, and three process functions. Two engines: the tape
 * (stereo in the Delay, mono in the Echo) and the plate (with shimmer in
 * the Reverb, without in the Echo).
 * Memory (SD1, SD13): the runtime (this union member, <= 100 B) holds the
 * raw rows, the type tag and the control-rate values that glide; the DSP
 * state is the 448 B header in the arena prefix (sk_state()); the audio is
 * the live span above the prefix:
 *   Delay  [span]           the stereo tape (L,R int16 frames);
 *   Reverb [span]           the 64 KB tank ring (SD5), then the predelay
 *                           line (int16 at 22,054 Hz, the rest);
 *   Echo   [span]           the 32 KB plate ring, then the mono tape.
 * Accessors: EffectsManager.c (registry rows, union), SpaceParameters.c.
 * Affiliates: SpaceKit.h, SpaceTables.h, CrumpBitEffect.h
 * (crumpBit_delaySamples(), crumpBit_delayLimit(): the `tim` law, D3).
 */
#ifndef SPACE_EFFECT_H_
#define SPACE_EFFECT_H_

#include "EffectsManager.h"
#include "SpaceKit.h"

/*
 * Each type's smallest usable span (plan §5.3 step 3; SD5): Delay 32 KB;
 * Reverb the 64 KB ring + 7,616 B predelay (0.17 s), which is exactly the
 * minimum share's span (73,664 - 512), so the ring always fits; Echo
 * 48 KB (32 KB ring + a 0.7 s tape at the 1/4 floor).
 */
#define SPACE_DELAY_SEAT_MIN     32768u
#define SPACE_REVERB_SEAT_MIN    73152u
#define SPACE_ECHO_SEAT_MIN      49152u

#define SPACE_TAG_DELAY          0u
#define SPACE_TAG_REVERB         1u
#define SPACE_TAG_ECHO           2u

/* Tape geometry: Hermite needs 2 frames either side; v floor 1/4 (D3). */
#define SPACE_TAPE_GUARD         4u
#define SPACE_TAPE_SPEED_MIN     0.25f

/* Raw row storage: rows 3.. of the largest type (Reverb, 17 rows). */
#define SPACE_ROWS_MAX           17u

/*
 * The Space union member (SD13; <= 100 B, asserted in EffectsManager.c).
 *
 * What:
 * - tag: SPACE_TAG_*; rows[]: raw row bytes (index - 3), written by
 *   space_writeParam(), converted per block in process;
 * - flags: SPACE_F_* (growth pending, fade-in after a reseat, seated);
 * - the tape (Delay, Echo) or the predelay line (Reverb): tape_off (bytes,
 *   share-relative), tape_len (frames), tape_w (write frame), tape_frac (the
 *   write head's sub-frame position, plan §4.1 "advances by v"), valid
 *   (frames written since the seat, saturating at tape_len), dmax (the
 *   delay ceiling: tape_len - guard, or the history after a growth, SD9);
 * - the glides (plan §4.1, 150 ms one-pole per block, then per-frame
 *   ramps): d_l/d_r (delay in tape frames), v (speed), brk_v (brake speed
 *   0..1), mix_g (the mix gain the block ramps from), fade_g (the reseat
 *   fade-in, 0..1);
 * - the plate (Reverb, Echo): ring_off (bytes), ring_p (the pointer,
 *   counts up), siz_g (the gliding size), ring_half (the half-rate phase
 *   0/1).
 * Lifetime: zeroed by the manager at a switch; space_init() fills it.
 */
typedef struct {
    uint8_t  tag;
    uint8_t  flags;
    uint8_t  rows[SPACE_ROWS_MAX];
    uint8_t  ring_half;
    uint32_t tape_off, tape_len, tape_w, valid, dmax;
    float    tape_frac;
    float    d_l, d_r, v, brk_v, mix_g, fade_g;
    uint32_t ring_off, ring_p;
    float    siz_g;
} SpaceRuntime;

#define SPACE_F_SEATED       0x01u
#define SPACE_F_GROW         0x02u   /* a growth waits for the wrap (SD9) */
#define SPACE_F_FADE_IN      0x04u   /* the wet returns over 10 ms */

extern const effect_type_ops_t space_delayOps;
extern const effect_type_ops_t space_reverbOps;
extern const effect_type_ops_t space_echoOps;

/*
 * The `tim` / `pre` clamps (BUFFER_DEPENDENT rows): the largest row that
 * fits the live (or, rt NULL, the default) seat at the current tempo.
 * Shared by effective_max and the page labels so both stop at the same
 * row (CrumpBit's rule).
 */
uint8_t space_timeLimit(uint8_t tag, const fx_share_t *share, uint8_t live);
uint8_t space_preLimit(const fx_share_t *share, uint8_t live);

#endif /* SPACE_EFFECT_H_ */
```

### S9.2 `SpaceEffect.c`: the header state (in the arena prefix)

```c
/*
 * The type state header (sk_state(), 448 B of the arena prefix, SD1).
 *
 * What: the per-frame DSP state of the live Space type, one struct per
 * type in a union: filter integrators, head LPs, DC blocks, LFO phases and
 * the random walk, envelope followers, the halfband histories, the
 * shifter phases. It lives in the arena (DTCM, zero-wait) because the
 * union member is 100 B; it never moves (SD1).
 * Lifetime: zeroed by space_init() (a 448 B memset, foreground).
 */
typedef struct {
    float tone_s[2][2];          /* SVF integrators per channel */
    float rec_lp[2][2];          /* 2-pole record head LP per channel */
    float play_lp[2];            /* 1-pole playback LP */
    float dc_x[2], dc_y[2];      /* DC blocks */
    float comp_env[2];           /* O8 tape compression level */
    float duck_env;              /* O1 */
    float wow_ph, flut_ph, drift, drift_to;   /* sk_lfo state */
    sk_shifter_t rev[2];         /* O4 reverse heads */
    uint32_t hiss_rng;           /* LCG for the hiss */
} space_tapeState_t;             /* ≈ 96 B */

typedef struct {
    sk_hbDown_t down;            /* input decimator (36 B) */
    sk_hbUp_t up[2];             /* output interpolators (48 B) */
    float bw_s;                  /* input bandwidth LP */
    float shelf_s[2];            /* in-loop shelf states, per half */
    float mod_ph[2];             /* tank modulation phases */
    float sh_hp_x, sh_hp_y;      /* the shimmer feed's 200 Hz HP */
    sk_shifter_t sh[2];          /* the two shimmer voices (D8) */
    float env_fast, env_slow, env_gain, env_t;   /* O10 gate/bloom */
    float duck_env;              /* O11 */
    float in_g, dcy_g, sh_g;     /* the freeze ramps (10 ms) */
    float tank_a, tank_b;        /* the halves' cross-coupled outputs */
} space_plateState_t;            /* ≈ 180 B */

typedef union {
    space_tapeState_t delay;
    space_plateState_t reverb;
    struct { space_tapeState_t tape; space_plateState_t plate; } echo;
} space_state_t;

_Static_assert(sizeof(space_state_t) <= SK_STATE_BYTES,
               "the Space state fits the 448 B header (SD1)");
```

### S9.3 Common ops

**`space_init()`** (static; one wrapper per type sets the tag: `space_delayInit()`,
`space_reverbInit()`, `space_echoInit()`):

```c
/*
 * Initialise a newly live Space type (plan §5.3; SD2, SD5, SD6).
 *
 * What:
 * 1. the tag and neutral rows (the forced resolution writes the Scene's
 *    rows before the first block): flat tone (63), spread 63, interval 75;
 * 2. sk_feedBegin(): read the handoff, pack a ring tail, choose the span
 *    outside the feed window W (at least this type's seat_min), fill the
 *    control block, set the slot lock when a feed runs;
 * 3. zero the 448 B state header;
 * 4. lay the span out (S9.1 table). The Reverb and Echo rings are CLEARED
 *    here (SD6: 64 KB / 32 KB memset in the foreground, ≈ 0.1-0.2 ms);
 *    the tapes and the predelay are not (their reads are gated by `valid`,
 *    so unwritten bytes never play);
 * 5. valid 0, dmax = tape_len - guard, the glides primed on the first
 *    block (d_l/d_r/v jump to their targets, as CrumpBit's `primed`),
 *    mix_g from the dry ramp's origin.
 * Inputs: runtime (zeroed), handoff, share. Output: runtime, the control
 * block and the state header. Caller: effects_switchRuntime().
 * Affiliates: sk_feedBegin(), sk_clear().
 */
```

**`space_exportHandoff()`** (static, shared by the three op sets):

```c
/*
 * Describe the valid audio for the next type (D24; SD3: no arena write).
 *
 * What, by tag:
 * - Delay: the wrap domain is the tape; segment 0 is the L head's pending
 *   repeat [P - D_L, P) frames (lane 0) and segment 1 the R head's
 *   [P - D_R, P) (lane 2); each start is clamped to the valid history
 *   (start = P - min(D, valid)); bits 16, stride 4, rate 44,108; speed v
 *   (with the brake: v x brk_v);
 * - Echo: the mono tape's pending repeat, one BOTH segment, stride 2 (D15:
 *   the plate is not exported);
 * - Reverb: the ring; its four tank delays d1/d2 of each half, oldest
 *   first. A line of length L read `off` behind the up-counting pointer p
 *   holds [p - off - L + 1, p - off + 1) (ring samples, x 2 bytes, modulo
 *   the ring): its next output first. Half B's two lines go to L, half A's
 *   to R. Bits 16, stride 2, rate 22,054, speed 1. Frozen or not, the lines
 *   are what they are;
 * - every type: dry = this block's dry gain, gain = SPACE_FEED_GAIN_*.
 * Inputs: runtime, the fresh record. Output: the record.
 * Callers: effects_exportHandoff() (exits and same-type refreshes).
 */
```

**`space_writeParam()`** stores `rows[index - 3]` (the manager has already
clamped it). Every conversion happens per block in process, so an LFO costs
what a static setting costs (CrumpBit's rule, `crumpBit_writeParam()`).

**`space_bufferChanged()`**:

```c
/*
 * The share changed (a voice unit claimed or released; D14, D26).
 *
 * What:
 * 1. sk_feedCut(): a feed whose W the new share no longer covers ends now;
 * 2. the span target is the whole usable share (no feed) or the current
 *    span clipped to the share (feed running);
 * 3. a smaller span: sk_shrinkKeep() on the tape/predelay (keep the
 *    newest when the write head is inside, else reseat and fade in over
 *    10 ms); if the plate ring itself is cut (only possible while a feed
 *    seated the span high), the ring is reseated at the new span base and
 *    cleared here (foreground, synchronous);
 * 4. a larger span: SPACE_F_GROW; process grows the tape at its next wrap
 *    (sk_growAtWrap(), SD9). The rings never move: the Reverb's predelay
 *    and the Echo's tape lie above their ring, so they grow upward only (a
 *    freed W below the ring stays unused until the next type change).
 * Caller: effects_onShareChanged().
 */
```

**`space_effectiveMax()`**:

```c
/*
 * The live clamp of the BUFFER_DEPENDENT rows (effects_service(), every
 * block; the page, foreground).
 * - `tim` (Delay, Echo; D3): crumpBit_delayLimit(4 x (frames - guard),
 *   bpm): the speed floor 1/4 makes the longest time four times the tape's
 *   frame count; frames from the live span when rt is the live runtime,
 *   else from the default span (the whole usable share);
 * - `pre` (Reverb): the largest row of space_preSamples[] at or below the
 *   predelay line's frames - guard (an unrolled 8-probe search,
 *   space_tableLimitU16(), CrumpBit's crumpBit_tableLimit() form);
 * - every other row: 255 (no clamp).
 * Inputs: runtime (may be NULL), row, share. Output: the row limit.
 */
```

**The op sets** (end of file):

```c
/*
 * One op set per type (plan §2.2): shared hooks, own init and process. No
 * lane variants (lane_map NULL), so the registry's lanes[] are the lanes.
 */
const effect_type_ops_t space_delayOps = {
    space_delayInit, space_exportHandoff, space_writeParam,
    space_delayProcess, space_bufferChanged, space_effectiveMax,
    NULL, 0u, 0u,
};
/* space_reverbOps, space_echoOps likewise. */
```

### S9.4 The Delay (plan §4.1; D3, D4, D5, D10, D23, O1, O3, O4, O6, O8)

**Per block** (`space_delayProcess()`, noinline):

```c
/*
 * One Delay block.
 *
 * What, per block (control rate; no per-frame division):
 * 1. seat: the span from the control block; a pending growth is taken in
 *    the block whose write head reaches the old end (sk_growAtWrap(); while
 *    pending, d targets are held <= tape_len - 2 x 32 - guard, SD9);
 * 2. times (D3, D23): T_L = crumpBit_delaySamples(tim, bpm) samples;
 *    T_R = T_L x space_sprRatio[spr]; each clamped to 4 (tape_len - guard);
 * 3. speed (one tape, one speed): v_t = 1 when max(T_L, T_R) <= tape_len -
 *    guard, else (tape_len - guard) / max(T_L, T_R) (the block's one
 *    division), floored at SPACE_TAPE_SPEED_MIN; the head delays in tape
 *    frames d_t = T x v_t, each <= dmax;
 * 4. glides: v, d_l, d_r move toward their targets by SK_GLIDE_K32 (the
 *    150 ms one-pole per block); the loop ramps each linearly from its
 *    block-start to block-end value, so a time change bends the pitch like
 *    tape and never steps;
 * 5. brake (O3): brk 0 runs; brk > 0 moves brk_v toward 0 by
 *    space_brkStep[brk] per block (toward 1 when brk returns to 0, at the
 *    last nonzero step); the write and read heads both advance by
 *    v x brk_v, and the wet follows sqrt(brk_v) (VSQRT, per block);
 * 6. coefficients: space_toneCoef[ton] (SVF), space_degDrive /
 *    space_degHeadK / space_degHiss[deg], space_fbkGain[fbk] (D4),
 *    xfb / 127, space_hedGain[hed], the wow depth and phases (sk_lfo: wow
 *    0.6 Hz + flutter 7 Hz + a random walk, L and R phase-offset), the
 *    ducker (O1), hold (D10: input gain -> 0 and the loop gain -> 1.0 over
 *    10 ms; ton and deg stay in the loop), reverse (O4: the read head is
 *    sk_shiftRead() at ratio -1 over the window D);
 * 7. the dry ramp (sk_dryRamp()) and the mix ramp (mix_g -> mix);
 * 8. the valid gate: the wet reads only history: gate = valid >= d + 32
 *    frames, else 0 for the block (a new seat is silent until its first
 *    repeat is due, CrumpBit's rule); after a reseat, fade_g rises over 10
 *    ms;
 * 9. the feed (SD8): sk_feedRead() into a local block; the feed loop
 *    variant runs when it returns 1, the plain loop otherwise;
 * 10. after the loop: valid += frames written (saturating), the states.
 * Inputs: runtime, io (stereo). Output: io, the tape, the header state.
 * Caller: effects_process(). Affiliates: space_delayRun(),
 * space_delayRunFeed().
 */
```

**The frame loop**: one `static inline __attribute__((always_inline))` body,
`space_delayLoop(..., const int feed)`. It is instantiated in two
`__attribute__((noinline))` functions, so the ELF gate can count each:
`space_delayRun()` (`feed` = 0) and `space_delayRunFeed()` (`feed` = 1).
Per frame, per channel c, with the other channel o (the plan's §4.1 loop):

```c
        /* P: the write position in frames (+ frac); v: this frame's speed. */
        /* rd: the playback head, Hermite at P - d_c - wow_c (plan D10). */
        rd_c = sk_hermite(t[i-1].c, t[i].c, t[i+1].c, t[i+2].c, f_c);
        /* O6 `hed`: the heads at d/3 and 2d/3 (linear reads), weighted by
         * space_hedGain[hed]; their sum is the playback signal. */
        rd_c = g3 * rd_c + g2 * sk_lerp(..2d/3..) + g1 * sk_lerp(..d/3..);
        rd_c = sk_onePole(&st->play_lp[c], rd_c, k_play);  /* playback LP */
        /* O4 `rev`: the reversed read replaces rd_c when rev is on, by
         * gains (r_on, 1 - r_on), so both forms cost the same. */
        if (feed)
            rd_c += fb->l[n] (L) / fb->r[n] (R);   /* C2: at the head */
        /* cross-feed (D23): 0 keeps L and R apart, 127 swaps them. */
        x_c = rd_c * (1.0f - xfb) + rd_o * xfb;
        x_c = sk_svfTone(&st->tone_s[c][0], &st->tone_s[c][1], x_c, &tone);
        /* O8: louder repeats dull (the head LP falls with the level). */
        /* degradation (D5): drive, clip, makeup; the hiss enters here. */
        x_c = sk_softClip(drive * x_c + hiss) * makeup;
        x_c = sk_dcBlock(&st->dc_x[c], &st->dc_y[c], x_c);
        /* the record head: in x input gain + feedback x loop, 2-pole LP
         * at 0.42 v fs (the anti-alias of the varispeed write). */
        w_c = rec_lp(in_c * in_g + fbk * x_c);
        /* out: the clean read is the wet (D5), ducked, at the mix. */
        out_c = in_c * dry + rd_c * wet;
    /* one 32-bit store writes the L,R pair at floor(P), saturated (SSAT). */
```

**Estimated cost** (plan §4.1, the D23 set): plain loop ≈ 191; the feed
variant adds 2 adds, plus `sk_feedRead()` ≈ 20, ≈ 213 against the feed
budget 203 + 13 = 216.

### S9.5 The Reverb (plan §4.2; D6, D7, D8, D12, O10–O12; SD5, SD10)

**Per block** (`space_reverbProcess()`, noinline):

```c
/*
 * One Reverb block: 32 output frames, 16 tank ticks at 22,054 Hz.
 *
 * What, per block:
 * 1. sizes (D12): siz_g glides to space_sizScale[siz] so that every line
 *    length (layout x siz_g, rounded) moves by at most one sample a block:
 *    the tail warbles as it grows, it never clicks;
 * 2. decay: g = sk_exp2Neg(space_dcyLog2[dcy] x the half's loop length)
 *    per half; the shelves (D7): g_lo = g x space_lshGain[lsh], g_hi = g x
 *    space_hshGain[hsh], corners space_lsfK / space_hsfK;
 * 3. freeze (O12): state off: in_g, dcy_g, sh_g ramp to (1, g, shm) over
 *    10 ms; on: (0, 1.0, 0) with the shelves flat; shm: (0, 1 - shm share,
 *    shm): the loop gain stays 1 while the shimmer rises (stability law
 *    R-7);
 * 4. shimmer (D8): the two voices' ratios space_semiRatio[int - 39] x
 *    space_centRatio[dtn]^+-1, window 50 ms (1,103 ticks), the phase steps
 *    per tick from them; the feed is the left tap through a 200 Hz HP into
 *    the 2,048-sample line inside the ring;
 * 5. env (O10): above 63 a gate (the wet closes 30..600 ms after each
 *    transient, then fades over 30 ms), below 63 a bloom (the wet rises
 *    over 20..800 ms after each transient); transient = the fast follower
 *    above twice the slow one; 63 is off;
 * 6. ducking (O11), the dry and mix ramps;
 * 7. the predelay's valid gate (as the tape's) and its growth at the wrap
 *    (SD9; upward only, above the ring);
 * 8. the feed (SD8): sk_feedRead(..., half = 1): 16 fed samples per side at
 *    the tank rate; the feed loop variant adds them to the tank input
 *    after the predelay and to the half-rate tap sums before the
 *    interpolators (C2: heard on time, washed by the new tank).
 * Caller: effects_process().
 */
```

**The tick loop** (`space_reverbRun()` / `space_reverbRunFeed()`, one
always_inline body). Per tank tick, from two input frames:

```c
        /* decimate: (L + R)/2 of two frames -> one tank-rate sample. */
        x = sk_hbDownPush2(&st->down, m0, m1);
        /* predelay (elastic, its own valid gate), then the bandwidth LP. */
        x = sk_onePole(&st->bw_s, predelay_read(x), k_bw) * in_g;
        if (feed) x += fb->l[t] + fb->r[t];     /* tank input, after pre */
        /* + the shimmer: two shifted voices off the shimmer line. */
        x += sh_g * (sk_shiftRead(&st->sh[0], ...) + sk_shiftRead(&st->sh[1], ...));
        /* input diffusion: 4 allpasses (dif); after the first trim, 5 taps. */
        /* the figure-8 tank (Dattorro): each half
         *   a = modulated allpass(x + other half's output)
         *   d1 delay -> shelf (g_lo, g_hi) -> x decay -> a2 allpass -> d2
         * all inside the up-counting ring (sk_ringIdx()). */
        /* taps: 5 per side (signed sums of d1/a2/d2 points), M/S width. */
        if (feed) { tl += fb->l[t] * fgain; tr += fb->r[t] * fgain; }
        /* interpolate: two output frames per side. */
        sk_hbUpPush(&st->up[0], tl, &l0, &l1);
        sk_hbUpPush(&st->up[1], tr, &r0, &r1);
        /* out = dry x in + wet x tap (ducked, gated/bloomed, at the mix). */
        p++;                                     /* the ring pointer */
```

**Estimated cost:** plain ≈ 191 per frame (183 + `env` 8 + `dck` 6 − the
first trim 6). The feed variant adds about 2, plus `sk_feedRead()` at the
half rate ≈ 10, ≈ 203 against 194 + 13 = 207.

### S9.6 The Echo (plan §4.3; D11, D15, O14–O16)

**Per block** (`space_echoProcess()`, noinline): the Delay's steps 1–8 on
one mono tape (input (L + R)/2; no `spr`, `xfb`, `rev` or `hld`; linear
tape read, the plan's −10), and the plate's steps 2 and 6 at its fixed
siz 0.6. The plate has 2 input allpasses, 4 taps per side, no predelay
and no shimmer. Routing (O16), by summing points only (both engines run
every block):

- `rtg` 0 (e>r): the plate input is `e>r` × echo + a little dry; out =
  dry + mix (echo (1 − bln) + plate bln);
- `rtg` 1 (r>e): the plate runs on the input, and its output feeds the tape
  input at `e>r`, so each repeat is a room;
- `rtg` 2 (loop): the plate sits in the tape's feedback path at `e>r`, so
  each repeat is more diffuse than the last.

**The feed** (SD8, mono): `sk_feedRead()` with one side, added at the tape's
playback head (`e>r` carries it into the plate).

**Loop symbols:** `space_echoRun()` / `space_echoRunFeed()`.

**Estimated cost:** ≈ 196 plain; feed ≈ 196 + 1 + 12 ≈ 209 against
199 + 13 = 212.

- **Gate S9a/b/c:**
  - the host laws of each type (§S12.2) pass;
  - `space_elf_check` passes;
  - `space_cpu_check` passes for the type:
    - plain ≤ the §1 engine cap (Delay 203, Reverb 194, Echo 199);
    - feed variant + `sk_feedRead()` ≤ cap + 13 (D19).

    If a type is over, the plan's §4 trims apply by rule, in their listed
    order:
    - Reverb: 3 input allpasses −6, then linear shifter reads −10;
    - Delay and Echo: a linear tape read −10.

    A remainder in the feed variant only is reported (D19: it comes from
    Advanced).

---
## S10. CrumpBit (plan §5.6; SD1, SD8, SD9)

The engine loops are **not touched**. Everything below is per block, at
init, in export, or in the new post-pass, so `crumpbit_elf_check`'s limits
hold unchanged.

### S10.1 `CrumpBitEffect.h` lines 18–46: **modify** the geometry

Add `#include "SpaceKit.h"` after `#include "EffectsManager.h"` (line
15; it brings `SK_PREFIX_BYTES`, `sk_ctl_t` and the kit API).

Replace the defines at lines 43–46 and the matching sentences of the
comment above them:

```c
/*
 * ... (the S091 history paragraph stays) ...
 * S095 P1 (SD1): the share's first SK_PREFIX_BYTES (512 B) are the arena
 * prefix every buffer user reserves (the control block for the seat, the
 * type-change feed and the drain); the loop lives above it. So:
 * CRUMPBIT_LOOP_MAX_BYTES: the longest loop, the arena (126,656 B) less the
 * prefix: 126,144 B, a multiple of 32.
 * CRUMPBIT_DELAY_CURVE_MAX: the loop less the 2-sample guard: 126,142
 * samples (2.860 s at 44,108 Hz; was 126,654, 2.871 s). Row 127 of the `dly`
 * curve still fits the full arena, and the Space Delay's top (its own 512 B
 * prefix, the 1/4 speed floor) is the same 2.86 s: one truncation law for
 * every tape (D3).
 * CRUMPBIT_LOOP_MIN_SHARE_BYTES: the share with all twelve voice units
 * claimed, 73,664 B (unchanged; declarative buffer_min_bytes).
 * CRUMPBIT_SEAT_MIN: the smallest span CrumpBit accepts beside a feed
 * window (plan §5.3 step 3): 32 KB.
 */
#define CRUMPBIT_DELAY_CURVE_MAX       126142.0f
#define CRUMPBIT_LOOP_MAX_BYTES        (SK_ARENA_PREF_BYTES - SK_PREFIX_BYTES)
#define CRUMPBIT_LOOP_MIN_SHARE_BYTES                                   \
    (SK_ARENA_PREF_BYTES -                                              \
     FXBUF_VOICE_UNIT_COUNT * FXBUF_VOICE_UNIT_BYTES)
#define CRUMPBIT_SEAT_MIN              32768u
```

The asserts at `CrumpBitEffect.c:185`, `:207` and `:211` hold:

- `126,144 / 2 + 2 fade + period` is still under the 16-bit marker;
- `126,144 ≥ 126,142 + 2`;
- `126,144 % 32 == 0`.

### S10.2 `CrumpBitEffect.h` after line 316 (`float crumpBit_shareMaxSamples(...)`): **add**

```c
/*
 * The `dly` knob limit (S091 P6; exported S095 P1, plan §5.6 item 4).
 *
 * What: the largest row of the `dly` delay-time curve whose delay fits
 * max_samples at bpm, by eight fixed probes of the curve (no table).
 * Why exported: the Space Delay and Echo `tim` rows play the same curve and
 * must stop at the same row for the same capacity (D3), so they call this
 * search instead of copying it.
 * Inputs: the longest delay the caller's seat holds (samples), bpm.
 * Output: 0..127.
 * Callers: crumpBit_subtypeRateLimit() (`dly`), space_timeLimit().
 */
uint8_t crumpBit_delayLimit(float max_samples, uint16_t bpm);
```

### S10.3 `CrumpBitEffect.c` line 478: **modify**

`static CRUMPBIT_NOINLINE uint8_t crumpBit_delayLimit(float max_samples,` →
`CRUMPBIT_NOINLINE uint8_t crumpBit_delayLimit(float max_samples,`
(remove `static`; it keeps its own symbol for the ELF gate). In its comment
block (lines 470–477), "Callers: crumpBit_subtypeRateLimit() (the manager's
effective_max, the page clamp and label)" gains "; space_timeLimit() (S095
P1, the Delay and Echo `tim`, D3)".

### S10.4 `CrumpBitEffect.c` lines 413–424 (`crumpBit_shareMaxSamples()`): **modify**

```c
float crumpBit_shareMaxSamples(const fx_share_t *share)
{
    /*
     * len: the loop this share seats (crumpBit_seat()'s rule). S095 P1
     * (SD1): the live span above the arena prefix, from the control block
     * when CrumpBit owns it (a feed may seat it smaller), else the whole
     * usable share.
     */
    uint32_t len = share ? sk_spanBytes(share, EFFECT_TYPE_CRUMPBIT) : 0u;
    ... (the LOOP_MAX clamp and the LOOP_MIN test unchanged) ...
}
```

Its header comment (`CrumpBitEffect.h:305-315`) changes "min(share bytes,
CRUMPBIT_LOOP_MAX_BYTES)" to "min(span bytes, CRUMPBIT_LOOP_MAX_BYTES), the
span being the live seat above the 512 B prefix (S095 P1)".

### S10.5 `CrumpBitEffect.c` lines 500–509 (`crumpBit_grainMaxSamples()`): **modify**

The half line becomes `min(sk_spanBytes(share, EFFECT_TYPE_CRUMPBIT),
CRUMPBIT_LOOP_MAX_BYTES) / 2`. Its comment says "(S095 P1: of the live
span)".

### S10.6 `CrumpBitEffect.c` lines 730–791 (`crumpBit_init()`): **modify**

The signature gains `const fx_share_t *share`. Line 744 `(void)handoff;`
becomes:

```c
    /*
     * S095 P1 (plan §5.3, §5.6 item 2; D20-D22): enter through the kit.
     * sk_feedBegin() reads the outgoing type's segments, packs a ring tail,
     * seats CrumpBit's span outside the feed window (at least
     * CRUMPBIT_SEAT_MIN; CrumpBit is a mono-engine type, so the tail is
     * folded to one side), writes the control block and sets the slot lock
     * while a feed runs. The loop's bytes are not cleared: `valid` mutes
     * every unwritten byte, as before (SD6).
     */
    (void)sk_feedBegin(share, handoff, EFFECT_TYPE_CRUMPBIT,
                       CRUMPBIT_SEAT_MIN, 1u);
    /*
     * The incoming dry ramp (plan §5.3 step 5) through CrumpBit's own mix
     * stage: its law is out = h + mix (wet - h), so dry = 1 - mix. The
     * first block ramps from the row whose dry equals the outgoing dry
     * (one block, CrumpBit's existing mix ramp, instead of the Space
     * types' 10 ms).
     */
    rt->mix_prev = (uint8_t)(127u - (((uint32_t)handoff->effect_dry_uq15 *
                                      127u + 0x4000u) >> 15));
```

**Note:** `effect_dry_uq15` is 0 when there was no Effect. The seed is
then `mix_prev` 127, which would ramp from fully wet. Guard it: seed only
when `handoff` is non-NULL and `handoff->state_flags &
FXBUF_STATE_EFFECT_WRITTEN`, and otherwise keep 0 (today's behaviour).
`sk_feedBegin()` tolerates a NULL handoff or share. It then starts no feed
and does not write the control block, so the span stays the default. The
host laws call `init(&rt, NULL, NULL)`.

The comment block above `crumpBit_init()` (lines 730–739) changes "The
handoff is not adopted in v1: re-entering CrumpBit never plays a previous
owner's tail." to "S095 P1: the previous owner's tail is fed through
(sk_feedBegin(), crumpBit_feedPost()): heard at its own due time and
written into CrumpBit's loop, where CrumpBit's quantizer truncates it to 8
bits (plan §5.4)."

### S10.7 `CrumpBitEffect.c` lines 829–877 (`crumpBit_exportHandoff()`): **replace** the body

```c
/*
 * Describe CrumpBit's valid audio when it is exited or refreshed (S095 P1,
 * D24; was: the write and read heads only).
 *
 * What: the wrap domain is the live span (or, `grn`, its delay half);
 * bits 8, rate 44,108, speed 1.0 (CrumpBit's tape glide moves the read
 * head, never the write speed), dry = 1 - mix, gain SPACE_FEED_GAIN_CBT.
 * Segments, each start clamped to `valid`:
 * - `dly`, `cmb`: the pending repeat [wp - delay, wp), BOTH, stride 1;
 * - `pit`: the window [wp - W, wp), BOTH, stride 1;
 * - `grn`: its delay line's pending tap [wp - tap_d, wp) in the delay half,
 *   BOTH, stride 1;
 * - `rvb`: the four FDN lines, lane k = [wp - d_k, wp) words (x 4 bytes),
 *   stride 4, lanes 0 and 2 to L, 1 and 3 to R (CrumpBit's wet sums: L
 *   carries l0 + l2, R l1 + l3); d_k recomputed from `rte` as
 *   crumpBit_runReverb() does (base_k x crumpBit_reverbSize[rte], clamped
 *   to the buffer).
 * Why: the next type feeds exactly this audio (plan §5.2 table). No arena
 * byte is written (SD3: a same-type Scene switch exports a live runtime).
 * Inputs: runtime, the fresh record. Output: the record.
 * Callers: effects_exportHandoff(). Affiliates: sk_exportBegin(),
 * sk_exportSeg(), FxBuffer.h read rule.
 */
```

The record's `effect_channels`, `effect_bits` and `effect_rate_hz` stay
written as today, because the decoders read them.

### S10.8 `CrumpBitEffect.c` `crumpBit_process()`, lines 2915–2958: **modify**

At lines 2926–2927 and 2940:

```c
    /*
     * S095 P1 (SD1, SD9): the loop is the live span above the prefix. A
     * span that may grow (no feed running, span smaller than the usable
     * share) grows in the block whose write head reaches the old end:
     * sk_spanGrow() re-bases the write index and the engine's length
     * follows, so the history stays contiguous and nothing is muted
     * (plan §5.6 item 3). `grn`'s two half-lines cannot grow in place; it
     * reseats as before.
     */
    crumpBit_growSpan(rt, io->share, io->frames);
    span_off = sk_spanOffset(io->share, EFFECT_TYPE_CRUMPBIT);
    bytes = sk_spanBytes(io->share, EFFECT_TYPE_CRUMPBIT);
    bytes = (bytes > CRUMPBIT_LOOP_MAX_BYTES) ? CRUMPBIT_LOOP_MAX_BYTES : bytes;
    ...
    blk.base = (int8_t *)io->share->base + span_off;
```

Before the engine switch (line 2951 area), record the write byte:
`crumpBit_bytePosition(rt, &wp0, &w0);`.

After the switch (between line 2957 and line 2958 `rt->mix_prev =
rt->mix_raw;`):

```c
    /* S095 P1 (SD8): a running feed is heard and written into the loop. */
    crumpBit_feedPost(rt, &blk, io->share, wp0, mix_to);
```

**New static function `crumpBit_growSpan()`** (above `crumpBit_process()`):

```c
/*
 * Grow CrumpBit's loop at its next wrap (S095 P1, SD9; plan §5.6 item 3).
 *
 * What: when no feed runs and the control block's span is smaller than the
 * usable share (a feed ended, or voice units were released), the span
 * becomes the usable share in the block whose write head reaches the old
 * end: sk_spanGrow() updates the span and re-bases the active engine's
 * write index (bytes for `dly`/`cmb`/`pit`, words for `rvb`) by
 * old offset - new offset, and the engine's length becomes the new one.
 * `valid` is kept, so the old history stays audible and the delay may
 * glide out to a target the small seat had clamped. While the growth is
 * pending, the delay is held <= old length - 2 x 32 - guard, so no read in
 * the switching block wraps through the old end. `grn` (two half-lines):
 * no in-place growth; its seat changes as before (crumpBit_seatActive()).
 * Inputs: runtime, share, frames. Output: runtime, the control block.
 * Caller: crumpBit_process().
 */
```

It uses the kit's `sk_spanGrow()` and `sk_spanGrowPending()`, declared in
S6.5 beside `sk_growAtWrap()`.

**New static function `crumpBit_feedPost()`** (noinline, above
`crumpBit_process()`):

```c
/*
 * Feed the previous Effect's tail into CrumpBit (S095 P1, plan §5.4, C2;
 * SD8).
 *
 * What, only while a feed runs (sk_feedRead() returns 1):
 * 1. the fed block (one side: CrumpBit is a mono-engine type) is added to
 *    both output channels at this block's mix ramp (heard at its own due
 *    time and at the NEW mix, plan §5.3 step 5), ahead of the output SVF,
 *    which then filters it like CrumpBit's own wet;
 * 2. it is written into the loop cells this block wrote, [wp0, wp) in the
 *    active engine's format, as CrumpBit's head signal would be:
 *    - `dly`, `cmb`, `pit`: cell = SSAT8(cell + 128 x fbk x feed): repeated
 *      at CrumpBit's own delay and feedback, truncated to 8 bits by
 *      CrumpBit's code (plan §5.4 "CrumpBit's quantizer does the 16-to-8
 *      truncation");
 *    - `grn`: into the record line (gain 1), so grains pick it up;
 *    - `rvb`: into all four FDN lanes of each word with the input's
 *      alternating signs (the FDN input).
 * With mix 0 (the new Scene's wet off) nothing is added or written (plan
 * §5.3 step 5).
 * Why a post-pass: CrumpBit's five engine loops stay bit-identical and
 * their ELF limits hold; the feed's cost is only in feed blocks.
 * Cost: sk_feedRead() ≈ 12 (one side) or ≈ 20 (an unpacked stereo tape
 * folded) + ≈ 12 here, per frame, feed blocks only (crumpbit_cpu.py
 * --feed-cap).
 * Inputs: runtime, block, share, the write byte before the engine, the
 * block's end mix. Output: io (blk.left/right), the loop.
 * Caller: crumpBit_process(). Affiliates: sk_feedRead().
 */
```

### S10.9 `CrumpBitEffect.c` lines 3080–3087 (`crumpBit_bufferChanged()`): **modify**

```c
    /*
     * S095 P1: a share change that cuts the feed window ends the feed
     * first (sk_feedCut(), D14); the seat then follows the live span.
     */
    (void)sk_feedCut(share, EFFECT_TYPE_CRUMPBIT);
    bytes = share ? sk_spanBytes(share, EFFECT_TYPE_CRUMPBIT) : 0u;
```

`crumpBit_effectiveMax()` (line 3105) needs no change. It reaches the span
through `crumpBit_shareMaxSamples()` and `crumpBit_grainMaxSamples()`, and
`rt` NULL falls back to the default span, because a non-live CrumpBit does
not own the control block.

### S10.10 `CrumpBitParameters.c` line 465: **modify** (the shared `tim` label)

`static void crumpBit_formatDelayTime(uint8_t row, char *out)` becomes
`void crumpBit_formatDelayRow(uint8_t row, char *out)`. Rename its one
caller (line 592, `crumpBit_formatDelayTime(shown, out);`).

Declare it in `CrumpBitParameters.h` before the `extern` block:

```c
/*
 * The `dly` delay-time label of a row (S091 P6; exported S095 P1): a
 * StepScale stop's name, else the row number. Shared with the Space Delay
 * and Echo `tim` (D3: the same curve shows the same labels).
 * Callers: crumpBit_uiFormatValue3(), space_delayFormatValue3(),
 * space_echoFormatValue3().
 */
void crumpBit_formatDelayRow(uint8_t row, char *out);
```

### S10.11 `CrumpBitEffect.c` lines 3194–3204 (`crumpBit_ops`): no change

The function names are the same. Only `crumpBit_init`'s signature
changed, matching S2.2.

- **Gate S10:**
  - host `crumpbit` passes CB1–CB23 (updated for the prefix, §S12.3) and
    the new CB24, with every break caught (old and new);
  - `crumpbit_elf_check` passes with its existing limits;
  - `crumpbit_cpu_check` passes:
    - `--cap 181` steady;
    - `--feed-cap 204` (engine + `crumpBit_feedPost` + `sk_feedRead`). A
      remainder is reported by D19 (SD8).

---
## S11. Root `Makefile`

| Line | Change |
|---|---|
| after 42 (`-ICore/DSP/Effects/CrumpBit \`) | **add** `-ICore/DSP/Effects/Space \` |
| after 122 (`Core/DSP/Effects/CrumpBit/CrumpBitTables.c \`) | **add** `Core/DSP/Effects/Space/SpaceParameters.c \` and `Core/DSP/Effects/Space/SpaceTables.c \` (descriptor and data sources, ordinary `-O2`, as CrumpBit's) |
| after 167 (`Core/DSP/Effects/CrumpBit/CrumpBitEffect.c \`) | **add** `Core/DSP/Effects/Space/SpaceKit.c \` and `Core/DSP/Effects/Space/SpaceEffect.c \` |
| after 262 (the CrumpBit `-Ofast` rule) | **add** the two rules below |

```make
# S095 P1: the Space kit (feed, drain, seat) and the Space types' DSP run
# with the fast-math policy like CrumpBit; their descriptor and table
# sources stay ordinary -O2 sources in SRCS.
$(BUILD)/Core/DSP/Effects/Space/SpaceKit.o: Core/DSP/Effects/Space/SpaceKit.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Effects/Space/SpaceEffect.o: Core/DSP/Effects/Space/SpaceEffect.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
```

---

## S12. Host tests, gates, break builds

### S12.1 `tools/dsp_test/Makefile`

- **`.PHONY`** (line 56 block) and **`all`** (line 62): **add** `space`,
  `space_tables`. The ELF gates `space_elf_check` and `space_cpu_check`
  join `.PHONY` only, like CrumpBit's: they need `make all` at the root
  first.
- **New target `space:`**, modelled on `crumpbit:` (line 645). It extracts
  with `extract.py`:
  - `FxBuffer.h`: the typedefs `fx_share_t`, `fxbuf_handoff_t`; the macros
    used today plus `FXBUF_SEG_SIDE_*`, `FXBUF_UQ15_ONE` and
    `FXBUF_OFFSET_NONE`;
  - `EffectsManager.h`: `effect_io_t`, `effect_type_ops_t`, the
    `EFFECT_IO_*` and `EFFECT_TYPE_*` macros;
  - `--body` of `SpaceKit.h/.c`, `SpaceTables.h/.c`, `SpaceParameters.h`,
    `SpaceEffect.h/.c`, plus CrumpBit's existing set (H-1 pairs with every
    CrumpBit sub-type);
  - `EffectsManager.c`: `--func effects_switchRuntime --func
    effects_drainProcess` (for H-7, with the stand-ins of §S12.2).

  It builds `modules/space.c` and runs it, then every `break_sp*` build
  below.
- **`space_tables:`**: `cd $(ROOT) && python3 tools/plots/gen_space_tables.py --check`.
- **`crumpbit:`**:
  - add the `--body` extraction of `SpaceKit.h/.c` before
    `CrumpBitEffect.host.h` (CrumpBit now includes the kit);
  - add `break_cb26` and `break_cb27`;
  - the existing `break_cb1`..`break_cb25` seds are unchanged: none of
    them touches the lines S10 edits. Each keeps its `grep -q` guard,
    which fails the build if a sed no longer applies.
- **`crumpbit_cpu_check:`**: `python3 crumpbit_cpu.py --elf $(ROOT)/build/lxr02.elf --cap 181 --feed-cap 204`.
- **New `space_elf_check:`**, modelled on `crumpbit_elf_check` (line
  962). `elf_looppath.py --max` caps each frame loop; the six engine loops'
  limits are set from their first linked measurement plus 2, as S091 did:
  - `space_delayRun`, `space_delayRunFeed`;
  - `space_reverbRun`, `space_reverbRunFeed`;
  - `space_echoRun`, `space_echoRunFeed`;
  - the four `sk_feedRead` forms;
  - `sk_drainStep`.

  The no-VDIV / no-`__aeabi_*` / no-double / no-libm check uses
  `elf_symdump.py` (the tool `crumpbit_elf_check` relies on through
  `elf_looppath.py`). It runs on every symbol of `SpaceKit.o` and
  `SpaceEffect.o` that holds a frame loop.
- **New `space_cpu_check:`**: `python3 space_cpu.py --elf $(ROOT)/build/lxr02.elf`.

### S12.2 `tools/dsp_test/modules/space.c` (new): the laws

The file header follows `modules/crumpbit.c`'s form (What / Why / Inputs /
Outputs / Accessors / Affiliates, one bullet per law).

Stand-ins, each with a comment:

- `seq_getBpm()` returns `sp_bpm`;
- `GetRngValue()` is an LCG;
- `effects_setFeedSlotLock()` records the lock into `sp_lock`, so L-1 can
  read it;
- for H-7, the statics the two extracted manager functions use:
  - an `effects_state` with `runtime_type`, `force_all`, `reserved`;
  - `effects_runtime`;
  - `effects_runtimeMember()`;
  - a registry lookup over the host's four op sets (OFF, Delay, Reverb,
    Echo, CrumpBit);
  - `effects_exportHandoff()` over a host handoff record;
  - `fxbuf_effectShare()` returning the test share.

The arena is a static `uint8_t sp_arena[126656]`, aligned to 32.

| Law | Plan test | Check |
|---|---|---|
| K-1 | — | span helpers: an owned control block's span; a foreign or absent block gives `[512, bytes)` |
| K-2 | — | the segment walk drops a segment outside the share or the prefix, and wraps lengths |
| K-3 | (D24, SD7) | the pack: four 16-bit lines and four 8-bit lanes, start-aligned, equal the reference sums `>> shift` exactly; the window never overlaps a source |
| K-4 | §5.3 step 5 | the span choice: largest free piece ≥ `seat_min`; the cut takes the cheaper region and truncates at the first overlapping byte in reading order |
| K-5 | H-3 | the feed read against a double reference: 8-bit c reads c/128 exactly; at v = 0.5 the read pitch is kept ±1 %; the end fade lands at 0 |
| K-6 | — | the primitives: Hermite and lerp exact on a cubic and a line; the SVF flat row bit-exact; the halfband against direct convolution (1e-6); the shifter's head gains sum to 1 |
| K-7 | S-2 | growth at the wrap, up and down: no output step above 4× the steady state, no mute, and the history contiguous (a ramp written before and after reads back monotonic) |
| K-8 | S-3 | shrink: keeps the newest when the head is inside; else reseats, and the wet returns over 10 ms with no NaN |
| K-9 | H-7 (kit) | drain envelope: 1.0 then linear over 689 blocks; quiet end after 14 blocks under 1e-4 |
| P-1 | — | rows: unique keys, labels within 3/8 cells, defaults in range, lanes equal plan §4 |
| D-1 … D-6 | D-1 … D-6 | as the plan, plus D-4's `xfb` 63 equal and monotonic (S8) |
| R-1 … R-5 | R-1 … R-5 | as the plan |
| R-6 | R-6 (**SD10**) | the halfband through the Reverb: ≤ 0.1 dB to 5 kHz, ≥ −3.5 dB at 9 kHz, ≥ 45 dB images above 17 kHz |
| E-1 … E-3 | E-1 … E-3 | as the plan |
| S-1 | S-1 | longest times: Delay 2.86 s full / 1.66 s minimum share; CrumpBit 2.86 s / 1.66 s; Echo the whole curve / 1.83 s; Reverb predelay 1.37 s / 0.17 s |
| S-2, S-3 | S-2, S-3 | through the real types; voice units are claimed and released with a host share (D26) |
| H-1 … H-8 | H-1 … H-8 | as the plan: every ordered pair of {Delay, Reverb, Echo, CrumpBit × 5, OFF}. H-6 is rewritten (SD7): a full-share Delay at its longest time into each type: W is cut, the incoming type seats at `seat_min`, the feed fades before the cut, nothing faults. H-7 runs the extracted `effects_switchRuntime()` / `effects_drainProcess()`: the drain output equals the type run on zero input bit for bit until the last 500 ms; it ends; a change mid-drain feeds; back to the same type keeps the runtime; after the end no feed (F2) |
| H-9 | (D24) | the segment metadata, every type and state (fresh, partly valid, wrapped, grown, cut): a counter ramp written by the type's own write path reads back by the §5.2 rule oldest first, with no gap, repeat or byte outside `[start, end)` |
| L-1 | L-1 | (host `modulation` L5, §S5.5) plus: `sp_lock` set by a feed's init, cleared on its last block and on a cut |
| L-2 | L-2 | `tim` rows equal `crumpBit_delaySamples()`; the knob limit equals `crumpBit_delayLimit()`'s first row over capacity at 60, 120 and 180 BPM, full and minimum share |
| O-1 … O-8 | O-1 … O-8 | as the plan; O-6 is R-7 |
| G-1 | "Every type" | after CrumpBit garbage in the arena, a new Space type's own wet stays muted: tapes by `valid`, rings cleared at init (SD6); only the deliberate feed plays |

### S12.3 `tools/dsp_test/modules/crumpbit.c` updates (S10)

| Line | Change |
|---|---|
| 213 area | **add** the `sk_*` stand-ins: `effects_setFeedSlotLock()` records into `cb_lock` |
| 223 area (includes) | **add** `#include "SpaceKit.host.h"` / `"SpaceKit.host.c"` before `CrumpBitEffect.host.h` |
| 290–297 (`ref_shareMax()`) | **modify**: `len = (bytes > SK_PREFIX_BYTES) ? bytes - SK_PREFIX_BYTES : 0`, then the LOOP_MAX clamp (the loop is above the prefix) |
| 415–418 (`ref2_seat()`) | **modify**: the same `bytes - SK_PREFIX_BYTES` |
| 608–609 (`cb_shares[]`) | **modify**: entry 0 `CRUMPBIT_LOOP_MAX_BYTES + SK_PREFIX_BYTES` (the whole arena, 126,656); the others are shares and stay |
| 850, 1030, 1124 and every other `crumpBit_ops.init(&rt, NULL);` | **modify** to `crumpBit_ops.init(&rt, NULL, NULL);`: no handoff and no share means no feed and the default span (S10.6), so the old laws are unchanged |
| 803 (`#define CB_ARENA CRUMPBIT_LOOP_MAX_BYTES`) | **modify** to `(CRUMPBIT_LOOP_MAX_BYTES + SK_PREFIX_BYTES)`: the test arenas `arena_new`, `arena_ref` and `arena_p3` stay the whole 126,656 B arena. Without this, `cb_shares[0]` (above) would describe a share larger than the buffer |
| 1056–1057 (CB8's `sum += (int8_t)arena_new[i];` over `i < rt.u.tap.length`) | **modify** the index to `arena_new[SK_PREFIX_BYTES + i]`: the loop starts above the prefix. It is the only law that reads loop bytes; the others compare outputs |
| the small test arenas (`arena_r1` 16,384 B at 2441, `arena_bal` 4,096 B at 2495, `arena_m` 4,096 B at 2685) | **check**: each is ≥ `SK_PREFIX_BYTES` + `CRUMPBIT_LOOP_MIN`, so CrumpBit still seats (loops 512 B shorter). Their laws compare outputs between equal arenas, so they hold unchanged |
| new **CB24** | the feed into CrumpBit (S10.8): for each sub-type, from an 8-bit tape, a 16-bit stereo tape and a packed ring: the fed audio appears in the output at its due time; the cells written this block hold the 8-bit truncation of fbk × feed (or the FDN input for `rvb`); mix 0 adds and writes nothing; the lock is set and then cleared. Plus H-9 for CrumpBit's export, all five sub-types |

New break builds (the `^…$`-anchored sed form, per plan §H.5):

- **`break_cb26`:** the `crumpBit_feedPost(...)` call line removed; it must
  fail CB24.
- **`break_cb27`:** the export's `valid` clamp removed; it must fail CB24
  (H-9 part).

### S12.4 `tools/dsp_test/space_cpu.py` (new)

The method is `crumpbit_cpu.py`'s (S091 P5), per type:

```
plain  = engine loop (space_<t>Run) + (engine per block + process per block
         + effective_max per block incl. crumpBit_delayLimit / the pre search) / 32
feed   = engine loop (space_<t>RunFeed) + sk_feedRead loop (the type's form:
         Delay two sides, Reverb half-rate two sides / 2, Echo one side)
         + the same per-block terms / 32
```

It fails when plain > the plan §1 cap (Delay 203, Reverb 194, Echo 199) or
feed > cap + 13. Its docstring follows `crumpbit_cpu.py`'s form, and names
D19 and SD8 for the feed cap.

`crumpbit_cpu.py`: **add** `--feed-cap`, computing the sub-type total +
`crumpBit_feedPost` loop + `sk_feedRead` one-side loop
+ their per-block paths / 32. The default is off, so old invocations behave
as before.

### S12.5 Space break builds (in the `space:` target)

| Build | Sed (anchored) | Must fail |
|---|---|---|
| `break_sp1` | the pack's start alignment made end-aligned | K-3 |
| `break_sp2` | the export's `valid` clamp removed (Delay) | H-9 |
| `break_sp3` | `sk_growAtWrap()` without the re-base | K-7 |
| `break_sp4` | the lock never cleared at the feed's end | L-1 |
| `break_sp5` | the cross-feed line `x = rd_c` (xfb ignored) | D-4 |
| `break_sp6` | `space_sprRatio` read at `126 - spr` | O-8 |
| `break_sp7` | the drain's input zeroing removed | H-7 |
| `break_sp8` | the feed injected at mix 0 | H-8 |
| `break_sp9` | `SPACE_HB_C3` sign flipped | R-6 |
| `break_sp10` | the Reverb freeze leaves `in_g` at 1 | R-3 |

---

## S13. Full build and gate run (the record)

```
make all && make img
python3 tools/diagnostic_tools/link_budget.py arm-none-eabi-nm build/lxr02.elf \
  > tools/dsp_test/baselines/s095p1/link_budget_after.txt
arm-none-eabi-nm -S build/lxr02.elf | grep fxbuf_handoffRecord   # 000000c4
make -C tools/dsp_test all
make -C tools/dsp_test elf_p4_check P4BASE=baselines/s095p1
make -C tools/dsp_test crumpbit_elf_check crumpbit_cpu_check
make -C tools/dsp_test space_elf_check space_cpu_check
```

Expected:

- flash ≈ +18 KB;
- ITCM 13,984 unchanged;
- DTCM statics 4,396 unchanged;
- FXBUF 126,656 unchanged;
- the handoff record `0xc4`;
- every host law and break as listed;
- the oscillator and mixer loop dumps unchanged against `s095p1`;
- CrumpBit's steady totals within ±1 of `crumpbit_cpu_before.txt`. The
  per-block `sk_spanBytes()` read adds a few instructions per block, under
  0.2 per frame.

Save each gate's output beside the S0 files, in `baselines/s095p1/`. The
resource figures go in one net total against the S0 line (user rule).

---

## S14. Device run (one flash; plan §9)

1. Flash the S13 image once.
2. Use the worst Scene (plan §9): four Alkalines on `wtb` blends, two
   HiHats, the compressor on. Part of the run is with all voice units
   claimed. **No code claims voice units today (D26), so that part cannot
   run; it is covered by the host (S-1, S-3, H-6).**
3. With a tail ringing at each change, cycle the Effect about 10 s per
   type:
   1. Delay (`hed`, `rev`, `fbk` high);
   2. Reverb (`shm`, `dtn`, `frz`);
   3. Echo;
   4. CrumpBit `grn`;
   5. OFF (the drain);
   6. back to Delay during the drain (a feed from a draining type).
4. **What the probe gives** (`decode_devlogs.py`, stage `'p'`):
   - `fx` + `fxsvc` per type, steady and in feed blocks;
   - the voice sections with the slot locked (what the lock frees);
   - the drain's `fx`;
   - the cycles per instruction of this code.
5. **Pass:**
   - steady blocks: `fx` + `fxsvc` ≤ 16,970, every type, worst block;
   - feed blocks: ≤ 16,970 plus what the locked slot freed;
   - a type over in steady blocks takes its §4 trims by rule;
   - a feed-block remainder, or a slot that frees less than 880, comes from
     Advanced, split evenly (D19, SD8), and is reported as one net total;
   - no second run is scheduled.
6. **Listening:** the user tries the D23 set by ear (plan D23). §16 notes
   that the dry ramp into CrumpBit is one block (S10.6), not 10 ms.

---

## S15. Not scheduled here

- **F5, the compressor adjustments** (plan §H.1): the user gives the
  scope at the end of S095 P1. They sit in the shared mixer budget
  (12,784).
- **S095 P2, the Colloid:** after S14, on the Advanced budget S14 confirms.

---

## S16. Statements that go out of date (for the user to log)

The plan's §11 list stands, with these additions from this schedule:

- **This schedule's decisions** supersede these plan figures:
  - §0: Delay/CrumpBit top 2.86 s / 2.87 s → both 2.86 s (SD1);
  - §3.3: the header that moves with the seat → the fixed 512 B prefix
    (SD1);
  - §4.2: the 32 KB ring mode, the quiet reseat and the `siz` clamp → none
    (SD5);
  - §5.3 step 6: the incremental clear → the rings cleared at init (SD6);
  - §5.2: "re-orders at export" → re-orders at the incoming init (SD3);
  - the reverb pre-sum is start-aligned, not end-aligned (SD7);
  - §5.4's feed cost → SD8;
  - §8 R-6 → SD10;
  - §8 S-4 dropped (SD5).
- **`CrumpBitEffect.h`:**
  - `CRUMPBIT_LOOP_MAX_BYTES` 126,144, `CRUMPBIT_DELAY_CURVE_MAX` 126,142;
  - the loop starts at share offset 512.

  `EFFECTS_MODULES_DSP_REFERENCE.md` §6 and §6.2 (CrumpBit geometry, the
  `dly` top 2.871 s) and any `MEMORY.md` CrumpBit timing line follow.
- **`EFFECTS_BUS_REFERENCE.md`:**
  - the type list (6 types);
  - into OFF, the drain;
  - the logical type;
  - the arena prefix;
  - the feed and the segment contract (§6, §8, §9).
- **`STORAGE_SRAM_MANIFEST.md`:**
  - `fxbuf_handoff_t` 196 B;
  - the arena prefix (control block 64 B + state 448 B), Effect-owned;
  - `effects_state.reserved` bits.
- **`MIXER_SYSTEM_DSP_REFERENCE.md`:** none. The mixer code is unchanged;
  the Effect's io flags stay up during a drain.
- **`CPU_USE_DSP_AUDIT.md`:** the Space types' cost items, the feed's
  measured cost, the D19 outcome.
- **`S094_P2_ALKALINE_SAVINGS.md` §21:** the budget lines, per plan §11,
  plus any D19 remainder from S14.
- **`MEMORY.md` Effects lines:**
  - the CrumpBit 170-cap note (S091) is long superseded by 181;
  - add the Space types, the drain, the feed slot lock and the prefix.

---

## S17. Implementation log (2026-11-12)

Working notes for the implementation itself. The steps below are in the
schedule's order; each names what landed and its gate result.

### S0. Baselines

- `tools/dsp_test/baselines/s095p1/` holds `link_budget_before.txt`,
  `crumpbit_cpu_before.txt` (`grn` 180.8, `rvb` 179.4),
  `crumpbit_elf_before.txt`, `osc.txt` and `mixer.txt`.
- **Pre-existing gate failure:** `crumpbit_elf_check`'s
  `crumpBit_process --rank 2` probe already failed on the baseline ELF
  ("has 1 loops, no rank 2"); the S091 P5 two-loop method no longer matches
  this tree's LTO layout. Recorded, not caused by S095. Everything before it
  passes.

### S1. `FxBuffer.h/.c`

- Added `FXBUF_SEG_SIDE_*` / `FXBUF_UQ15_ONE`; the handoff comment now
  documents the D24 read rule; seven fields appended. `sizeof == 196`
  asserted and confirmed (`nm`: `fxbuf_handoffRecord` `0xc4`).
- `fxbuf_handoffBeginExit()` resets the segment description.

### S2/S3/S4. Manager, ids, init signature

- Ids 3/4/5 and `EFFECT_TYPE_COUNT` 6; `init(rt, handoff, share)`; the
  `SpaceRuntime` union member; three registry rows with the plan's lanes;
  `effects_feedSlotLock()` / `effects_setFeedSlotLock()`.
- The drain: `effects_switchRuntime()` starts it on into-OFF from a
  buffer-using type, cancels it on a return, and clears both reserved flags
  at an exit; `effects_drainProcess()` mutes the input, runs the type, scales
  by `sk_drainStep()` and clears `FXBUF_STATE_EFFECT_WRITTEN` at the end;
  `effects_service()` skips the descriptor loop while draining (the FX latch
  is still consumed); `effects_activeType()` returns the logical type and
  `effects_rebindSourceScene()` uses it. StereoFilter takes the share and
  ignores it.

### S5. Slot lock

- Both `modNode_interpClaim()` grant paths use
  `OSC_WAVE_INTERP_MAX_ACTIVE - effects_feedSlotLock()`;
  `instrumentManager_itpFadeSlotAvailable()` uses the same limit. Host
  `modulation` gains law L7 (L5 is already the stamp wrap) and passes.

### S6. Space kit

- `SpaceKit.h/.c`: the 512 B prefix (64 B control block + 448 B state
  header), the primitives, span helpers, export helpers, the segment walk /
  pack / span choice / feed read, growth at the wrap, shrink, dry ramp and
  the drain. `sk_plateTick` is **noinline** (not part of the four loops) to
  keep flash under the threshold; the loop bodies are otherwise per the
  schedule. `SK_FEED_GAIN_CBT` is 0.5 (CrumpBit's wet sum).

### S7. Tables

- `tools/plots/gen_space_tables.py` writes `SpaceTables.h/.c` and `--check`
  passes C1-C5.
- **Halfband:** the 11-tap side taps are `c1 = 0.300`, `c3 = -0.061`,
  `c5 = 0.011` (a numeric search, not Kaiser beta 4.1): measured +0.01 dB
  to 5 kHz, -2.1 dB at 9 kHz, -58.8 dB above 17.06 kHz. Kaiser 4.1 only
  reached -42.9 dB here.
- **Table storage:** every generated table is `int16_t` with a per-table
  scale (`SPACE_*_S`, Q12-Q24 depending on the law). This is a post-plan
  change taken for flash: the float tables alone were ~13 KB. Values are
  converted per block, never per frame.

### S8. Parameters

- Three descriptor tables, the three tokens/abbrevs/full names, the `tim`,
  `spr`, `fbk`, `xfb`, `hed`, `brk`, `pre`, `dcy`, `frz`, `int`, `dtn`,
  `env`, `rtg` and `dck` value hooks, and the target hooks for the stepped
  rows. `tim` and `pre` clamp through `space_timeLimit()` /
  `space_preLimit()`, the same search as `effective_max`.

### S9. Space types

- The 448 B state header (tape and plate unions) and the three processes.
  The Delay has the varispeed tape, glides, brake, reverse head, three
  heads, ducking, hold, tone SVF, degradation and the O8 compression; the
  Reverb has the half-rate figure-8 plate with shimmer and freeze; the Echo
  is the mono tape into a fixed 0.6 plate with the three routings.
- **Deviation:** the Delay uses one loop with a NULL/non-NULL feed block
  instead of two compiled variants (flash, ~3 KB); the `space_cpu.py` cap
  still counts it. The gate now mutes a tape read per frame (`valid + n`),
  not per block, so the first repeat is heard on its due frame.

### S10. CrumpBit

- Geometry: `CRUMPBIT_LOOP_MAX_BYTES` 126,144, `CRUMPBIT_DELAY_CURVE_MAX`
  126,142, `CRUMPBIT_SEAT_MIN` 32 KB; the loop starts at share offset 512
  through `sk_spanBytes()`. `crumpBit_init(rt, handoff, share)` enters
  through `sk_feedBegin()` (mono fold) and seeds `mix_prev` from the
  handoff dry. `crumpBit_exportHandoff()` writes the five sub-types' D24
  segments. `crumpBit_growSpan()` grows the single line/word ring at the
  wrap (`grn` keeps its reseat). `crumpBit_feedPost()` adds the fed block to
  the output and writes it into the cells this block wrote (8-bit
  truncation, FDN lanes for `rvb`). `crumpBit_delayLimit()` is exported;
  `crumpBit_formatDelayRow()` is shared with the Space `tim` labels.
- Host `crumpbit` passes with 0 failures after the prefix updates (CB1/CB4/
  CB6 reference spans, `CB_ARENA`, the CB8 byte offset, the init signature).

### S11/S12. Build and host tests

- Root `Makefile`: Space include path, the two DSP sources in `DSP_SRCS` and
  the `-Ofast` rules; `make all && make img` builds.
- `tools/dsp_test`: `space` and `space_tables` targets; the CrumpBit target
  now extracts SpaceKit/SpaceTables; `space.c` covers K-1..K-9, P-1, D-1,
  D-4 and G-1 with the host stand-ins for the real `dly` curve (CB1 owns
  that curve). `space` passes with 0 failures.
- `space_elf_check` and `space_cpu.py` targets exist; they are the S13
  gates and were not run in this session (the looppath pass on the large
  loops is slow).

### Measured resources (S13, `make all`)

| | Before | After | Change |
|---|---:|---:|---:|
| Flash | 691,100 | **736,916** | **+45,816** |
| Headroom | 62,564 | **16,748** | -45,816 |
| ITCM | 13,984 | **13,560** | -424 |
| DTCM statics | 4,396 | **4,392** | -4 |
| FxBuffer arena | 126,656 | **126,656** | 0 |
| `fxbuf_handoff_t` | 180 | **196** | +16 (D18) |

- The plan's flash estimate was +18 KB; the delivered code is +45.8 KB. The
  int16 tables and the noinline plate tick recovered ~9 KB of that; the
  remaining difference is the three engines' code (the plan's per-frame
  instruction estimates did not price the instruction count of the DSP
  source). Headroom is above the 16 KiB warning threshold by 364 B only:
  **S095 P2 and the compressor adjustments need a flash decision first.**
- CPU budget: the D19 re-budget (Effect 16,970, Advanced 21,635) is
  unchanged on paper; the S13/S14 `fx` + `fxsvc` measurement is still the
  gate that decides it.
- **CrumpBit steady (`crumpbit_cpu_check`):** `grn` 180.8 -> **185.3**,
  `rvb` 179.4 -> 183.7; the others +4-5. The added per-block work is
  `sk_spanBytes()`/`sk_spanOffset()` and the feed/growth tests in
  `crumpBit_process()` (the schedule expected under 0.2 per frame; LTO
  inlined more). The Makefile cap is **186**, funded from the D19 re-budget
  (+336 cycles a block); the feed's own cost still uses the locked slot.

### Host test results

- `modulation`: 0 (with L7).
- `crumpbit`: 0.
- `space`: 0.
- `space_tables`: OK.
- `make -C tools/dsp_test all`: exits 0, but `voice_control` reports 6
  failures in its deferred-trigger laws; no S095 file is on that path, so it
  is recorded as observed and not attributed to this change.
- `crumpbit_elf_check`: still stops at the pre-existing `crumpBit_process`
  rank-2 probe (S0 baseline note).

---

## S18. Re-implementation (2026-11-12, after the reset)

The first implementation (§S17) is in `Helicase-failed-P1/` (gitignored)
for reference only. This section records the second implementation:
budgets, the design changes against the schedule above, and measured
figures. Every number below is measured unless it is marked **estimate**
with its basis.

**Current state (2026-11-12, after the user's review round, §S18.6):**
- P1 flash **+19,936 B** (691,100 → 711,036; user line < 22 KB).
- ITCM 14,032 (+48, accepted), DTCM statics 4,396 (0), FX arena 126,656
  (0), `fxbuf_handoff_t` 196 (+16, D18).
- Every CPU gate passes:

  | Gate | Measured | Cap |
  |---|---:|---:|
  | CrumpBit | 182.9 / 202.9 | 184 / 204 |
  | Delay | 194.7 / 214.7 | 202.9 / 222.9 |
  | Reverb | 189.7 / 209.6 | 191.8 / 211.8 |
  | Echo | 193.1 / 213.1 | 199.2 / 219.2 |

- Host `space` (SP1–SP10), `crumpbit` and `modulation`: 0 failures.
  `elf_p4_check` exact.

§S18.3 and §S18.5 record the earlier measurements of each step.

### S18.1 Phase budgets (flash, the user's line; each phase stops if it misses)

| Phase | What | Budget | Measured |
|---|---|---:|---:|
| A | kit (`SpaceKit.h/.c`, `SpaceKitDsp.c`), manager (ids, init with the share, drain, logical type, lock), FxBuffer (+16 B record), slot lock, CrumpBit (prefix, feed, export) | ≤ 5.5 KB | **+3,932 B** after the second rework (§S18.4; first rework +4,600) |
| B–D | Delay, Reverb, Echo, their rows and page text, the stopped-transport idle | 12.5 KB | **+16,004 B** (695,036 → 711,036 link; `symdiff` +15,166) |
| — | Tables | 1.5 KB | 0 (laws computed at a row change) |
| | **Total** | 18 KB (later: < 22 KB, user) | **+19,936 B** |

Phase A by group (`symdiff` against the S0 ELF): kit +2,460, CrumpBit
+1,366, manager/FxBuffer +340, other +428 (LTO re-layout of unrelated
functions, the lock in `modNode_resetTargets()` +56). Largest functions:
`sk_feedBegin` 836, `sk_feedMix` 748 (four copies of its frame loop: two
formats × the wrap split), `crumpBit_growSpan` 364, `crumpBit_seatBlock`
320, `crumpBit_exportHandoff` 308, `crumpBit_spanRateLimit` 288,
`sk_drainStep` 284. Record: `baselines/s095p1/symdiff_phaseA.txt`.

### S18.2 Design changes against the schedule

R1's seat rule, R4's growth-wait term and R7 were withdrawn by the second
rework (§S18.4). The text below marks them.

- **R1. The feed is the first segment, read in place (SD7 withdrawn, SD5's
  packing reason gone).** `sk_feedBegin()` reads the record's first
  segment: a tape's L or mono head, a reverb's first line. There is no
  packing, no stereo head and no start-aligned sum. A source of more than
  two segments (a reverb's lines) plays its one line at ×2 gain and fades
  over its end, since a ring line ends abruptly. The record still
  describes every segment (D24, the export is unchanged). *(Withdrawn,
  §S18.4: the seat beside the head. Now the incoming type uses the whole
  share and `sk_feedLimit()` cuts the head where its writes would reach
  it.)* The wrap domain must hold at least 64 samples.
- **R2. The feed is heard, not injected (D22's loop injection reversed).**
  `sk_feedMix(share, owner, l, r, frames, mix0, mix1)` adds the head × gain
  × end fade × the incoming type's wet mix (ramped across the block) to
  the type's output, after its engine and before its output filter. The
  tail is not written into the new loop, so the new feedback does not
  repeat it. One frame loop, a stepped nearest-sample read (Q16 phase:
  speed × rate / 44,108, so a varispeed tape keeps its pitch), no wrap
  test per frame (the block is split once where the head wraps). With mix 0
  the head still reads silently, so it stays in time. It returns 0 when the
  head ends; the slot lock clears then.
- **R3. No Space loop variants (SD8 withdrawn).** The Space engines keep
  one loop each and call `sk_feedMix()` after it, as CrumpBit does. No
  `sk_feedRead()`, no stack block, no `crumpBit_feedPost()`, no second
  compiled variant.
- **R4. The feed fits the Effect's own budget; D19's Advanced remainder is
  withdrawn.** Nothing is taken from Advanced for the feed. The slot lock
  (one interp slot held while a feed reads, D19) stays. CrumpBit's gates:
  steady cap **184** (accepted; was 181), feed cap **204**. The feed term
  is `sk_feedMix`'s loop and per-block path. *(The growth-wait term went
  with the seat, §S18.4.)* The Space types' caps follow the same rule
  (§S18.5).
- **R5. The slot lock lives in `modNode_resetTargets()` (S5.2
  superseded).** It works as a phantom grant: the active count starts each
  block at the lock, and the reserve is capped at MAX − lock, only while
  locked. `modNode_interpClaim()` is unchanged, so it stays inlined in the
  ITCM blend functions. `instrumentManager_itpFadeSlotAvailable()` compares
  against MAX − lock. Host law L7 is updated for it.
- **R6. Code size.** Every kit function that runs once per type change,
  share change or growth is `noinline, cold`. Per-file `-Os` has no effect
  under `-flto` (the image came out byte-identical), so the Makefile does
  not use it. The kit's per-frame half (`SpaceKitDsp.c`) is built
  `-Ofast` with the DSP sources.
- **R7. CrumpBit's seat fast path** *(withdrawn, §S18.4: CrumpBit plays
  in the whole share; `feed_bits` holds CRUMPBIT_FEED_RUN and
  CRUMPBIT_FEED_LIMIT)*. `rt->grow_hold` held three bits:
  - `CRUMPBIT_HOLD_GROW`: a growth waits for the wrap;
  - `CRUMPBIT_HOLD_FEED`: a feed runs;
  - `CRUMPBIT_HOLD_SEAT`: the seat lives in the control block.

  While SEAT is clear, the span is the usable share, read from the share
  alone, as before S095. A feed block reads the span straight from the
  control block (the seat cannot move during a feed). Other SEAT blocks
  call `crumpBit_seatBlock()` (noinline, packed return), which grows the
  span at its wrap and clears SEAT once the seat is whole.
- **R8. Tool fix.** `elf_looppath.py` now decodes `tbb`/`tbh` jump tables.
  The S0 baseline's `grn` reads **181.5** with it (180.8 before), recorded
  in `crumpbit_cpu_before_fixedtool.txt`.

### S18.3 Phase A measured (first rework; superseded by §S18.4)

| | Before (S0) | Phase A | Change |
|---|---:|---:|---:|
| Flash | 691,100 | **695,700** | **+4,600** |
| ITCM | 13,984 | **14,032** | +48 (accepted; LTO re-layout) |
| DTCM statics | 4,396 | 4,396 | 0 |
| FX arena | 126,656 | 126,656 | 0 (512 B prefix inside it) |
| `fxbuf_handoff_t` | 180 | 196 | +16 (D18) |

CrumpBit instructions per frame (`crumpbit_cpu.py --cap 184 --feed-cap
204`, worst case per sub-type). The feed term is 20.0: a 16-instruction
loop plus 127 per block / 32. The growth wait is 7.1.

| Sub-type | Before (fixed tool) | Steady | With the feed |
|---|---:|---:|---:|
| `dly` | 157.4 | 159.5 | 179.5 |
| `cmb` | 153.6 | 155.7 | 175.7 |
| `pit` | 153.8 | 155.8 | 175.7 |
| `grn` | 181.5 | **183.7** | **203.7** |
| `rvb` | 180.2 | 182.0 | 202.0 |

The `grn` margins are 0.3 against both caps. An LTO re-layout in a later
phase can move `grn` by a few instructions per block, so each phase
re-runs this gate.

Gates:

| Gate | Result |
|---|---|
| `elf_p4_check P4BASE=baselines/s095p1` | 96/96 oscillator loops exact; 59 mixer hot loops equal. One oscillator loop (`osc_wtbFmBlendRampBlock`, 74 instructions) changed register allocation under LTO. Re-snapshotted as accepted: `osc.txt` is now phase A, `osc_before.txt` keeps S0. |
| `crumpbit_elf_check` | Every probe up to the obsolete `crumpBit_process --rank 2` line passes. That line fails on the S0 ELF too and stays as it is (user). The two probes after it, run by hand, pass: `crumpBit_delayLimit` 152 ≤ 152, `crumpBit_tableLimit` 63 ≤ 63. |
| Host `crumpbit` | 0 failures. CB24 is new (see below). Every break is caught. |
| Host `modulation` | 0 failures (L7 for the phantom lock). |

**CB24** (new, `modules/crumpbit.c`) tests `sk_feedMix()` against a
per-frame-wrap reference:
- 20,000 random heads;
- domains of 64 samples and up, strides 1/2/4, int8 and int16;
- steps from 0.25 to exactly 1;
- short heads.

It also checks that the feed and the lock clear when the head ends. With
the wrap subtraction removed, it fails.

Records in `baselines/s095p1/`:
- `link_budget_phaseA.txt`
- `crumpbit_cpu_phaseA.txt`
- `elf_p4_phaseA.txt`
- `symdiff_phaseA.txt`
- `osc.txt` (phase A) and `osc_before.txt` (S0)

### S18.4 Second rework of phase A: no seat (user, 2026-11-12)

The seat machinery is gone: `crumpBit_growSpan`, `crumpBit_seatBlock`,
`crumpBit_spanRateLimit`, `sk_gap`, `sk_spanGrow`, the HOLD_SEAT/GROW bits,
growth at the wrap (SD9) and `seat_min`.
- **Every buffer user plays in the whole usable share above the prefix.**
- **The new type starts its write heads at the tail's end**
  (`sk_feedEnd()`), so its writes move away from the tail.
- **`sk_feedLimit()` cuts the tail before the first sample a write head
  would reach less than two blocks after the feed reads it.** It is an exact
  solve over at most two tail pieces and two pieces per writer, with a
  fade.
- **A ring the type clears at init is a rate-0 writer.** A tail inside the
  Reverb's 64 KB or the Echo's 16 KB ring is dropped.
- **CrumpBit limits the feed again when it enters another sub-type** during
  a feed (`CRUMPBIT_FEED_LIMIT`).
- **A share change keeps HEAD's rule:** keep the newest audio if the write
  index fits, else restart.

New host law **CB25**: the limit against brute force, within one sample. It
fails if the cut is disabled.

Phase A, measured: **+3,932 B**. CrumpBit `grn` 183.0 steady and 203.0 with
the feed; caps 184 and 204.

### S18.5 Phases B–D: Delay, Reverb, Echo (as built)

`SpaceEffect.h/.c` holds the DSP and ops (`-Ofast`). `SpaceParameters.h/.c`
holds the descriptors and one shared page-text formatter. They are
registered as ids 3–5, `EFFECT_TYPE_COUNT` 6.

- **Laws:** computed in `write_param` (a row change), through a per-type
  row→law table, with the `exp2f`/`sqrtf` already linked. No tables were
  generated; `gen_space_tables.py` was not written. Nothing with libm runs
  per block or per frame.
- **Size policy:**
  - everything outside a frame loop is `optimize("Os")`; per function it
    survives LTO, per file it does not;
  - every frame loop is single-copy: no unswitch, peel, unroll or
    predictive commoning (`SPACE_LOOP`, `SPACE_ONCE`), so each loop body is
    one frame or one tick and the CPU gate's count is exact.
- **Stepped rows (`hed`, `frz`, `rtg`):** stored 0..127 and read in equal
  zones (`space_stop()`), so the whole knob travel steps through them in
  order.

**Delay (`dly`, 15 rows):** `mix tim spr fbk xfb ton deg wow hld rev hed
dck`.
- **Tape:** one fused stereo loop, a linear main read, a packed L|R int16
  write with guard frames, and same-frame cross-feed (`xfb`).
- **`tim` and `spr`:** `tim` is CrumpBit's `dly` curve, clamped to the
  tape at the 1/4 speed floor (D3). `spr` is relative: the R/L ratio
  0.5..2, and the longer channel plays `tim`, the other `tim` / ratio, so
  neither ever needs more tape than `tim`.
- **`hed`:** seven stops in knob order, 1 2 3 12 13 23 123 (D/3, 2D/3,
  D), default "3". The extra heads are nearest-frame reads at L's time for
  both channels.
- **`rev`:** two windowed heads read backwards through L's last D.
- **`hld`:** no input, the loop at unity. The soft clip is
  x − x³/3 (slope 1), so the loop sustains and does not grow (SP8).
- **`ton`, `deg`, `wow`, `dck`:**
  - `ton` caps the playback LP on its low side and adds a one-pole HP on
    its high side;
  - `deg` sets drive, bandwidth and hiss, with O8 compression per block;
  - `wow` is wow, flutter and drift;
  - `dck` is the ducking.
- **The `valid` gate:** the heads are gated until `valid` (frames the
  write head has entered, counted exactly) covers the delay.

**Reverb (`rvb`, 21 rows):** `mix pre siz dcy dif lsh lsf hsh hsf mod wid
frz shm int dtn env dck ton`.
- **Tank:** Dattorro figure-8 at 22,054 Hz in a 64 KB int16 ring with a
  32-entry guard mirror (reads through per-block bases at 4 instructions,
  no mask).
  - two input allpasses;
  - a modulated allpass per half with a linear fractional read;
  - two-corner shelves in the loop (D7);
  - three taps per side, M/S width.
- **Around the tank:**
  - the input: mono and its peak fused into the 11-tap halfband
    decimator;
  - predelay at half rate after the decimator;
  - the size geometry remade every 8th block (D12 glide).
- **Shimmer:** two voices (D8), nearest reads, evaluated every 8th tick,
  one block behind.
- **`frz`:** three zones: off, on, and on with the shimmer (O12).
- **`env`, `dck`:** `env` gate and bloom (O10); ducking.
- **`ton` (new, §S18.6):** a one-pole low-pass per output side at the
  plate rate. At 127 (the default) its coefficient is exactly 1, so the
  sound is unchanged.
- **Output:** linear 2× interpolation fused with the mix.

**Echo (`ech`, 17 rows):** `mix tim fbk ton deg wow e>r bln dcy lsh hsh hed
dck rtg`.
- **Tape:** mono, linear read, the Delay's `hed` stops, `dck`. The routing
  inputs are folded into the tape loop.
- **Plate:** one half of the figure-8 at quarter rate (11,027 Hz) in a
  16 KB ring with the guard mirror; three taps per side; fed by a 4:1
  average of the echo.
- **`rtg`:** three zones. e>r feeds the plate from e2r × echo + 0.1 ×
  input. r>e and loop take the plate's last block (0.7 ms).
- **Output:** `bln` blend, fused with the 4× interpolation and the mix.

**All types:**
- **Export (D24):**
  - the tape types export their tape, one segment per lane;
  - the Reverb exports its four tank delay lines;
  - the Echo exports the tape only (D15).
- **Stopped-transport idle (new, §S18.6; `sk_idleBefore/After()`,
  `effects_returnGain()`):** while the transport runs, nothing changes.
  While it is stopped, every type keeps processing until its output stays
  below −90 dBFS for 14 blocks. Then the Effect return ramps to 0 over 1 s
  (in the mixer's return gain, which ramps across its block). Then
  processing stops and the outputs are zeroed until input above −90 dBFS
  returns (it wakes at full gain) or the transport starts. The state lives
  in the arena control block's spare bytes: no RAM growth.

**Changes from the plan text (my calls, §S18), each to fit the caps:**
1. **Delay main head:** a linear read, not the 4-point Hermite of
   §4.1/D10.
2. **Delay delay floor:** 36 samples. CrumpBit rows 0–1 play 0.8 ms.
3. **Delay extra heads:** D/3, 2D/3 and the reverse heads are
   nearest-frame reads at L's time for both channels.
4. **Delay `ton` and DC:** no SVF (a capped LP plus a one-pole HP) and no
   DC block (the clip is odd).
5. **Reverb tank:**
   - two input allpasses (plan: four), three taps per side (plan: six);
   - no input bandwidth one-pole;
   - linear output interpolation;
   - the shimmer evaluated every 8th tick with nearest reads (grainy by
     design).
6. **Echo plate:**
   - quarter rate (top about 5 kHz), one half of the figure-8, three taps
     per side;
   - fixed diffusion 0.7, corners 250 Hz / 4 kHz, modulation 3.
7. **`brk` removed** from the Delay and the Echo (user, §S18.6).

**CPU (`make space_cpu_check`, instructions per frame).** The count covers
every frame loop at its iterations, the per-block paths, the loops inside
per-block code, and what the stopped-transport idle adds (2.0). The caps are
(16,634 − (1,070 + 124.5 × rows)) / 67.5, as CrumpBit's (plan §1), plus 20
for a feed block; nothing comes from Advanced.

| Type | Rows | Steady | Cap | With the feed | Cap |
|---|---:|---:|---:|---:|---:|
| Delay | 15 | **194.7** | 202.9 | **214.7** | 222.9 |
| Reverb | 21 | **189.7** | 191.8 | **209.6** | 211.8 |
| Echo | 17 | **193.1** (every routing's loops counted) | 199.2 | **213.1** | 219.2 |

**Host laws (`make space`, 0 failures):**
- **SP1:** the Delay returns at the `tim` delay on L and R (7,890
  samples). At `spr` 126, R plays `tim` and L half of it.
- **SP2:** 4 s of noise at fbk 127 and deg 127 through every `hed`, `rev`,
  `hld`, `spr` and `tim` change stays bounded.
- **SP3:** the Reverb tail decays (RT60 near the `dcy` law). Freeze plus
  shimmer stays bounded for 10 s.
- **SP4:** the Echo's three routings are bounded and distinct.
- **SP5:** a Delay tail feeds a Reverb with the lock set; both clear.
- **SP6:** `hed` steps 1 2 3 12 13 23 123 in order across 0..127, and the
  default is "3".
- **SP7:** at the longest `tim` (the clamp) the long channel returns at
  the curve's time ±1 %, for `spr` 0 and 127.
- **SP8:** `hld` at fbk 127 does not build (second 4 below second 1).
- **SP9:** Reverb `ton` 0 cuts the high band, and 127 is flat.
- **SP10:** the idle ramp is at 0.5 at mid-ramp; processing stops at
  block 1392 (14 quiet + 1,378 ramp); −60 dBFS input wakes it at gain 1.

**Gates:** `elf_p4_check` 96/96 and 59 mixer hot loops equal (the mixer
calls the out-of-line `effects_process()` and the return gain);
`crumpbit_cpu_check` 182.9 / 202.9; host `crumpbit` (CB24, CB25) and
`modulation` 0 failures; `crumpbit_elf_check` passes up to the obsolete
rank-2 probe (user: leave it). Space frame loops have 0 VDIV.

### S18.6 Review round (user, 2026-11-12) and what changed

| Report | Cause found | Change |
|---|---|---|
| Delay/Echo: `brk` does nothing | — | Removed: the rows, their law, the runtime speed, the lane (Delay: free lane; Echo: `lsh` on it). |
| Delay: some `tim`/`spr` settings cut off | Two bugs. (1) The `valid` gate needed valid > D + 4 while valid saturates at the tape length, so the longest delays never opened. (2) valid advanced by a per-block rounding of the speed, lagging ~4 % at the 1/4 speed, so long delays stayed gated past their first repeat. `spr` could also ask R for twice `tim`. | The gate is valid ≥ D + 2; valid counts the frames the write head enters; `spr` is relative (the longer channel is `tim`). SP7. |
| Delay/Echo: `hed` knob goes down or does not move | The row was 0..6 on a 0..127 knob (most of the travel clamped at 6), and its stops were labelled 3 2 1 13 23 12 123. | 0..127 in seven zones, ordered 1 2 3 12 13 23 123, default "3"; `frz` and `rtg` in three zones likewise. SP6. |
| Delay: `hld` builds far too fast | The soft clip 1.5x − 0.5x³ has a small-signal gain of 1.5, so the loop gain was 1.5 × `fbk` (and "unity at 106" was 1.5). | Clip x − x³/3 (slope 1, 2/3 at the clip): `hld` sustains, `fbk` unity is unity. SP8. |
| Reverb: add a `ton` low-pass | — | New row `ton` (`rvb_tone`, default 127 = flat). SP9. |
| Reverb tail cut off at transport stop | No code path stops or clears the Effect at stop: the mixer runs the Effect every block; stop resets the FX-step locks (rows return to their base values) and sends note-offs. A host test re-applying every row mid-tail decays smoothly. The cut was not reproduced; the likeliest cause is a lock reverting at stop. | The stopped-transport idle (above) for every type: processing always continues until the output is below −90 dBFS, then the 1 s ramp, then off. |

To fit the new row and the idle inside the caps, I also made these cuts:
- the Reverb's shimmer every 8th tick (was every 4th);
- no input bandwidth one-pole;
- the input mono fused into the decimator;
- the idle meter at four samples a block.

### S18.7 Remaining

- **Flash:** +19,936 B against the < 22 KB line; overall savings later
  (user).
- **Not done:**
  - the device run (§S14: `fx` + `fxsvc` per type, and the 67.5 cycles per
    instruction-frame behind the caps; the idle's ramp and wake-up by ear);
  - the plan's full law list beyond SP1–SP10;
  - break builds for the SP laws.
- **Left as is (user):** the obsolete `crumpbit_elf_check` rank-2 probe.

Records in `baselines/s095p1/`:
- `link_budget_phaseBCD.txt`
- `symdiff_phaseBCD.txt`
- `space_cpu_phaseBCD.txt`
- `crumpbit_cpu_phaseBCD.txt`
- `space_host_phaseBCD.txt`
- `elf_p4_phaseBCD.txt`
