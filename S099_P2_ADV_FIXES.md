# S099 P2 — Advanced fixes after the first device round (2026-11-19)

Input: the user's device round on `SD_CARD_ADV/` (a DEV_MODE_LOGGING build of
the S099 P1 §15.10 tree, image 733,160 B) and these notes:
- the Colloid Advanced file would not save to the library (the OK press froze
  the screen, though other modes could still be entered);
- REx is too close to R-S by ear, so it can go and its flash comes back;
- R-S needs a narrower width at its narrowest;
- how is pos spread applied when an LFO modulates pos? A fully modulated pos
  should fully modulate every spread position, on every osc type;
- the R-S low-frequency alias is a little too audible: what are the options?

The user dropped a separate question (the tone change above a low C) as a
spurious parameter. For the record, the stock octave rows switch exactly at
the Cs (32.7, 65.4, 130.8 Hz …; `osc_octaveEdgeHz[]`), so a row change
would land just above one.

Done in this part, as instructed: §1 REx removal, §2 the narrower R-S, §3 the
save fix, §4 the card numbers. §5 and §6 answer the two questions; neither is
implemented (§5: left as it is, the user's decision after the simulation in
§5.1; §6: deferred until the device tests). §9 is the later Effect-page
change (done).

---

## 1. REx removed (done)

The Colloid and Alkaline pwm lists:

| Engine | Before | After |
|---|---|---|
| Colloid (Basic and Advanced) | 0 Tri, 1 Saw, 2 Rec, 3 R-S, 4 REx | **0 Tri, 1 Saw, 2 Rec, 3 R-S** (`COL_PWM_WAVE_COUNT` 4) |
| Alkaline (Basic and Advanced) | 0 Saw, 1 Sqr, 2 Tri, 3 R-S, 4 REx | **0 Saw, 1 Sqr, 2 Tri, 3 R-S** (`ALK_PWM_WAVE_COUNT` 4) |

A stored REx byte is not migrated (no migration, as before):
- **Colloid:** a stored 4 plays Rec, through the clamp past the list.
- **Alkaline:** a stored 4 plays R-S, because `alk_renderPwm()` tests
  `>= ALK_WAVE_RS`.
- **Menu:** both show a blank name until the value is edited.

**Code:**
- **`tools/plots/gen_pwm_tables.py`:** the REx curve (`rex_s`, `rex_S`,
  `CHOP_REX_K`) and every REx table are gone. `oscChopTables.c/.h` are
  regenerated and keep `oscChopCurveRs`, `oscChopGainQ14`, `oscChopSpanF` /
  `InvSpanF`, and the R-S DC, level and release tables. `OSC_CHOP_REX_K` and
  `OSC_CHOP_LREF_REX` are gone.
- **`ColloidVoice.h`:**
  - `COL_PWM_REX` is gone; `COL_PWM_WAVE_COUNT` 5 → 4;
  - the `_Static_assert` is now `OSC_PWM_WAVE_COUNT + 1`.
- **`ColloidVoice.c`:** `col_chopBlock()` lost its `form` argument and the
  REx selects.
- **`AlkalineVoice.h`:** `ALK_WAVE_REX` is gone; `ALK_PWM_WAVE_COUNT` 5 → 4.
- **`AlkalineChop.c`:** `Alkaline_renderChop()` lost its REx selects.
- **Menu names:** `MenuText.h` `colPwmWaveNames` / `alkPwmWaveNames` are
  {4, …}, and the asserts in `menu.c` are updated.
- **Comments only:** the R-S/REx comments in `ColloidOsc.c/.h`,
  `ColloidParameters.c`, `DiodeLadder.c/.h`, `InstrumentManager.c` (the wave
  domain) and `AlkalineVoice.c/.h`.
- **Laws:**
  - AL-TY: pwm now clamps to R-S, and pwm R-S → wav gives Tri;
  - AL-CH: R-S only, and the reference render's second half is now R-S at
    pos 127 (it was REx at 96);
  - CO-CHP, CO-PWG and the CO-PWM form bus: R-S only.
- **Tools:** `render_chop_audition.py` (R-S only, on Rec's widths) and a
  comment in `colloid_cpu.py`.

## 2. R-S narrows to 0.39 % (done)

- **Before:** R-S used the Saw width table `oscPwmDSaw`, 512 → 26 points of
  the gate cycle (narrowest 2.54 %). That was D-5 in S099 P1.
- **Now:** it uses Rec's `oscPwmDRec`, 512 − 4·pos points (narrowest
  4 points, **0.39 %**), linear in pos like every other pwm width law. pos 0
  is unchanged (50 %, the Alpha Juno Saw 2; law T15). This supersedes D-5.
- **Level:** the peak-keeping level stays tame: ×1.78 at pos 0 and ×2.00 at
  pos 127 (`OSC_CHOP_LREF_RS` 1.996, was 1.976). The generator's range check
  T12 passes at every width down to 4 points.
- **Colloid code:** `col_oscControl()` already writes Rec's widths for every
  pulse form, and `col_chopBlock()` used to overwrite them with Saw's. Those
  two writes are gone, so the chop now uses the values already there. CO-CHP
  gained a check that every R-S width equals `oscPwmDRec[pos]`.
- **Alkaline code:** `Alkaline_renderChop()` reads `oscPwmDRec[k]`.
- **If 0.39 % is too narrow by ear:** a dedicated chop width table with any
  end point costs 256 B. Say which end.

## 3. Colloid Advanced library save: the freeze (fixed, not yet tried on the device)

**What the trace shows.** In `asavetrc.bin`:
- the nested Instrument page opened (its `LOAD_LIBRARY_INDEX` reads are
  there);
- **no Instrument-save request was ever recorded.** An accepted request
  writes a lifecycle record (`filesystem_requestSaveInstrumentMode()`), and
  only Kit requests appear.

So the OK press was refused, and nothing reported the refusal.

**Cause.** `menu_instrumentSaveRequestSelection()` (nested Instrument Save):
- called the request and simply did nothing when it was refused;
- the click had already toggled `editModeActive`, so the OK row was left "in
  edit mode", where the encoder does nothing. This is exactly the freeze
  S097 P3 described and fixed for the top-level Load/Save rows; the nested
  path never got that fix.
- The usual refusal is a busy filesystem: a trace flush (about once a second
  in a DEV build), an AutoSave drain, or a name read. That explains why it
  hit here, and it is not specific to clx; any type could do it.

**Fix (`Core/Menu/menu.c`):**
- **On refusal:** `menu_loadSaveDropClick(MENU_LS_CLICK_REFUSED)` restores
  the toggle, and the press is held for replay (`menu_loadSaveOkRetry`, up
  to `MENU_LS_OK_ATTEMPTS` 4, replayed by `menu_loadSaveRetryOk()` once
  storage and Preset are idle). This is the top-level rows' rule.
- **On acceptance:** the click is now traced (stage `k`, ACCEPTED).
- **`menu_loadSaveOkRowHoldable()`** now admits nested Instrument *Save*.
  Nested Instrument Load is still excluded.
- **Build order:** the `MENU_LS_CLICK_*` constants moved up beside
  `MENU_LS_OK_ATTEMPTS`, and three forward declarations were added.

**To confirm on the device:** save a clx (and an alx) to the library while
playing. A DEV build should then show stage `k` records (REFUSED / HELD /
RETRY, then ACCEPTED) and a lifecycle REQUEST of type Instrument. If a
refusal is still followed by no file, the next step is the filesystem's own
reason, and the stage `k` value carries the filesystem status.

## 4. The card's cycle numbers

**Source:**
- `SD_CARD_ADV/asavetrc.bin` was decoded with
  `tools/diagnostic_tools/decode_devlogs.py`;
- the S099 session is trace records 1,232,243 onward: 2,101 probe windows of
  2,048 blocks each (about 1.5 s);
- slot 3 was clx, with tkt, drm, alk, col and hat in the other slots, and no
  alx anywhere;
- each per-slot figure is a window **average**; only the whole-mixer maximum
  is a true per-block peak.

| Measure | Before S099 on the same card (records 1.15 M–1.23 M) | S099 session | Cap |
|---|---|---|---|
| slot 3: col (Basic) → clx (Advanced), cycles/block | median 19,008, p95 20,267, max 21,482 | **median 21,034, p95 22,397, max 23,076** | Basic 19,620 → Advanced **21,803** |
| clx block-rate / render (median) | — | 5,121 / 15,963 | — |
| clx windows over 21,803 | — | **333 of 2,101 (15.8 %)** | — |
| slot 2 alk (Basic) | median 16,033, max 16,132 | median 16,109, max 16,408 | 19,620 |
| slot 4 col (Basic) | median 16,960, max 19,134 | median 17,338, max 17,977 | 19,620 |
| whole mixer, average (largest window) | 145,295 (92.7 %) | **147,890 (94.4 %)** | 152,004 (97 %) |
| whole mixer, per-block peak > 100 % of a block | 1,282 of 3,201 windows | 929 of 2,108 windows | — |

**What the numbers say:**
- **D-G5 (Advanced Colloid at p95): the median passes, by 769; p95 is over,
  by 594.**
  - The Advanced step costs **+2,026 cycles at the median** (21,034 against
    the same slot's Basic 19,008). The host model predicted 2,025 at CPI 1.4
    (S099 P1 §15.2), so the instruction model holds.
  - The p95 failure comes from this Scene's Colloid itself: as a Basic voice
    in the same slot its p95 was already **20,267, over the Basic share
    (19,620)**. The S096 P3 record (p95 19,562) was a lighter setting.
  - The settings behind each window are not in the trace (oscillator count,
    form, chorus mode).
- **The whole mixer is inside the 97 % cap:** the largest average window is
  94.4 %.
- **Per-block peaks above one block are not new:** 40 % of windows before
  S099 and 44 % during it. That is a separate, pre-existing question about
  what produces the peaks (the slot averages are flat).
- **The Alkaline (Basic) is unchanged** within noise. This round tested no
  Advanced Alkaline.
- **Next step for D-G5:**
  - C1 (drop the chorus pre-pole, about −100 cycles a block) does not close
    a 594-cycle gap on its own;
  - lowering the clx's own worst case does. Options: the voice-count cap
    under clx; the half-rate chorus's mid form; or pricing the clx's
    dearest Colloid mode at the Advanced share minus the chorus (2,026),
    i.e. ≤ 19,777, which this Scene's Basic Colloid (p95 20,267) is not.
  - This needs your decision. One controlled device run per mode (clx pwm 6,
    wtb 12, chop 6, each with chorus types 1–3) would locate the worst case.

## 5. Question: pos spread under LFO modulation (answer; not implemented)

**How it works today.** An LFO, velocity or Morph on pos writes the modulated
value into `osc.position` once per block (the modulation writer, clamped
0..127), and the spread is then applied on top. By osc type:

- **pwm** (`col_pwmTarget()`):
  - each oscillator's width target is `fold(pos + r_i·k·63.5)`, where r_i is
    the oscillator's draw (−1..+1), k = `col_pspK(psp)` (0..5), and the fold
    is a triangle at 0 and 127;
  - so an oscillator with offset +o sweeps from o up to 127 and turns back.
    **It never reaches the widths below o.** The bigger the spread, the less
    of the range each oscillator covers, and they bunch at the ends.
  - Separately, the width slew (2 steps a block, 46 ms end to end) limits
    fast LFOs: above about 10 Hz the sweep's depth shrinks for every
    oscillator, spread or not.
- **wts** (the scan): the same fold in frame space (`col_scanControl()`), so
  the same result.
- **wtb:** the frames are **latched at the trigger** (C-D4, CO-WTB). A
  modulated pos moves **nothing** during a held note; it only picks the
  frames at the next trigger, and the spread there **wraps** (modulo the
  frame count), it does not fold.
- **wav:** pos is not used by the Colloid's wav ("---").

**Options, if every spread position should cover the whole range when pos
does:**

| Option | What it does | Sound | Cost |
|---|---|---|---|
| A. Keep the fold (today) | — | each oscillator's sweep is cut by its offset and turns back at the ends | — |
| B. Range-preserving spread (recommended) | each oscillator gets its own monotone map from pos to target, through (0, 0), (63.5, 63.5 + o_i, held inside 1..126) and (127, 127) | every oscillator sweeps 0 → 127 when pos does; spread is widest at the centre and zero at the ends | a compare and a multiply-add per oscillator per block in `col_pwmTarget()` / the scan's target (control rate; nothing per sample) |
| C. Wrap instead of fold | `(pos + o) mod 128` | full range, but a jump from the narrowest width to 50 % mid-sweep; the slew turns it into a fast sweep | about the same |
| D. wtb follows pos during a note | drop the latch: frames step as pos moves | a step between frames (the reason C-D4 latched them); wts already does this with interpolation | per block, as wts |

~~My recommendation is B for pwm and wts.~~ Superseded by §5.1.

### 5.1 Follow-up: the phase lead/lag model, and the decision

**The user's model:** spread as a lead/lag in the phase of the pos
modulation. At psp 0 no oscillator leads or lags pos; at psp 127 the
oscillators' phases are spread evenly across the modulation cycle and stay
that way.
- **It is possible, but only LFO-aware.** Each oscillator would compute
  `base + depth × shape(φ + δ_i)` from the LFO driving pos, with even
  offsets δ_i.
- **pos alone cannot give it:** the voice sees neither the LFO's phase nor
  its direction.
- **Costs:** plumbing, a few bytes of RAM per Colloid voice, and about +2 to
  +4 instructions a sample on pwm and wts. wts is the Colloid's dearest mode.

**Simulation (the question: which gives the greatest harmonic change and
phase-cancellation change?).**
- Six detuned pulse oscillators, the Rec width law, a full-depth triangle LFO
  on pos, 110 Hz.
- "Harmonic swing" is the mean over 40 harmonics of each harmonic's swing
  over the cycle.
- "Level swing" is the swing of the stack's power against the sum of the
  oscillators' powers, i.e. inter-oscillator cancellation.

| Spread | Harmonic swing (no detune) | Level swing (no detune) |
|---|---:|---:|
| psp 0, all together | **34.2 dB** | 0.0 dB |
| today, random + fold, mid psp | 8.2 | **2.8** |
| today, random + fold, psp 127 | 9.4 | 1.2 |
| lead/lag over ¼ cycle | 6.7 | 1.9 |
| lead/lag over ½ cycle | 8.0 | 1.7 |
| lead/lag over the full cycle | **4.5** | **0.4** |

With detune on, its beating dominates both measures: harmonic swing 31–36 dB
and level swing 12–19 dB in every row.

**Findings:**
- **psp 0 gives the deepest harmonic sweep.** Every oscillator passes the
  width nulls together.
- **A moderate spread gives the most changing cancellation,** and today's
  model at mid psp gives the most of all.
- **A full-cycle even spread is nearly static:** the set of widths only
  rotates, so the sum barely changes.

**Decision (user, 2026-11-19): leave it.** No change to the spread law.


## 6. Question: the R-S low-frequency alias (measured; options)

**Measured on the host.** I used `render_chop_audition.py`'s law (R-S on
Rec's widths). I split the gate reads and the ramp read into truncated (T),
interpolated (I) or exact, and measured the inharmonic energy: everything
off the harmonic bins, relative to the total, over 1 s at pos 0 and pos 96.

| Note | pos | gate T, ramp T (**shipped**) | gate I, ramp T | gate T, ramp I | gate T, exact ramp | gate I, exact ramp |
|---:|---:|---:|---:|---:|---:|---:|
| 110 Hz | 0 | **−44.7 dB** | −56.8 | −44.8 | −44.8 | **−78.5** |
| 110 Hz | 96 | **−40.3** | −59.8 | −40.4 | −40.4 | **−74.5** |
| 440 Hz | 0 | **−49.9** | −56.7 | −50.1 | −50.1 | **−84.9** |
| 440 Hz | 96 | **−45.4** | −59.7 | −45.5 | −45.5 | **−83.1** |
| 1,760 Hz | 0 | **−53.6** | −55.9 | −54.1 | −54.2 | −66.0 |
| 1,760 Hz | 96 | **−48.9** | −54.2 | −49.3 | −49.3 | −56.9 |

**Findings:**
- **The truncated gate reads are the main source.** Both gate points are
  read at a whole table point, which puts the chop's edges on a 1/2,048-cycle
  time grid. That grid is not related to the note, so its error lands off
  the harmonics, low ones included.
- **The ramp's truncation matters only once the gate is interpolated**
  (−57 → −78 dB).
- **At 1,760 Hz** what remains is the products' bandwidth: the gate row's
  lift, not the reads.
- **Both engines ship truncated reads:** the Colloid's form T, and the
  Alkaline's `ALK_CHOP_READ_FORM 2`. The ladder's low-pass hides some of it
  on the Alkaline.

**Options:**

| Option | Effect (from the table) | Cost |
|---|---|---|
| E. Exact ramp instead of the curve table: `v = ((p + 2^29) >> 17) − 4,096`, now that R-S's curve is linear | alone ≈ 0 dB; with F, the full −75 to −85 dB | probably cheaper (no load); **frees 2,048 B of flash** (`oscChopCurveRs`) |
| F. Interpolate the gate (both reads) | −57 dB alone; −75 to −85 with E | Colloid: about +4 to +8 instructions per oscillator per sample, so +24 to +48 a sample on chop 6 (387.8 now). Against the Colloid's dearest mode (scan6-par 406.3) that leaves 18.5 a sample; full interpolation does not fit, and one shared fraction for A and B might. Alkaline: mode 5 is 220 a sample against its dearest 276.7, so form 0 (all interpolated) probably fits. Needs a linked count. |
| G. Raise the gate's lift (`OSC_CHOP_LIFT` 2 → 3) | helps the high notes (the 1,760 Hz rows), not the low-note alias | free; the top notes get a duller gate |
| H. Alkaline only: ship `ALK_CHOP_READ_FORM 0` | the full interpolated result on the Alkaline | inside the Alkaline's budget, probably; needs a linked count |

**Recommendation:** E now (it is free or cheaper and returns 2 KB). Then
measure F on the Colloid with one shared fraction, and F/H on the Alkaline,
and decide on the linked counts. Needs your decision.

**Decision (user, 2026-11-19): deferred** until the other changes are tested
on the device. Any fix must be the same for the Alkaline and the Colloid, so
H (the Alkaline alone) is out as a standalone option.

---

## 7. Gates (this tree)

| Gate | Result |
|---|---|
| `make all && make img` | no new warning. Flash **730,112 / 753,664 B** (−3,032 against S099 P1 §15.10's 733,144; headroom **23,552**). ITCM **15,432 / 16,384** (952 B free, unchanged). |
| `make -C tools/dsp_test all` | exit 0; every law 0 failures; all 87 breaks caught; `pwm tables OK` (T10–T15 on Rec's widths). AL-CH prints the chop form 2 against form 0 at −33.4 dB RMS, and R-S at pos 0 against Saw at −2.67 dB. |
| `elf_s099_check` | 15 of 15 exact. |
| `elf_p4_check P4BASE=baselines/s099p1/p4base` | 73 exact, 13 missing by text (as S099 P1 §15.10); mixer 58 equal, 0 grew. |
| Colloid host cost (`baselines/s099p2/colloid_cpu.txt`) | chop6-par 391.0 → **387.8**, chop6-ser 364.2 → **361.0** (the dropped width writes); every other mode unchanged. |
| Alkaline host cost (`baselines/s099p2/alk_cost.txt`) | `Alkaline_renderChop` 401 → 395, `alk_renderPwm` 413 → 407 per block; every other path unchanged (the dearest, wtb, unchanged). |

**Not verified on the device:** the save fix (§3), the narrower R-S by ear
(§2), and the REx removal on stored Scenes.

## 8. Statements now out of date (for the user)

- **`MEMORY.md`:**
  - the pwm lists (Colloid Tri Saw Rec R-S; Alkaline Saw Sqr Tri R-S);
  - R-S's widths are Rec's;
  - flash 730,296 B after §9 (headroom 23,368);
  - the Advanced types' effect rows: fx dpt rte mix, keys `fx_*`, with dpt
    carrying the chorus's mode and depth (§9).
- **`S099_P1_IMPLEMENTATION.md`:**
  - D-5 (the R-S floor 2.54 %) and D-11 (the REx k) are superseded;
  - §15.9's REx row has been applied (as REx's removal).
- **`S099_P1_PWM_ADVANCED.md` / `DRAFT_S-R_PWM_WAVE.md`:** REx as a shipped
  wave.
- **`DRAFT_ADVANCED_BBD_CHORUS.md` §5, §7.2 and `S099_P1_IMPLEMENTATION.md`
  (the chorus rows):** the cho / crt / cdp / cmx rows and the six-mode list
  are replaced by §9's fx / dpt / rte / mix.

## 9. The Advanced types' Effect page: fx, dpt, rte, mix (done)

**Request (user, 2026-11-19):**
- The chorus is an *effect*, and more may follow, so the Advanced types'
  parameter is `fx`, not `cho`.
- It is always the last page of the MIX SELECT button's clicks, and always
  has four cells:
  - **fx** (Effect Type): off, chr;
  - **dpt** (Effect Depth), a custom table: 0 `I`, 1 `II`, 2 `1+2` (the
    Juno modes, which ignore rate), then 3–127 moving smoothly through var
    and dbl, ending at dbl at full depth at 127;
  - **rte** (Effect Rate), 0–127, as now, and not blanked under I, II or
    1+2;
  - **mix** (Effect Mix), as now.

### 9.1 What changed

> **Superseded in part by §10:** the effect's screen is on page 4 (the 5th
> SELECT button, "click"), not MIX; csr, cbw, cdv and cwd are gone.

**The MIX page (SELECT 8), both Advanced types:**

| Type | Screen 1 | Screen 2 | Screen 3 (the effect) |
|---|---|---|---|
| clx | vol pan ton drv | csr | **fx dpt rte mix** |
| alx | vol pan ton drv | cbw cdv cwd | **fx dpt rte mix** |

- **The effect's screen is the last of the instrument's own screens.** The
  menu still appends the shared Scene-setting screen (route, FX send,
  fader, Voice Morph) after it, as on every type. If the effect should come
  after the Scene-setting screen too, that is a menu-order change; say so.
- **No screen before it may be empty.** A SELECT press advances only to the
  next screen that has a cell, so the effect's own extra rows (csr; cbw cdv
  cwd) sit directly before it.
- **Blank cells don't hide a screen.** A blanked cell still counts, so the
  csr screen stays reachable with fx off.

**The rows.** Descriptor positions are unchanged; the keys, labels and
meanings changed. There is no migration: an old `chorus_*` key no longer
loads.

| Key (was) | Short (was) | Long | Values |
|---|---|---|---|
| `fx_type` (`chorus_type`) | `fx` (cho) | Effect Type | 0 off, 1 chr (`ADV_FX_*`, `fxTypeNames`); a fixed selector whose writer claims or releases the voice unit (`IM_SPECIAL_FX_TYPE`, was `IM_SPECIAL_CHORUS_TYPE`) |
| `fx_depth` (`chorus_depth`) | `dpt` (cdp) | Effect Depth | 0–127 (`DTYPE_0B127_FX_DEPTH`): 0–2 display I, II, 1+2; 3–127 display the number |
| `fx_rate` (`chorus_rate`) | `rte` (crt) | Effect Rate | 0–127, `bbdRateHz[]` (0.1–10 Hz), used for dpt 3–127 |
| `fx_mix` (`chorus_mix`) | `mix` (cmx) | Effect Mix | as before |

- **Category:** the four rows are now "Effect" (`CAT_EFFECT`). The chorus's
  own rows (csr, cbw, cdv, cwd) stay "Chorus".
- **Inactive cells:** every effect and chorus cell except fx is blank only
  with fx off. rte stays live under I, II and 1+2. The csr mono-route rule
  is unchanged.

**The chorus's dpt law (`BbdChorus.h` `BBD_DPT_*`, `BbdChorus.c`
`bbd_target()`):**

| dpt | LFO rate | Centre (samples) | Sweep depth (samples) |
|---|---|---|---|
| 0 I | 0.513 Hz | 154.6 | 81.4 |
| 1 II | 0.863 Hz | 154.6 | 81.4 |
| 2 1+2 | 9.75 Hz | 154.4 | 8.8 |
| 3 → 65 (var) | rte | 154.6 | 0 → 81.4, linear |
| 65 → 127 (var → dbl) | rte | 154.6 → 662, linear | 81.4 → 110, linear |

- **Smoothness:** the path is continuous and never falls. The depth's 1/8
  slew and the centre's 16-sample step limit keep every dpt move a glide,
  automated or not, across the Juno-mode boundary too.
- **Delay range:** the lowest delay on the path is 73.2 samples, inside
  `BBD_D_MIN` / `BBD_D_MAX`.
- **The knee at 65:** var reaches full depth at the midpoint, and the
  doubler half follows. A different shape is one table edit.
- **Why dpt carries the modes:** the old cho list (off I II 1+2 var dbl) is
  gone, and the Juno modes, var and dbl are now dpt values. The old cdp
  depth (x / 127 of var's or dbl's sweep) is folded into the same path.

**Code:**
- **`BbdChorus.h`:**
  - the `BBD_MODE_*` constants are replaced by `BBD_DPT_I`, `_II`, `_I_II`,
    `_VAR` (3) and `_VAR_FULL` (65);
  - `bbdChorus_init(unit, mono, dpt)` and `bbdChorus_control(unit, dpt,
    rate, mix, tone, drive, width, quiet)`: the separate mode and depth
    arguments are gone.
- **`BbdChorus.c`:** `bbdJuno[3]` and the var / dbl constants, plus
  `bbd_target()` (a compare and at most two multiply-adds a block).
- **`InstrumentManager.h`:** `ADV_FX_OFF` / `ADV_FX_CHORUS` / `ADV_FX_COUNT`
  and the `IM_SPECIAL_FX_TYPE` contract.
- **`InstrumentManager.c`:**
  - `instrumentManager_writeFxType()` (was `writeChorusType`);
  - `instrumentManager_chorusBind(slot, fx, dpt, …)`;
  - `instrumentManager_unitsWanted()` (`!= ADV_FX_OFF`);
  - the clx and alx control calls.
- **Voices:** the fields `fxType`, `fxDepth`, `fxRate`, `fxMix` (were
  `cho*`) in `ColloidVoice.h` and `AlkalineVoice.h`. The size is unchanged,
  so no RAM moved.
- **`ColloidParameters.c` / `AlkalineParameters.c`:** the rows, the
  `*_PARAM_FX_*` / `*_IDX_FX_TYPE` names, the inactive-cell rules and the
  MIX layouts.
- **Menu:**
  - **`menu.h`:** `DTYPE_TAG_FX_DEPTH` / `DTYPE_0B127_FX_DEPTH`, a tagged
    `DTYPE_0B127`. Every dtype switch masks the low nibble or treats its
    default as 0–127, so range, clamp, edit, automation and modulation are
    a plain 0–127 row's.
  - **Short labels:** `SHORT_FX`, `SHORT_FXR`, `SHORT_FXD`, `SHORT_FXM`.
  - **`MenuLabels.c`:** "fx " "rte" "dpt" "mix".
  - **`MenuText.h`:** `fxTypeNames` {off, chr} on `ONOFF_TAG_FX_TYPE` (tag
    9), and `fxDepthNames` {I, II, 1+2}.
  - **`menu.c`:** `menu_formatDtype3()` and the single-parameter painter name
    dpt 0–2. The name tables are asserted against `ADV_FX_COUNT` and
    `BBD_DPT_VAR`.
- **Init files:** `init.clx` / `init.alx` are regenerated with fx chr,
  dpt 0 (I: the S099 P1 init's sound), rte 40 and mix 64. `init.col` /
  `init.alk` are byte-identical.
- **Tools and laws:**
  - `check_special_tags.py` uses `fx_type` → `FX_TYPE`, and still requires
    a fixed selector;
  - `modules/chorus.c`: the new signature; CH-SLEW (dpt 3 → 65 → 127); and
    the new law **CH-DPT**: the Juno modes fixed and ignoring rte; dpt 3,
    65 and 127 at var 0, var full and dbl full; 3–127 following rte; the
    path never falling and inside the delay range;
  - comments in `modules/instrument_manager.c`, `make_init_col.py` and
    `make_init_alk.py`.

### 9.2 Gates

| Gate | Result |
|---|---|
| `make all && make img` | no new warning in the changed files (the full rebuild also lists the unchanged files' standing warnings). Flash **730,296 B** (+184); ITCM unchanged (15,432). |
| `make -C tools/dsp_test all` | exit 0; every law 0 failures, CH-DPT included; 87 breaks caught; `special tags OK` (439 rows); the init checks match (59 / 64 / 31 / 38 rows). |
| `elf_s099_check` | 15 of 15 exact (the chorus loops did not change). |
| Colloid host cost (`baselines/s099p2/colloid_cpu_fx.txt`) | `clx-pwm6-par` 449.8 → **449.7** (the control's longest path); every Basic mode unchanged. |
| Alkaline host cost (`alk_cost_fx.txt`) | unchanged. |

**Not verified on the device:** the page, the labels, dpt's display and the
dpt law by ear.

## 10. Chorus simplification, and the effect screen on the click page (done)

**Request (user, 2026-11-19):**
- Remove csr from the Advanced Colloid: the routing is always stereo when the
  filters are stereo, and always mid when they are mono serial.
- On the Alkaline, remove the tone (it does little). The chorus drive follows
  the post-filter drive and the width follows the depth, so all three rows go.
- §9 put the effect screen on MIX by mistake. "Click" is page 4, the 5th
  SELECT button: the effect is the last screen of that page.

**Layouts:**

| Type | Page 4 "click" (5th SELECT) | MIX (8th SELECT) |
|---|---|---|
| clx | fat fde fsl fre \| fam drv \| **fx dpt rte mix** | vol pan ton drv (as the Basic Colloid's) |
| alx | acc \| **fx dpt rte mix** | vol pan ton drv (as the Basic Alkaline's) |

- **No empty screen before the effect,** so SELECT reaches it.
- **The Basic layouts are unchanged.** The page macros are split
  (`COL_PAGES_0_3`, `COL_PAGE_4`, `COL_PAGES_5_6`; the same for the
  Alkaline) so the Advanced tables can replace page 4 and MIX alone.

**Derived settings:**
- **Colloid source:** L-R when the route is stereo (`rte str`), mid when it
  is a mono serial route. Chosen per block in the clx sync case.
- **Alkaline tone:** fixed at `BBD_ALK_TONE` (73, 9 kHz, the old init value;
  the post pole still tracks the delay).
- **Alkaline BBD drive:** the post-filter drive byte (`instrument_drive`,
  `vcaDrive`), x1–x4 into the clip across the same knob as the overdrive.
- **Alkaline width:** `BBD_ALK_WIDTH(dpt)`. 127 (the Juno's opposed sweeps)
  for I, II and 1+2; dpt itself for 3–127. At dpt 64 the right tap is
  unswept; at 127 the sweeps are fully opposed.

**Rows:**
- **Colloid:** 64 → **63** (`chorus_source` removed).
- **Alkaline:** 38 → **35** (`chorus_tone`, `chorus_drive`,
  `chorus_width` removed).
- **Voice fields:** `choSrc`, `choTone`, `choDrive`, `choWidth` and
  `choPad` are gone.
- **Menu:** `SHORT_CSR` / `CBW` / `CDV` / `CWD` and `CAT_CHORUS` ("Chorus")
  are removed, as are `ONOFF_TAG_CHO_SOURCE` (tag 10 is now free) and
  `choSrcNames`.
- **Init files:** `init.clx` / `init.alx` are regenerated (63 / 35 rows);
  `init.col` / `init.alk` are unchanged.

**Gates:**
- Flash **729,904 B** after this section (§11 then adds 328 B, to 730,232);
- the suite exits 0; 87 breaks caught; `special tags OK` (431 rows);
- `elf_s099_check` 15 / 15;
- Colloid and Alkaline host costs unchanged.

## 11. Step editing: the screen order, VOICE-mode step edit, held steps flash (done)

**The step edit screens (user, 2026-11-19).**
- Screen 1 is **vel nte tim fil** and screen 2 is **cdn rol rtm**
  (`menuPages.h`).
- `rfl` is renamed **fil** (category "Step", long name "Fill").
- Its behaviour is unchanged: it truncates a roll and, on some instruments,
  shortens the step.
- The '>' affordance (4 cells on screen 1, 3 on screen 2) and the
  automation list after screen 1's last cell are as before.

**VOICE mode: SHIFT + step (user's device report).**
- **Before:**
  - SHIFT + step called `buttonHandler_selectActiveStep()`, whose
    `menu_showStepEditPage()` set sub-page 1 of the *current* page. In VOICE
    mode that is the voice page, so the AEG page appeared.
  - The step kept flashing after its release.
- **Now (`buttonHandler.c`, `buttonHandler_voiceStepEditPress()` /
  `Release()`):**
  - SHIFT + step opens the step edit page (SEQ_PAGE sub-page 1, as a
    STEP-mode press) on that step;
  - further steps pressed while it is open, SHIFT held or not, join it;
  - every held step flashes, and edits go to every held step
    (`menu_broadcastStepSpecial()`, the STEP-mode path);
  - a released step stops flashing, and the last release returns to the
    VOICE page that was showing, with no step flashing;
  - releases never toggle a step;
  - a mode change closes the edit (`buttonHandler_closeLatchedSurfaces()`).
- **Unchanged:** holding steps without SHIFT still opens the VOICE
  automation overlay. RAM: 2 B (`bh_voiceStepEdit`,
  `bh_voiceStepPrevPage`).

**STEP mode: every held step flashes.**
- A step pressed while others are held no longer clears their flashing
  (`led_clearAllBlinkLeds()` now runs only when no other step is held);
- `buttonHandler_selectActiveStep()` leaves a previous selected step flashing
  while it is held;
- a released step other than the selected one stops flashing;
- the selected step keeps flashing, as before;
- multi-step editing was already correct (the user's test) and is unchanged.

**Gates:** flash 730,232 B; the suite exits 0. Not tried on the device.

**Still open: legato.** The `leg` roll value, its recording rules and its
playback wait for answers to the eight questions asked in the session (the
defaults were proposed there).
