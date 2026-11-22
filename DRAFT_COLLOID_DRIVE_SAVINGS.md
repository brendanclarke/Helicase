# DRAFT — Colloid final drive and tone: the CPU saving of removing them

**Date:** 2026-11-22. **Branch:** `dev-ph7-alkaline` (the S101 P2 tree).
**Status:** a draft for your decision. Nothing here is built. The figures
are measured on scratch builds of the linked image; the device has not run
either form.

**The decision:**
1. **Keep the final drive and tone as they are,**
2. **drop the final drive,** or
3. **drop both the final drive and the tone.**

---

## 1. What the stage is

Both Colloid types (the Colloid and the Advanced Colloid) end their render
with the post-filter stage of S095 P3 F10, "the same post-filter drive as for
Alkaline":

| Row (MIX page) | Field | Default | What it does |
|---|---|---|---|
| `instrument_drive` ("drv") | `vcaDrive` | 0 | the overdrive: `z = clamp(y x alkOvdGain[drv], ±3)` (x1 .. x32), the Alkaline OTA's saturator `z (27 + z²) / (27 + 9 z²)` (one division), x `alkOvdMakeup[drv]`, crossfaded in over its first 8 steps (`COL_POST_WET_STEPS`) |
| `col_tone` ("ton") | `tone` | 127 (open) | a one-pole low-pass after the drive, `alkToneC[ton]`: 127 open and exact, lower values roll off toward about 800 Hz |

**Where it runs:** once per sample after the filters and before the amp.
- In the stereo route (`col_renderPar()`): `col_postPair()`, one division
  for both channels, then a tone pole per channel.
- In the mono routes (`col_renderSer()`): `col_post()`, one division, and one
  tone pole.

**At the defaults the stage is already a pass-through.** At `drv` 0 the
output is the filter output exactly (law CO-POST); at `ton` 127 the tone is
exact. It still costs its full price every sample (constant cost).

**What is not affected:** the **pre-filter drive** (the filter page's `drv`,
`drive` x1..x16 into the cubic `col_drive()`, with the right-filter offset
`dro`). It stays either way. A Colloid without the final stage still drives
its filters.

---

## 2. What removing them saves (measured)

**Method:** two scratch builds of the S101 P2 tree, built into the scratchpad
and then discarded, with the tree restored and checked:
- **(a)** the drive call replaced by a pass-through (`ol = yl; orr = yr` /
  `y = y * amp`);
- **(b)** the same plus the tone pole removed (`tzL = ol`, …).

The figures come from `colloid_cpu.py` and `elf_looppath.py` on each linked
image.

**Instructions a sample:**

| Mode | As built | (a) No final drive | (b) No drive, no tone |
|---|---:|---:|---:|
| `pwm6-par` (stereo route: the worst) | 400.8 | 363.2 (**−37.6**) | 355.1 (**−45.7**) |
| `plain12-par` | 416.2 | 378.7 (−37.5) | 370.6 (−45.6) |
| `clx-pwm6-par` (Advanced Colloid) | 447.7 | 410.1 (−37.6) | 402.0 (−45.7) |
| `pwm6-ser` (mono routes) | 373.9 | 353.5 (−20.4) | 349.4 (−24.5) |

**The filter and output loop per sample:**

| Loop | As built | (a) | (b) |
|---|---|---|---|
| stereo | 121 instructions, 1 division | 85, no division | 77 |
| mono | 99, 1 division | 79, no division | 75 |

**In cycles per Colloid-engine voice** (× 32 × 1.51, the Colloid's measured
cycles an instruction):

| | Stereo route | Mono routes |
|---|---:|---:|
| Drop the drive | −1.8k, up to −2.2k | −1.0k, up to −1.4k |
| Drop the drive and the tone | −2.2k, up to −2.6k | −1.2k, up to −1.6k |

The "up to" is the division. The instruction count treats the saturator's
division as one instruction, but it takes about 14 cycles and does not
pipeline. Removing it can save up to about 0.4k a block more than the count
shows. Only the device can say how much of that is real.

**The tone on its own** was not built. On top of the drive removal it is −8.1
a sample (stereo), about −0.4k per voice. It is the smaller part.

### 2.1 The forms that keep the drive (measured, rejected)

| Form | Result |
|---|---|
| A polynomial in place of the division (CL2, S101 P2 §4.4) | Not within S1: degree 7 is off by 1.3 × 10⁻², degree 9 by 4.4 × 10⁻³, neither monotone |
| A 256-point linear table in place of the division | Sounds the same (worst difference −80 dB under the output, at `drv` 8; drive 0 exact). But it **costs as much as the division**: the stereo loop goes 121 → 133 instructions with the division gone; with a guard entry about +6. It is cycle-neutral, so it saves nothing |

**There is no cheap middle form.** The choice is the stage or its saving.

---

## 3. What it does to the Scenes

The device figures from `SD_CARD_S101_P2_2` (before S101 P2's final reverts),
less the per-voice saving of §2 at the stereo route. These are estimates:

| Scene | Colloid-engine voices | Measured | Drop the drive | Drop both |
|---|---:|---:|---:|---:|
| 4 Colloids + 2 Advanced Colloids | 6 | 140.9k | ≈ 127.7–130.1k | ≈ 125.3–127.7k |
| 4 Tetrakts + 2 Advanced Colloids | 2 | 143.3k | ≈ 138.9–139.7k | ≈ 138.1–138.9k |
| Bank 076 Scene 5 (one Colloid, one Advanced Colloid) | 2 | 140.4–143.7k | ≈ 136.0–140.1k | ≈ 135.2–139.3k |

**The strict worst-case estimate** (S101 P2 §7, CL3 out, six-engine cycles
per instruction: 4 Colloids + 2 Advanced Colloids, 154.0k) becomes about
140.8–143.2k (drive) or 138.4–140.8k (both). That is under the 144k cap
without relying on the one-engine cycle gain.

**Who gains:** Scenes with many Colloid-engine voices. A Scene with four
Tetrakts gains only through its two Advanced Colloids. The four-Tetrakt Scene
then becomes the worst case.

---

## 4. What it costs in sound and features

- **The rows go.** The MIX page loses `drv` (`instrument_drive`) and `ton`
  (`col_tone`) on both Colloid types. The rows are a user feature from
  S095 P3 (F10): the post-filter overdrive "the same as for Alkaline".
- **Existing patches change, with no migration** (the standing rule):
  - a Colloid or Advanced Colloid saved with `drv` above 0 loses its final
    saturation and its make-up gain, so it is cleaner and, at high drive,
    louder or quieter by the make-up;
  - one saved with `ton` below 127 loses its final roll-off, so it is
    brighter.
  - Patches at the defaults (`drv` 0, `ton` 127) do not change at all: the
    stage is a pass-through there today.
- **The stored bytes:** with the rows gone, the values stored in old files
  are ignored on load. No converter (the standing rule).
- **NRPN / MIDI Learn:** `instrument_drive` is the shared voice-drive key
  (the NRPN voice-drive group). For the Colloid types it would address
  nothing. It still drives the other types that have it (the Alkaline).
- **Not measured:** how many of your saved Colloid patches use `drv` above 0
  or `ton` below 127. Before deciding, it is worth scanning the card's
  instrument files for those two bytes (S101 P2 has the decoders).

---

## 5. If you choose to drop it: what would be built

1. **`ColloidVoice.c`:**
   - `col_renderPar()` and `col_renderSer()` lose the drive call (and the
     tone pole for option 3), with a comment block saying what and why.
   - `col_postPair()` / `col_post()` go if nothing else uses them.
   - The async drops `postGain`, `postMakeup`, `postWet` and `toneC`.
   - The voice drops those fields, `vcaDrive`, `tone` and the tone states
     (`toneZL`, `toneZR`). The voice gets smaller; no RAM change.
2. **`ColloidParameters.c`:** the two rows removed from the MIX page.
3. **`InstrumentManager.c`:** `IM_SPECIAL_INSTRUMENT_DRIVE`'s Colloid branch
   removed.
4. **Host laws:**
   - CO-POST (the stage's exactness at drive 0) retires with the stage;
   - CO-RTE and the other render laws re-run;
   - a new law for the shortened chain (filter → amp → output, against the
     old chain at `drv` 0 / `ton` 127, bit-exact), with a break.
5. **Gates and checks:**
   - `elf_s099_check`: the render loops change by design; the rest must
     stay exact;
   - `colloid_cpu.py` to the figures of §2;
   - the device run of the Scenes of §3.

---

## 6. Decisions

| Q | Question | Note |
|---|---|---|
| Q-D1 | Keep, drop the drive, or drop the drive and the tone? | Drive: −1.8 to −2.2k per Colloid-engine voice; both: −2.2 to −2.6k |
| Q-D2 | If dropping: scan the card's Colloid instrument files for `drv` > 0 / `ton` < 127 first? | Tells you which patches would change |
| Q-D3 | If dropping: is `instrument_drive` on the Colloid types simply gone, or should it be re-pointed to the pre-filter `drv`? | Re-pointing changes what an existing NRPN / Learn mapping does on a Colloid |
