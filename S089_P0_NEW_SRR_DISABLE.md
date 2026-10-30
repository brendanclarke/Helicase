# S089 Phase 0 (new) — Per-Instrument SRR Enable

**Status:** Implemented 2026-10-30 (Steps 0–3, §7); hardware H0–H6 **passed** (user, 2026-10-30)
**Replaces:** `S089_P0_SRR_MOVE.md` (moving SRR into the engines) and the
schedule in `S089_P0_IMPLEMENTATION.md`
**Prerequisite for:** `S089_P1_VOICE_CHAIN_PLAN.md`
**Baseline:** HEAD `b4d3db8f`, clean source tree

---

## 0. Goal

By default, sample-rate reduction (SRR, the S&H decimator `mixer_decimateBlock()`)
does not run. The instrument that owns a slot decides whether it runs. The four
existing types (Drum, Snare, Cymbal, HiHat) turn it on under their default
voice chain, which is the only chain today. Their sound and their CPU cost stay
exactly as they are now. A later chain (P1) that leaves SRR out gets back the
cost of the S&H loop for its own stages.

What stays where it is: the S&H state (the three DTCM arrays), the algorithm,
the `srt` descriptor rows, the `INSTRUMENT_BIND_SLOT_DECIMATION` binding and
all LFO/velocity paths. Nothing moves into the engine structs.

---

## 1. Findings: why the earlier attempts failed

`S089_P0_SRR_MOVE.md` §9.6 concludes that `mixer_calcNextSampleBlock()` sits at
an I-cache cliff, and that any growth or branch makes the firmware freeze. The
measurements below do not support that conclusion. Run them before trusting
any further hardware result.

### F1. Since `f3805478`, `make all && make img` never rebuilds the image (blocking)

Commit `f3805478` moved `tools/link_budget.py` to
`tools/diagnostic_tools/link_budget.py`. Makefile line 187 still calls the old
path. So `make all` links the ELF and then fails with `make: *** [all] Error 2`
(reproduced at HEAD). Because of the `&&`, `make img` never runs, and
`build/LXRV2_lxr02.img` keeps whatever image was there before.

So every hardware test built with the documented Quick Start command since
`f3805478` may have flashed an old image. That includes the S089 attempts. On
the shared tree, `build/lxr02.elf`/`.bin` are Attempt E (14:29, bin
677,012 B), while `build/LXRV2_lxr02.img` is the committed HEAD image
(676,700 B, identical to a fresh HEAD `make img`).

The fix is one line (§3, step 0). The script itself runs fine from its new
location (checked on the spike ELF: flash headroom 76,644 B, ITCM
12,656 / 16,384 B, exit 0).

### F2. Attempt E did not raise CPU at boot, so a boot freeze cannot be a cycle or I-cache cost

- **Per-sample hot loops.** `elf_hotloops.py`, HEAD vs the Attempt E ELF:
  29 equal, 6 shrank, 2 grew by one instruction each. The S&H loop went
  10 → 11 because of a stack reload (`ldr r6, [sp, #12]`). The other is a
  layout change in one dry-mix routing case (§2.3).
- **Inlined set.** No function joined or left the set inlined into the mixer.
  Only six symbols changed size, each by a few bytes.
- **Boot order.** `main.c` `dsp_init()` calls `instrumentManager_runtimeInit()`
  **before** `mixer_init()`. Attempt E's `mixer_init()` set every enable to 0
  after InstrumentManager had set it to 1. So at boot SRR was **off** in every
  slot until the next slot reset, and the render did less work than at
  baseline.

A slower render shows up as a stall under load. It cannot freeze a boot where
the render is cheaper than baseline. The freeze has some other cause: a stale
image (F1), state on the SD card, or the layout shift in F4. The record never
shows a control test of the baseline image on the same card.

### F3. The "+1,352 B cross-TU barrier" (Attempts C/D) is inliner budget, not spills

Spike P put the `mixer_decimateBlock()` call at the tail of
`instrumentManager_calcSlotSyncBlock()`, which is in an -O2 file. The call then
stayed out of line (112 B). LTO spent the inlining budget it freed by inlining
`busComp_processBlock()` (1,484 B) into the mixer. Result: mixer
13,508 → 14,572 B, while total `.text` **shrank** by 184 B.

The mixer's size in bytes is therefore not a cost measure. Use these two gates
instead:
1. Per-sample hot-loop length (`elf_hotloops.py`).
2. The set of functions inlined into the mixer: a symbol diff of the two ELFs
   shows only `mixer_calcNextSampleBlock` changing.

### F4. Every failed attempt also changed the SRAM1 `.bss` layout

- Attempt E added 6 B (8 B after alignment). 806 SRAM1 data symbols moved by
  +8 B.
- Attempts A/B grew `runtime_slots` by 48 B.

Alignment of 8 B or less is kept, so no aligned object broke. Still, a latent
out-of-bounds write that hits a different neighbour after a shift fits a freeze
that does not scale with code size. The design below adds **no RAM**, so
`.bss` stays byte-for-byte where it is at HEAD. Hardware step H0 (§4.3) tells
the possible causes apart.

---

## 2. Design

### 2.1 Mechanism

InstrumentManager already owns the per-slot facts the mixer reads every block:
`instrumentManager_runtimeVolume()`, `instrumentManager_runtimePan()` and
`instrumentManager_gateGain()`. LTO inlines all three. Add a fourth:

```c
uint8_t instrumentManager_slotUsesSrr(uint8_t slot);
```

It returns 1 when the slot's runtime type is one of the four known types (their
default chain includes SRR). In every other case it returns 0: unknown or empty
slot, and any future type. The mixer calls the S&H block only when it returns 1.

The answer comes from `runtime_slot_type[slot]`. Only
`instrumentManager_runtimeInit()` and
`instrumentManager_resetRuntimeSlotFromScene()` write that value, and the
render already reads it through `instrumentManager_slotType()`. Because of
this:
- **No new state:** nothing to initialise, and no `mixer_init()` ordering
  hazard (F2).
- **No new RAM:** `.bss` layout is identical to HEAD (F4).
- **No sync points:** a Scene or type rebind changes the answer at the same
  moment it changes the engine that renders.

### 2.2 CPU mandate (`CPU_USE_DSP_AUDIT.md` §Binding Mandates)

The answer depends only on the instrument type, and from P1 on the chain
selector. It never depends on the `srt` value, on whether a voice is silent, or
on any other runtime activity. At `srt` 127 the S&H loop keeps running.

This is a structural choice of chain, budgeted at the worst case, so it is not
"switching off DSP work because it is currently inactive". An empty slot
already skips the whole engine render (`memset`). Skipping the S&H on zeros
there matches that behaviour, and the worst-case Scene has all six slots
filled. The user requested this change (S089).

### 2.3 Spike measurements (arm-none-eabi-gcc 14.2, `-O2 -flto`, DSP files `-Ofast`)

All variants were built from HEAD in a scratch worktree, which has since been
removed.

| Variant | Mixer B | `.text` | `.bss` | Inlined set | Hot loops vs HEAD | S&H loop |
|---|---:|---:|---:|---|---|---:|
| HEAD | 13,508 | 676,264 | 412,884 | — | — | 10 |
| E: gate byte array in mixer.c | 13,736 | 676,592 | 412,892 | unchanged | 29 =, 6 −, 2 + | 11 |
| P: SRR call at the tail of the IM type switch | 14,572 | 676,080 | 412,884 | **+busComp**, decimate out of line | 31 =, 4 −, 2 + | out of line |
| R: SRR trip count 0/32 from IM | 13,760 | — | 412,884 | unchanged | 35 =, 0 −, 2 + | 10 |
| **Q: mixer gate on IM predicate (this plan)** | **13,848** | **676,600** | **412,884** | **unchanged** | **36 =, 0 −, 1 +** | **10** |

**The one loop that grows in Q** also grows in E, P and R: one routing case of
the saturating dry-mix loop (27 → 28 textual). It is a block-layout change.
The low-clamp test now branches forward on the common path into a copy of the
4-instruction tail (`adds; str; cmp; bne`), where HEAD fell through. The
instructions executed on the common path are the same (27). It adds one taken
branch per sample in that routing case only, which is at most about one cycle
per sample. No variant avoided it, because any edit to the mixer body
triggers it.

Q's S&H loop is opcode-identical to HEAD (10 instructions). The cost of the
predicate is one byte load plus a range test per slot per block, about
0.1 instruction per sample.

**Flash** grows by 336 B. **RAM**: none.

---

## 3. Changes

Every hunk below matches spike Q. Comments do not affect code generation, but
every gate in §4 must still be re-run on the real build.

### Step 0 — Makefile: fix the `link_budget.py` path (F1)

```diff
-	python3 tools/link_budget.py $(PREFIX)nm $(BUILD)/$(TARGET).elf
+	python3 tools/diagnostic_tools/link_budget.py $(PREFIX)nm $(BUILD)/$(TARGET).elf
```

After this, `make all` exits 0 and the Quick Start `make all && make img`
rebuilds the image again. Do this first, in its own build, and confirm that
`make all && make img` prints `Written:build/LXRV2_lxr02.img (…b) OK` before
any hardware test.

### Step 1 — `Core/DSP/Instruments/InstrumentManager.h`: declaration

Place it directly after the `instrumentManager_runtimeVolume()` declaration,
the mixer-facing accessor group:

```c
/*
 * Report whether a slot's voice chain includes sample-rate reduction (S089 P0).
 *
 * What: 1 when the instrument type that owns the slot's live runtime runs a
 * voice chain whose last stage is the mixer's S&H decimator
 * (mixer_decimateBlock()); 0 otherwise. SRR is off by default: an unknown or
 * empty slot, and any type or chain that does not list it, returns 0. Drum,
 * Snare, Cymbal and HiHat return 1 under their default chain.
 *
 * Why: lets an instrument give the S&H cost to another chain stage
 * (S089 P1) without moving the decimator state out of the mixer's DTCM
 * arrays. Constant CPU: the answer depends only on the runtime type (and,
 * from P1, the chain selector), never on the srt value or voice activity.
 *
 * Inputs: zero-based render slot 0..5. Output: 0 or 1. Clients:
 * mixer_calcNextSampleBlock(), once per slot per 32-frame block, foreground
 * render context only. Affiliates: instrumentManager_slotType(),
 * INSTRUMENT_BIND_SLOT_DECIMATION (still writes mixer_decimation_rate[] for
 * every type, so the srt value is kept across chain changes).
 */
uint8_t instrumentManager_slotUsesSrr(uint8_t slot);
```

### Step 2 — `Core/DSP/Instruments/InstrumentManager.c`: definition

Place it directly after `instrumentManager_calcSlotSyncBlock()`, the position
used in the spike:

```c
uint8_t instrumentManager_slotUsesSrr(uint8_t slot)
{
    /*
     * Each type's default voice chain ends in SRR (S089 P0). P1 adds the
     * chain selector here: a type returns 1 only when its selected chain
     * includes SRR. Unknown/empty slots and new types default to 0.
     */
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM:
    case INSTRUMENT_TYPE_SNR:
    case INSTRUMENT_TYPE_CYM:
    case INSTRUMENT_TYPE_HAT:
        return 1u;
    default:
        return 0u;
    }
}
```

`instrumentManager_slotType()` is already forward-declared at line 215 and
range-checks `slot`.

Comment update in `instrumentManager_calcSlotSyncBlock()` (line ~1977):
"Mixer remains responsible for decimation" → "Mixer remains responsible for
decimation (only for slots where instrumentManager_slotUsesSrr() is set)".

### Step 3 — `Core/DSPAudio/mixer.c`: gate the call

```diff
 		instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);
-		mixer_decimateBlock(slot,sampleData);
+		/*
+		 * S089 P0: SRR runs only when the slot's instrument chain includes it
+		 * (instrumentManager_slotUsesSrr(); off by default, on for the four
+		 * types' default chain). Decided by type/chain only, never by the
+		 * srt value, so the worst case stays constant.
+		 */
+		if (instrumentManager_slotUsesSrr(slot))
+			mixer_decimateBlock(slot,sampleData);
```

Also update two comments:
- The slot-loop comment (line ~1002): "mixer state chooses routing,
  decimation, …" → "mixer state chooses routing, decimation (when the slot's
  chain includes it), …".
- The `sampleData` comment (line ~1017): "the decimated, pre-volume voice
  block" → "the pre-volume voice block (decimated when the chain includes
  SRR)".

**Do not touch** `mixer_init()`, the three DTCM arrays,
`mixer_decimateBlock()`, `mixer.h`, the descriptor rows, the binding or the
modulation helpers.

### Behaviour change (intended, and only this one)

An unknown or empty slot used to run the S&H on its zero buffer. If the
previous instrument had left `srt` below 127, the slot could output that
instrument's last held sample as a constant DC level. It now outputs exact
silence. No UI path normally leaves a slot unknown; this only happens briefly
during a type rebind. For the four types the output is unchanged (S0).

---

## 4. Verification

### 4.1 Build

```
make all && make img
```

Both must succeed, and the `Written: … OK` line must appear. Check that
`build/LXRV2_lxr02.img` is newer than `build/lxr02.elf`. Keep a copy of the
HEAD ELF (`build/` is not a frozen snapshot: copy it before editing, per
MEMORY.md).

### 4.2 Static gates (no hardware)

1. **Inlined set unchanged:** a symbol diff (`arm-none-eabi-nm -S`, types
   t/T/w/W) of the HEAD and new ELFs lists only `mixer_calcNextSampleBlock`
   (spike: 13,508 → 13,848 B). Fail if any other function appears,
   disappears or changes size (for example `busComp_processBlock` or
   `mixer_decimateBlock` showing up as its own symbol).
2. **Hot loops:** `elf_loopdump.py --sym mixer_calcNextSampleBlock` on both
   ELFs, then `elf_hotloops.py --compare`. Expected: 36 equal, 0 shrank,
   1 grew (the dry-mix layout case, §2.3). Any other growth fails.
3. **S&H loop opcode-identical** to HEAD: 10 instructions, `vadd / vcmpe /
   vmrs / blt / ldrsh / vsub / strh / strh / cmp / bne`, with no stack load.
4. **`.bss` byte-identical** to HEAD (spike: 412,884), and no SRAM1/DTCM data
   symbol moved (compare `nm -n` addresses).
5. **`link_budget.py`** runs from `make all` and reports flash headroom
   ≥ 16 KiB.
6. Host tests: `make -C tools/dsp_test` `instrument_manager` and
   `special_tags` pass. The existing `run_modes` bench gap (it declares
   `distortion_curveSample16`) is S088 debt and not part of this gate.

### 4.3 Hardware (in this order; stop at the first failure)

| # | Image | Check |
|---|---|---|
| H0 | **HEAD** (`b4d3db8f`, the committed `build/LXRV2_lxr02.img`) | Same SD card and the same Scene/Bank used in the failed S089 tests: boot, a worst-case six-slot Scene switch, note the `cpu` widget. **If H0 freezes, the freeze is not caused by SRR**: investigate card state first and do not continue. |
| H1 | Step 0 alone (Makefile fix, so source identical to HEAD) | Built image is byte-identical to H0's image; nothing to flash. Confirms the pipeline. |
| H2 | Steps 0–3 | Boot; worst-case Scene switch; `cpu` matches H0. |
| H3 | Steps 0–3 | `srt` 127 on each of the four types: A/B against H0 shows no audible difference. |
| H4 | Steps 0–3 | `srt` ≈ 20 on a sustained sound of each type: the S&H staircase sounds the same as H0. |
| H5 | Steps 0–3 | LFO → `SampleRt`: smooth sweep, returns to base when cleared. Velocity → `SampleRt`: onset modulation, restores on retrigger. |
| H6 | Steps 0–3 | Scene switches that change slot types (Drum ↔ HiHat and so on) with `srt` < 127: no clicks or DC, and SRR stays active after the rebind. |

**Result (2026-10-30): H0–H6 passed on hardware (user).**

The off path (returns 0) cannot be reached from the UI with today's four
types. It is first exercised from the panel once P1 adds a chain without SRR.
Until then, static gates 1–3 are its proof.

---

## 5. Hand-off to P1 (`S089_P1_VOICE_CHAIN_PLAN.md`)

- **Extending the predicate.** P1 extends `instrumentManager_slotUsesSrr()`,
  and only that function. Each type case returns 1 only when its `chainType`
  selects a chain that includes SRR, read through the existing typed runtime
  accessors. Keep it a pure function of type plus chain, with no cached copy.
- **Budget.** The default chain's budget now explicitly includes the S&H loop.
  It runs in the mixer but is charged to the chain, so it sits outside the
  ≈93-cycle engine stage figure in P1 §1. It costs 10 instructions per sample
  (about 12–15 cycles, estimated). A chain without SRR may spend that 10 in its
  own stages, and no more (P1 §1.1). The integer S&H in §8 was reverted.
- **The `srt` cell.** Under a chain without SRR, the cell still writes
  `mixer_decimation_rate[slot]`, but nothing reads it. P1 has to decide
  whether to show it as inactive or relabel the cell. This plan keeps the
  value so that switching back to the default chain restores it.
- **Gates for P1.** P1 changes must pass the same §4.2 gates. In particular,
  run gate 1 (inlined set) after every edit, because the mixer is at the
  limit of LTO's large-function growth budget (F3).

---

## 6. After hardware passes

- Mark the conclusions in `S089_P0_SRR_MOVE.md` §9.6 and the "Fix 1"
  root-cause section of `S089_P0_IMPLEMENTATION.md` as superseded by §1 of
  this plan. Their measured numbers stand; the I-cache explanation does not.
- `CPU_USE_DSP_AUDIT.md`, under Session 089 additions: per-instrument SRR
  enable, no per-sample change for the four types, and the layout-only
  dry-mix +1.
- `EFFECTS_MIXER_DSP_REFERENCE.md` §3: decimation is conditional on
  `instrumentManager_slotUsesSrr()`.
- Suggested `MEMORY.md` DSP rule, if the user agrees: "Mixer byte size is not a
  cost measure. Gate mixer-path changes on `elf_hotloops.py` plus an
  inlined-set symbol diff; LTO re-spends freed inline budget (S089)."

---

## 7. Implementation record (2026-10-30)

Steps 0–3 were applied as written. The build is function-for-function
identical to spike Q (a symbol diff against the Q ELF is empty).

- `make all` exits 0; `link_budget.py` reports flash headroom 76,644 B,
  ITCM 12,656 / 16,384 B, DTCM statics 4,372 B.
- `make img`: `Written:build/LXRV2_lxr02.img (677020b) OK`, file 677,036 B.
- Gate 1: only `mixer_calcNextSampleBlock` changed (13,508 → 13,848 B).
- Gate 2: 36 loops equal, 0 shrank, 1 grew (the dry-mix layout case, 27 → 28).
- Gate 3: the S&H loop has 10 instructions, opcode-identical to HEAD, with no
  stack load.
- Gate 4: text 676,264 → 676,600; data 420 and bss 412,884 unchanged; no data
  symbol moved.
- Gate 6: `instrument_manager` 0 failures (the break builds were caught as
  expected); special tags OK (167 rows, 0 mismatches).
- Hardware: H0–H6 passed (user, 2026-10-30).

---

## 8. Follow-up: integer S&H (SRR efficiency fix, 2026-10-30) — REVERTED

**Reverted 2026-10-30 (user decision; see §8.6).** §8.1–§8.5 are kept as a
record of what was built and measured. None of it is in the tree.

### 8.1 Change

`mixer_decimateBlock()` now runs on a Q31 phase instead of a float counter.

- **Per sample:** `cnt += inc`. If bit 31 is set, latch the input. Clear bit
  31, then output the held sample.
- **Exact end points:** `inc = 0x80000000` latches every sample (exact
  pass-through from any carried phase); `inc = 0` holds forever.
- **Files:**

| File | Change |
|---|---|
| `Core/DSPAudio/mixer.c` | `mixer_decimation_rate[6]` (float) → `mixer_decimation_inc[6]` (uint32, Q31); `mixer_decimation_cnt[6]` float → uint32; `mixer_init()` seeds `inc = 0x80000000`; loop rewritten with a contract comment |
| `Core/DSPAudio/mixer.h` | extern renamed and retyped |
| `Core/DSP/Instruments/InstrumentManager.c` | `INSTRUMENT_BIND_SLOT_DECIMATION` converts the shaped rate: `>= 1` → `0x80000000`, `> 0` → `(uint32_t)(rate × 2^31)`, else 0; two comments renamed |
| `InstrumentManager.h`, `{Drum,Snare,Cymbal,HiHat}Parameters.c` | comments renamed |

RAM is unchanged. The two arrays keep their 4-byte elements and their DTCM
addresses: `mixer_decimation_inc` sits at `0x20000f5c`, where `_rate` was, and
no other data symbol moved.

### 8.2 Measured (linked ELF, against the hardware-passed P0 build)

- **The S&H loop.** GCC places the latch block out of line.
  - **No latch:** 6 instructions, `adds / add / bmi / cmp / strh / bne`. The
    input is not even loaded.
  - **Latch:** 7 instructions; the branch out runs `cmp / ldrsh / bic / bne`.
    No store is needed there, because the output equals the input.
  - **Worst case** is `srt` 127, the default, where every sample latches:
    **7 instructions** and one extra taken branch. It was 10 instructions
    plus a `vcmpe`/`vmrs` stall.
- **All other mixer loops are unchanged.** Comparing the full set of loop
  bodies, only the SRR loop differs; two other loops (42 and 58 instructions)
  were reordered by the scheduler with the same length and opcodes.
  `elf_hotloops.py` reports "1 grew (10 → 14)". That is a pairing artifact:
  the vanished SRR loop was matched to a 14-instruction loop that already
  existed. For later checks, use this build as the gate 2/3 baseline.
- **Inlined set unchanged.** `mixer_calcNextSampleBlock` 13,848 → 13,776 B,
  `instrumentManager_writeRuntimeInternal` +60 B, `mixer_init` −16 B,
  `effects_activateScene` +4 B.
- **Size and build.** `.text` 676,600 → 676,576 B; data and bss unchanged.
  `make all` exits 0; `make img`: `Written:build/LXRV2_lxr02.img (676996b) OK`.
- **Host tests.** `instrument_manager`: 0 failures. Special tags: OK. Both
  generated `im_gate.c` copies under `tools/dsp_test/build/` were
  regenerated with the renamed comment.

### 8.3 Sound class: S3 (needs a listening A/B)

The two forms are not bit-identical at fractional rates, measured in a host
harness over all 128 `srt` values and 60 s of samples:

- **Latch counts are identical.** The one exception is `srt` 98, off by one
  latch in 60 s.
- **Where the latches fall differs.** The float sum rounded on every add, so
  its latch grid drifted slowly against the exact integer grid. At most 14
  samples of phase over 60 s, at `srt` 2, where each hold lasts about 210
  samples.

So the rate and hold length are the same, and the slow phase drift of the
float version is gone. `srt` 127 (pass-through) and `srt` 0 (hold) are
exact.

The real `mixer_decimateBlock()` was extracted from `mixer.c` and checked
against the Q31 reference model, using the binding's own conversion. Result:
0 mismatches over 128 rates × 5,000 blocks of random input; 100,000
pass-through blocks with random carried state; and a hold at `inc = 0`.

### 8.4 Budget consequence (user, 2026-10-30)

The SRR credit available to a P1 chain that drops SRR is reduced from 10 to
**7 instructions per sample** (≈8–9 cycles, estimated). The per-type
ceilings in `S089_P1_VOICE_CHAIN_PLAN.md` §1.1 use the reduced figure:
Drum/Snare ≈101–102 cycles and Cymbal/HiHat ≈96–97, against default chains
of ≈93 / ≈88.

The 3 instructions saved per slot per sample on the four default chains are
general headroom. They do not raise any chain's ceiling.

### 8.5 Hardware (pending)

| # | Check |
|---|---|
| H7 | Boot; worst-case six-slot Scene switch; the `cpu` widget reads at or below the P0 build. |
| H8 | `srt` 127 on all four types: no difference from the P0 build. |
| H9 | Listening A/B (S3): `srt` 2, 20, 60 and 100 on a sustained sound of each type, against the P0 build: the same staircase and the same pitch of the aliasing. |
| H10 | LFO and velocity → `SampleRt`: sweep and restore as in H5. |

### 8.6 Reversion (2026-10-30)

**What was tried and dropped.** A log `srt` curve was also proposed: one
semitone per step, `rate = 2^((srt − 127)/12)`, about 29 Hz at 0, through a
128-entry Q31 table. Its build was stopped before it ran.

**Decision (user).** Keep the original implementation: the float S&H and the
original `valueShaperI2F(v, -0.7f)` `srt` curve. The 3-instruction saving made
little difference to overall CPU, and the user preferred the existing
behaviour.

**How it was reverted.**
- The four `*Parameters.c` files and `mixer.h` were restored from HEAD.
- In `mixer.c`, `InstrumentManager.c` and `InstrumentManager.h`, the
  follow-up hunks were undone; the §3 P0 changes were kept.

**Verified.**
- `make all` exits 0.
- `make img`: `Written:build/LXRV2_lxr02.img (677020b) OK`.
- `build/lxr02.bin` is **byte-identical** to the P0 build that passed H0–H6.
- `instrument_manager`: 0 failures. Special tags: OK. The generated
  `im_gate.c` files match HEAD again.
- Checks H7–H10 do not apply.

**Budget.** The SRR credit stays at 10 instructions per sample.
`S089_P1_VOICE_CHAIN_PLAN.md` §1.1 is restored to the ceilings Drum/Snare
≈105–108 cycles and Cymbal/HiHat ≈100–103.
