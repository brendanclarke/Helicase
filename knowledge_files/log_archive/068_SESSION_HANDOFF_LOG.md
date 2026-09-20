# Session 068 — Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Date**: 2026-09-19
**Branch**: `dev-ph4-5-fixes` (base `9989811`, the Session 067 merge into `prime`); committed at `613f466`
**Session goal**: Root-cause and fix the reported VOICE-mode Pattern trigger
assignment failure, the missing chase-light-after-boot bug, and the reported
track-settings-ignored-by-sequencer bug; assess AutoSave/Pattern-service CPU
convergence.

---

## End of Session

```
DATE: 2026-09-19
SESSION GOAL: Fix VOICE-mode step-toggle silent failures, missing boot
  chase-light, and sequencer track-settings (length/scale/shuffle) ignored
  by playback; assess AutoSave/Pattern-service bounded-CPU convergence.
COMPLETED: Front-panel event ring rebuilt (64-entry monotonic SPSC ring,
  overflow reconciliation, physical-held hold-timer check, diagnostic
  witness, hold-delay increase); menu_setPlayedPattern() chase-light mirror
  fix at all three filesystem realignment sites; per-track sequencer step
  length now read from Pattern data. All three hardware-accepted.
  AutoSave/Pattern-service bounded-CPU plan (S069_ATS_PAT_BOUNDED_CPU.md)
  fully written and settled but NOT implemented — carried to Session 069.
VERIFIED ON HARDWARE: Yes, for all three implemented fixes. Event-ring fix:
  SD_CARD_PAT_ASSIGN_BUG_OUTPUT card capture, zero U (overflow) records,
  42 K (step-toggle witness) records including every SEQ1-5 tap, zero
  Pattern-service error records. Chase-light fix: chase appears immediately
  on boot across non-zero active Scenes, VOICE/STEP/EUKLID, follow OFF/ON,
  PERF still suppresses chase. Track-length fix: independent per-track
  lengths wrap correctly, realignment correct after Scene/Bank switch.

CHANGES THIS SESSION:
- Core/Hardware/frontPanel/buttonHandler.c: event ring 16->64 entries with
  monotonic producer/consumer counters, overflow flag/counter and
  reconciliation, seq_buttons[] promoted to file scope, held-check added to
  buttonHandler_tick(), K trace witness before pat_toggleStep(), ISR-vs-
  foreground comment corrections (5 sites)
- Core/Hardware/frontPanel/buttonHandler.h: public concurrency comment
  corrections (ISR -> foreground scan)
- Core/Bank/Scene/AutosaveTrace.h: new stage codes U (EVT_OVERFLOW) and K
  (STEP_TOGGLE), plus K's value32 layout shift defines
- config.h: BUTTON_HOLD_DELAY_MS 100u -> 200u
- main.c: second buttonHandler_processEvents() call site after
  buttonHandler_tick(), separated by audio_check_and_render()
- Core/Menu/menu.c: new menu_setPlayedPattern() setter
- Core/Menu/menu.h: menu_setPlayedPattern() declaration
- Core/Hardware/SD/filesystem.c: menu_setPlayedPattern() called at 3
  Scene/Bank realignment sites (Bank Load phase-20, HCPR matching-winner
  reader, HCPR all-refreshed reader)
- Core/Sequencer/sequencer.c: seq_advanceTrackStep() and
  seq_realignActivePatternToMasterClock() read region->track_length[track]
  instead of hardcoded NUM_STEPS_PER_BAR
- Core/Bank/Scene/Pattern/PatternData.c: init default track_length changed
  from NUM_STEPS (128) to 16
- SD card SD_CARD_PAT_LENGTH/: 35 PAT4 files patched to track_length=16,
  CRC32C recalculated (297 legacy v1-v3 text files unaffected, no such field)
- SCOPING_TARGETS.md: chase-light item marked RESOLVED, new "Session 068
  deferred items" section (track scale, track shuffle)

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED:
- VOICE-mode SEQ step taps silently failing to toggle (event ring overflow
  + stale pairing-mask bits + shared-timer false promotion, compounding).
- Chase-light LED missing/intermittent after boot when active Scene != 0
  (menu_playedPattern desync from the other three Pattern/Scene authorities).
- Sequencer always playing 16 steps per track regardless of the stored,
  edited, persisted track_length value.

NEXT SESSION RECOMMENDED GOAL: Implement S069_ATS_PAT_BOUNDED_CPU.md (Pattern
  Stack Service maintenance no longer manufacturing continuous AutoSave
  dirtiness/CPU work), then retest the AutoSave OFF-to-ON convergence matrix
  from S068_AUTOSAVE_REENABLE.md now that layout churn no longer distorts
  timing. Track scale and shuffle sequencer consumption remain deferred
  behind that (see SCOPING_TARGETS.md Session 068 deferred items).
BLOCKERS: None for Session 069 to start the bounded-CPU implementation; it
  is a settled plan (all resolved decisions recorded in
  S069_ATS_PAT_BOUNDED_CPU.md before deletion — preserved in §4 below).

CRITICAL REMINDERS FOR NEXT SESSION:
- All presently uncommitted RAM is reserved (DTCM for delay-line buffers,
  SRAM1 for Pattern data). This session's 56 bytes of button-ring/overflow
  state was explicitly approved as an exception (front-panel input
  integrity), not drawn from either reserved pool. Obtain user
  acknowledgement before any new allocation, including the ~256-byte
  reservation-bitmap option discussed in the bounded-CPU plan.
- Always `make clean` after editing config.h. The Makefile has no header
  dependency tracking (no -MMD/-MP).
- buttonHandler_buttonPressed/Released and the event ring run from the
  foreground 500 Hz din_dout_exchange() scan (timebase_serviceFrontPanel),
  NOT from an ISR. This was a stale comment through Session 067; corrected
  this session. The volatile qualifiers remain correct regardless (the scan
  and the main-loop consumer still interleave around audio rendering).
- buttonHandler_processEvents() is now called TWICE per main-loop pass
  (main.c, separated by audio_check_and_render()). It must never call
  audio_check_and_render() itself or acquire any audio dependency — this
  invariant is unchanged, just now exercised from two call sites.
- The event ring's overflow reconciliation deliberately does NOT hide the
  condition: it clears pairing masks and cancels the hold timer, which can
  cause a visible unintended toggle. This is accepted as a diagnostic
  signal. Do not "fix" this into silence without revisiting the design
  rationale in §1.5 below.
- Per-track sequencer length is now live (region->track_length[track]).
  seq_handleMasterBoundary() intentionally still uses the fixed
  NUM_STEPS_PER_BAR (16) for the master bar grid (pattern-change commit,
  beat LED, clock out) — this is correct and must not be "fixed" to match
  per-track length; they are different concepts.
- Track scale and shuffle are STILL not consumed by playback (stored/edited/
  persisted only). Do not assume they work; see §3.2/§3.3.
- The AutoSave/Pattern-service bounded-CPU plan (§4) is fully specified and
  ready to implement, but zero Core/ code was changed for it this session.
```

---

## 1. Pattern trigger assignment failure (VOICE-mode step taps not toggling)

### 1.1 Reported symptom

VOICE-mode SEQ taps intermittently failed to toggle a step's trigger — no LED
change, no sound change — following multi-button gestures (holding/releasing
many steps together, or interacting with the MODE-VOICE Scene-mask overlay or
the Load/Save Scene selector). The numerical pattern in the field reports was
"SEQ6-16 work, a lower range doesn't," which turned out to be a scan-order
artifact of the root cause below, not a boundary related to Pattern pool
capacity.

### 1.2 Confirming the mutation path itself was not the problem

The entire path from a VOICE-mode SEQ release to a trigger mutation is:

```
buttonHandler_seqButtonReleased()
  -> buttonHandler_setRemoveStep()
    -> pat_toggleStep(track, step, patternNr)
      -> *entry ^= PAT_ADDR_TRIGGER_BIT     // XOR bit 15, PatternData.c
      -> pat_markSceneDirty(scene_index)
    -> led_setValue(pat_isStepActive(...), ledNr)
```

`pat_toggleStep()` operates directly on the validated 16-bit address entry.
There is no Pattern Stack Service queue involvement, no pool allocation, no
deferred processing, and no failure path once `pat_addrPtr()` returns
non-NULL (which it always does for valid track/step/Scene coordinates). If
`pat_toggleStep()` had been reached, the toggle would have succeeded — the
LED is rewritten from the same entry immediately afterward, so a silent LED
failure with a healthy mutation is not physically possible in this path.

The supplied `SD_CARD_PAT_ASSIGN_BUG/pattrace.bin` (5,545 complete 8-byte
records: 4,587 `M` Tier-1-relocation-success, 958 `R` Tier-2-relocation-
success, **zero** `H`/`Q`/`C`/`F`/`G`/`X` error records) confirmed the
Pattern Stack Service was healthy throughout the capture window and,
because ordinary trigger toggling never enters the service at all, decisively
excluded every Pattern-service failure class as the cause. This placed the
defect before `pat_toggleStep()` — in front-panel event delivery.

### 1.3 Root cause — three interacting defects in buttonHandler.c

**Defect 1 — event ring capacity and silent overflow.** The ring was declared
`EVT_RING_SIZE 16` with classic masked head/tail:

```c
static inline void evt_push(uint8_t buttonNr, uint8_t pressed) {
    uint8_t next = (uint8_t)((evt_head + 1) & (EVT_RING_SIZE - 1));
    if (next == evt_tail) return;     // silent drop, no counter/trace
    evt_ring[evt_head] = ...;
    evt_head = next;
}
```

The `next == evt_tail` full-guard reserves one slot to distinguish full from
empty, leaving only **15 usable entries**. `din_dout_exchange()`
(500 Hz DIN scan, 3-sample debouncer) settles all simultaneously-changed
buttons in one scan pass, so a 16-button release gesture alone can emit 16
events in a single call — 1 over capacity before counting any modifier,
mode, or transport edge in the same burst. `buttonHandler_processEvents()`
drained exactly **one** event per main-loop call (`if`, not `while` — an
intentional carry-over of the original AVR's one-button-per-loop cadence,
which predates the STM32 port's ability to receive 40 button states
atomically per scan). Between 500 Hz scan passes the main loop executes
roughly 15 iterations at the ~7,600 Hz effective loop rate, so a burst of
16+ events in one scan pass overflowed the ring before the main loop could
catch up.

**Defect 2 — stale press/release pairing masks.** Two 16-bit masks
(`buttonHandler_voiceSceneSeqPressedMask` for the MODE-VOICE Scene-mask
overlay, `buttonHandler_loadSceneSeqPressedMask` for the Load/Save Scene
selector) record which SEQ presses were consumed by a modal overlay; the
matching release clears the bit and is itself consumed (never reaching
`buttonHandler_seqButtonReleased()`). If a release event was silently
dropped by ring overflow, its pairing bit was never cleared. The user's
*next* ordinary tap of that same button then had its **release** wrongly
consumed as the stale overlay pairing (clearing the bit but skipping the
toggle) — a one-tap-afterward failure that self-heals on the tap after that.

**Defect 3 — shared long-press timer, no physical-held check.** A single
global timer (`buttonHandler_buttonTimer` / `buttonHandler_buttonTimerStepNr`)
governs all SEQ hold detection. `buttonHandler_tick()` fired the timer purely
by deadline comparison, without checking `btn_held[]` for the initiating
button. If a tap's release event was delayed behind a backlog past the hold
threshold, the timer fired while the button was already physically released,
called `menu_voiceAutoOverlayHoldExpired()` (VOICE mode), and set the global
`TIMER_ACTION_OCCURED` sentinel — so the eventually-delivered delayed release
was consumed as a completed-hold-gesture release instead of a tap toggle.
This defect is independent of ring overflow (any sufficiently delayed
release can trigger it) but ring overflow's added backlog made it far more
likely.

**Why VOICE and not STEP+SHIFT.** VOICE mode changes the trigger on SEQ
**release** (`buttonHandler_seqButtonReleased()`); a dropped/misconsumed
release therefore directly produces "no toggle." STEP mode with SHIFT
changes the trigger on the SEQ **press**; even if its release is later lost,
the toggle already happened. STEP mode is not fully immune (a lost *press*
could still be lost), but release bursts (all fingers lifted together) are
concentrated in a way press bursts typically are not, making VOICE mode
dramatically easier to provoke in practice.

**Why the scan order produced "SEQ6-16 work, lower doesn't."** `din_dout_
exchange()` scans physical shift-register indices upward; the SEQ groups
occur in this order: indices 8-11 = SEQ13-16 (scanned first), 16-19 =
SEQ9-12, 24-27 = SEQ5-8, 32-35 = SEQ1-4 (scanned **last**). When a burst
fills the ring, the earlier-scanned higher-numbered SEQ buttons' events are
pushed first and survive; the later-scanned lower-numbered buttons' events
are the ones dropped. This is a ring-layout artifact, not a Pattern pool
capacity boundary.

### 1.4 Fix — 5 patches, `Core/Hardware/frontPanel/buttonHandler.c` (+ 4 other files)

**Patch 1 — ring capacity and overflow detection.**
`EVT_RING_SIZE` 16 -> **64** (power of two, exceeds the 41-button hardware
maximum, so it cannot physically overflow under any legitimate input). Ring
storage changed from masked `evt_head`/`evt_tail` to monotonic `uint8_t
evt_producer`/`evt_consumer` counters (matching the `PatternStackService`
ring pattern), with full detection `(uint8_t)(evt_producer - evt_consumer)
>= EVT_RING_SIZE` — because 64 divides the `uint8_t` range (256) evenly, the
unsigned modular subtraction wraps correctly across the 0xFF->0x00 boundary,
and **all 64 slots are usable** (no reserved-empty-slot loss). New state:
unconditional `evt_overflow_flag` (1 byte, set by `evt_push()` on drop) and
`DEV_MODE_LOGGING`-only saturating `evt_drop_count` (1 byte). On overflow,
the next `buttonHandler_processEvents()` call clears both pairing masks,
resets `buttonHandler_buttonTimerStepNr` to `NO_STEP_SELECTED`, and (logging
builds only) emits a `U` (`AUTOSAVE_TRACE_STAGE_EVT_OVERFLOW`) trace record
with the drop count and ring depth. `buttonHandler_processEvents()` still
drains exactly one *event* per call — the button handler must never call
`audio_check_and_render()` or acquire any audio dependency — but a **second**
call site was added in `main.c` after `buttonHandler_tick()` (separated by
its own `audio_check_and_render()`), giving ~2 events drained per main-loop
pass, or roughly 30 events per 500 Hz scan interval, well over the 41-button
hardware maximum, without any structural change to the drain function itself.

**Patch 2 — hold-timer physical-held check.** The `seq_buttons[16]` physical-
button lookup table (previously local to `buttonHandler_seqHeldMask()`) was
promoted to file scope so `buttonHandler_tick()` can reverse-map the timer's
stored absolute step (`buttonHandler_buttonTimerStepNr`, 0..127) back to a
physical `BUT_SEQ*` button via `seq_buttons[(uint8_t)stepNr %
NUM_STEPS_PER_BAR]` (the modulo recovers the 0..15 SEQ index from the
absolute step, matching how `buttonHandler_setTimeraction()` originally
stored it via `buttonHandler_visibleStep()` = `menu_currentBar * 16 +
seqButtonPressed`). Before promoting the timer, `buttonHandler_tick()` now
checks `btn_held[physBtn]`: if released, the timer is cancelled by resetting
to `NO_STEP_SELECTED` (**not** `TIMER_ACTION_OCCURED`) so the eventually-
delivered release processes as a normal tap toggle; if still held, the timer
fires exactly as before. A single shared timer remains sufficient — holding
multiple SEQ buttons together is always treated as one hold gesture, driven
by the raw held mask, not per-button timers; this is an intentional scope
decision, not a limitation left for later.

**Patch 3 — diagnostic witness.** A `DEV_MODE_LOGGING`-gated `K`
(`AUTOSAVE_TRACE_STAGE_STEP_TOGGLE`) trace record was added in
`buttonHandler_setRemoveStep()` immediately before the `pat_toggleStep()`
call, packing `trackNr` (bits 0-7), absolute `seqButtonPressed` (bits 8-15),
`patternNr` (bits 16-23), and the pre-toggle `pat_isStepActive()` result
(bit 24) into `value32`. This lets a future report distinguish "input
delivery never reached the mutation" (no `K` record) from "Pattern mutation
itself failed" (`K` present, trigger bit unchanged) without relying on LED
appearance. It touches only the static fixed-size address array via
`pat_isStepActive()` — no pool/allocation/service interaction — and fires
only on actual SEQ taps, bounded by human button-press rate.

**Patch 4 — hold delay.** `BUTTON_HOLD_DELAY_MS` 100u -> 200u (`config.h`,
requires `make clean` — no `-MMD`/`-MP` header dependency tracking in this
Makefile). Reduces false hold-promotion under short event-processing stalls;
applied after Patch 2's held-check as the structural correction, not as a
substitute for it — a release that is genuinely dropped would still never
toggle the step at either threshold.

**Patch 5 — stale comment correction.** `buttonHandler.c`'s header comment
and several internal banners described `buttonHandler_buttonPressed`/
`buttonReleased` as running from "the TIM6 ISR." They actually run from the
foreground 500 Hz `din_dout_exchange()` scan via
`timebase_serviceFrontPanel()`, not from any ISR — this had been stale since
at least the STM32 port's scan architecture was established, predating this
session. Corrected in `buttonHandler.c` (file header, held-state array
banner, scan-safe banner, `buttonHandler_voiceSceneMaskHoldActive` docblock)
and `buttonHandler.h` (three public boundary comments: lines ~5-8, ~61-64,
~80-83). The `volatile` qualifiers on `btn_held[]` and the ring remain
correct regardless — the foreground scan and the foreground consumer still
interleave around audio rendering within the same context.

### 1.5 Design rationale for the overflow-reconciliation policy

The implementation plan explicitly decided **not** to hide an overflow
event: clearing the pairing masks and cancelling the hold timer on overflow
can itself cause a visible unintended toggle (if a pairing mask is cleared
mid-overlay, the eventual "matching" release is no longer suppressed and
falls through to an ordinary toggle). This was accepted as the correct
tradeoff — the purpose of the reconciliation is to prevent *permanently
stuck* state (an orphaned pairing bit that silently eats the next N taps),
not to make overflow invisible. With the 64-entry ring this condition should
never occur in normal operation (41-button hardware maximum), so the
reconciliation path is a safety net, not a steady-state behavior.

### 1.6 Resolved decisions (recorded before the planning documents are deleted)

| # | Question | Decision |
|---|----------|----------|
| 1 | RAM allocation | 66 bytes approved ceiling (64-byte ring + 2-byte logging-only overflow state), SRAM1 `.bss`. Measured actual: 56 bytes (48 ring expansion + 1 unconditional flag + 1 logging-only counter, plus ~6 bytes of alignment/layout movement). |
| 2 | Drain count | 1 event per call, 2 call sites in the main loop. Button handler never touches audio. |
| 3 | Overflow reconciliation | Minimal: log + clear masks + reset timer. No screen message, no user-facing recovery. Detect and prevent stuck state, not hide the condition. |
| 4 | Timer model | Single shared timer, with a `btn_held[]` check on expiry. Multi-SEQ hold is one hold gesture, not per-button tracked. |
| 5 | Diagnostic witness | Approved: `DEV_MODE_LOGGING`-gated record before `pat_toggleStep()`, touching only the static address array. |
| 6 | Audio interleaving | None inside the button handler; the second main-loop call site provides the extra throughput instead. |
| 7 | Overflow edge cases | Do not mitigate further; an overflow-induced unintended toggle is an acceptable diagnostic signal. |
| 8 | Ring size | 64 entries — architecturally cannot overflow (41-button hardware maximum). |

### 1.7 Build and hardware validation

Clean build (`make clean && make`), zero warnings, zero errors. Final image
after all three of this session's fixes (button ring + chase-light + track
length combined): `text=447,860`, `data=412`, `bss=291,196` (see §5).

Hardware capture `SD_CARD_PAT_ASSIGN_BUG_OUTPUT` (post all 5 patches):

- `asavetrc.bin`: 151,218 records. **Zero `U` (event-ring-overflow) records**
  — the 64-entry ring was never full during any exercised gesture sequence.
  Zero `E` (operation error), zero `F` (trace suppressed), zero `X` (phase
  stall). All 27 `O` (Save lifecycle) records completed without `FAILED`
  flags; the final autosave cycle is a complete `S -> A -> V -> M -> C -> P
  -> T` sequence. 25 `G` (trace-ring-dropped-count) records with cumulative
  count 45,949 are present — these are the AutoSave *trace ring's own*
  bounded-capacity overwrites (a pre-existing, documented condition, see
  `S069_ATS_PAT_BOUNDED_CPU.md` and §4 below), unrelated to the button event
  ring and with no effect on data integrity.
- **42 `K` (step-toggle witness) records**, all targeting Scene 8/bar 0,
  every one internally consistent (trigger-before states alternate correctly
  across repeated presses on the same step). SEQ buttons 1 through 15 were
  all exercised and all toggled successfully — **critically, SEQ1 through
  SEQ5, the range previously dropped first on overflow, all worked.** (SEQ16
  was architecturally identical but not pressed during this test session.)
- `pattrace.bin`: 6,283 records, `M`=5,248 / `R`=1,035, **zero error stages**
  — Pattern service healthy throughout. (The maintenance-oscillation pattern
  visible here — 1,015 distinct (stage, scene, track, step) tuples, top entry
  relocated 179 times — is the subject of §4/`S069_ATS_PAT_BOUNDED_CPU.md`,
  not a defect in this fix.)
- Pattern PAT4 A/B file generations examined and consistent with normal
  double-buffered autosave alternation (e.g. pat08/Scene 8, the active test
  Scene, has both generations at 10,656 bytes each, differing by one
  generation-counter increment).
- Boot reader (`Q`): 33 summary records, all `case2_mask=0x0000`,
  `case3_mask=0x0000` — no reload mismatches or invalidated Scenes across
  all 33 captured boot cycles.

**Verdict: fixed.** All three root-cause defects addressed by the five
patches; Pattern mutation path confirmed uninvolved and correct throughout.

---

## 2. Missing chase-light after boot

### 2.1 Root cause

Four state authorities together determine whether the sequencer chase LED
is shown:

| State | After restoring active Scene N |
|---|---:|
| `scene_active_index` | N |
| `seq_activePattern` | N |
| `menu_shownPattern` | N |
| `menu_playedPattern` | **0 (BSS default, never updated)** |

After a Bank restore, `filesystem.c` calls
`seq_alignActivePatternToScene(op_bank_active_scene)` and
`menu_setShownPattern(op_bank_active_scene)`. The sequencer helper
deliberately does **not** call `led_notifyPatternChanged()`, because that
notifier also performs runtime presentation/performance side effects (follow-
mode LED/menu repaint, PERF Scene LED refresh, program-change companion
logic) that are inappropriate before audio has started. Nothing else updates
`menu_playedPattern` — it is zero-initialized in BSS and assigned **only** by
`led_notifyPatternChanged()`.

The chase renderer (`led_updateCurrentStep()`) reads `menu_playedPattern`,
not `seq_activePattern`:

```
shownPattern = menu_getViewedPattern();   // == menu_shownPattern
playedPattern = menu_playedPattern;
show chase only if shownPattern == playedPattern
```

So a Bank that boots into an active Scene N != 0 has `menu_shownPattern ==
N` and `menu_playedPattern == 0`; every chase dirty event is deliberately
rejected by `led_updateCurrentStep()`'s equality predicate even though the
producer side (`seq_realignActivePatternToMasterClock()` writing
`seq_ledState.chaseStep` and setting `SEQ_LED_DIRTY_CHASE`, drained by
`led_processSeqLedState()`) is completely healthy. A Bank that happens to
boot into Scene 0 works by coincidence, because the stale BSS default (0)
equals the real active Scene — explaining the reported intermittency.
Switching Scenes from PERF mode calls `seq_selectActivePattern()` ->
`led_notifyPatternChanged(seq_activePattern)`, which finally assigns
`menu_playedPattern`, self-repairing the symptom exactly as reported.

### 2.2 Fix

New side-effect-free setter, called at the same three sites that already
pair `seq_alignActivePatternToScene()` with `menu_setShownPattern()`:

**`Core/Menu/menu.c`** (new function, placed immediately after
`menu_setShownPattern()`):

```c
void menu_setPlayedPattern(uint8_t patternNr)
{
    menu_playedPattern = pat_patternValid(patternNr) ? patternNr : 0u;
}
```

**`Core/Menu/menu.h`**: matching declaration, placed immediately after
`menu_setShownPattern()`'s declaration.

**`Core/Hardware/SD/filesystem.c`**, one added line at each of:

- **Site A** — Bank Load phase-20 commit (~line 14049):
  `menu_setPlayedPattern(op_bank_active_scene);` immediately after
  `menu_setShownPattern(op_bank_active_scene);`.
- **Site B** — HCPR matching-winner reader (~line 26889):
  `menu_setPlayedPattern(active_scene);` immediately after
  `menu_setShownPattern(active_scene);`.
- **Site C** — HCPR all-refreshed reader (~line 28049):
  `menu_setPlayedPattern(bank_activeSceneSlot());` immediately after
  `menu_setShownPattern(bank_activeSceneSlot());`.

The invariant this restores, stated explicitly for future filesystem
realignment sites: **at every committed playback realignment,
`menu_playedPattern` must equal `seq_activePattern` before a chase dirty
event can be drained.** `led_notifyPatternChanged()` remains the sole
authoritative *runtime* writer (PERF Scene switches still go through it,
with its full follow/LED/repaint side effects); the new setter covers only
the pre-audio/filesystem-driven realignment path that
`led_notifyPatternChanged()` was deliberately excluded from.

### 2.3 Why this is low risk

`menu_setPlayedPattern()` writes one validated byte with no LED, LCD, MIDI,
note-off, follow-mode, or repaint side effect — contrast
`led_notifyPatternChanged()`, which does all of those and is exactly why the
boot alignment path avoided calling it. `menu_playedPattern` is read only in
`led_updateCurrentStep()` (foreground); all three new call sites are
foreground (boot is single-threaded, runtime Bank Load runs in
`filesystem_tick()`) — no ISR interaction. No new RAM, no new state. When
the active Scene is 0, the setter writes 0 to an already-0 value — a
no-op, confirming the fix is a pure superset of the previously-working case.

### 2.4 Hardware validation

Tested on hardware: chase-light appears immediately on boot across non-zero
active Scenes, on VOICE, STEP, and EUKLID pages, with follow both OFF and
ON. PERF still suppresses chase (it owns the SEQ row). Runtime Scene/Bank
Load transitions preserve the chase. No MIDI program change or
all-notes-off observed at boot alignment. **PASS — fix accepted.**

Post-implementation source review confirmed: exactly two direct writers of
`menu_playedPattern` remain in the whole codebase — `ledHandler.c:1218`
(the runtime `led_notifyPatternChanged()` path) and `menu.c:11916` (the new
setter) — and exactly three callers of the new setter, matching the three
filesystem sites above. Image delta: text +48 bytes (three call sites plus
one small function), bss unchanged.

---

## 3. Track settings ignored by the sequencer

### 3.1 Summary

`track_length`, `track_scale`, and `track_shuffle` are three per-track
fields in `pat_scene_region_t` (`PatternData.h`) that are correctly stored,
editable through Menu, dirty-marked, and round-tripped through PAT4 —
but the sequencer playback engine read none of the three. All tracks always
played 16 steps at fixed 1/16th-note resolution with zero shuffle,
regardless of the stored values.

| Path | Working? |
|------|----------|
| Init default | Yes |
| Menu display (`pat_applyTrackSettingsToMenu()`) | Yes |
| Menu edit (`pat_setTrackLength/Scale/Shuffle()`) | Yes |
| AutoSave dirty mark | Yes |
| PAT4 file write/read | Yes |
| **Sequencer playback consumption** | **No, for all three (fixed this session for length only)** |

### 3.2 Root cause 1 — track length hardcoded to 16 (fixed this session)

`seq_advanceTrackStep()` (`Core/Sequencer/sequencer.c`) used the
compile-time constant `NUM_STEPS_PER_BAR` (16) as every track's step-wrap
boundary, never reading `region->track_length[track]`:

```c
static void seq_advanceTrackStep(uint8_t track)
{
    seq_stepIndex[track]++;
    if (seq_stepIndex[track] >= (int16_t)NUM_STEPS_PER_BAR)  /* == 16 */
        seq_stepIndex[track] = 0;
    ...
```

The same hardcoding appeared in `seq_realignActivePatternToMasterClock()`
(`seq_stepIndex[track] = seq_masterStepClock % NUM_STEPS_PER_BAR;` — used
by boot alignment and runtime Scene/Bank realignment). A third site,
`seq_handleMasterBoundary()`, also uses `NUM_STEPS_PER_BAR` but was
correctly identified as **not** part of this bug: it detects the *master
grid* bar boundary (pattern-change commit, beat LED, clock output), a
bar-level concept independent of any individual track's loop length, and was
left unchanged.

**Fix**: both `seq_advanceTrackStep()` and
`seq_realignActivePatternToMasterClock()` now read
`region->track_length[track]` from the active Scene's `pat_scene_region_t`
(via `pat_sceneRegion(seq_activePattern)`), falling back to
`NUM_STEPS_PER_BAR` (16) when the region pointer is null or the stored value
is 0 (guards a zeroed/corrupt field):

```c
uint8_t len = (region && region->track_length[track] > 0u)
              ? region->track_length[track]
              : NUM_STEPS_PER_BAR;
```

The in-memory init default in `PatternData.c` was also changed from
`NUM_STEPS` (128) to **16**, to match the sequencer's historic playback
behavior and the pre-existing card content (the previous 128 default meant
every fresh/imported Pattern would have silently played all 128 steps per
track once this fix landed, a behavior change nobody asked for). 35 PAT4
files on the test card (`SD_CARD_PAT_LENGTH/`) were patched to
`track_length=16` with CRC32C recalculated; 297 legacy v1-v3 text-format
files were left untouched (no stored `track_length` field exists in that
format — they receive the new default of 16 on import, same result).

**Valid range**: the current `pat_setTrackLength()` menu setter accepts any
`uint8_t`. The sequencer fix treats 0 as "use default 16." Musically valid
lengths are 1..128 (`NUM_STEPS`), but this session's fix targets 1..16 (one
bar) — lengths 17..128 (multi-bar) would additionally need
`menu_currentBar` integration in the chase-LED renderer
(`ledHandler.c`'s bar-within-pattern clamp), which is explicitly deferred as
a separate follow-on, not part of this fix.

**Risk review (from the assessment, still valid)**:
`seq_advanceTrackStep()` runs inside `TIM3_IRQHandler` (priority 2);
`pat_sceneRegion()` returns a pointer into resident SRAM1 with no SD I/O or
allocation — the added pointer dereference is a single load, safe at ISR
priority. The region pointer is stable between pattern-change commits (a
Scene switch atomically replaces `seq_activePattern` at the master boundary
in `seq_handleMasterBoundary()`, after which the next
`seq_advanceTrackStep()` reads the new region) — no torn-read risk.

**Chase LED**: `led_updateCurrentStep()` already reads
`seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()]`, i.e. the
currently active UI voice's own step index — this was already correct for
independent per-track lengths and needed no change for the 1..16 case.

### 3.3 Root cause 2 — step scale has no per-track prescaler (deferred)

`seq_processSchedulerTick()` advances **every** track together on one
global divisor, `SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` (96/4 = 24 PPQ ticks,
i.e. always 1/16th note):

```c
if ((seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) == 0u) {
    for (track = 0u; track < NUM_TRACKS; track++)
        seq_advanceTrackStep(track);
```

There is no per-track tick accumulator, counter, or scale lookup anywhere.
`track_scale[track]` (init default `TRACK_SCALE_OFF` = 10) is stored and
edited but never queried by the sequencer.

**Potential fix (not implemented)**: add per-track PPQ tick accumulators
(`seq_trackTickAccum[NUM_TRACKS]`, +`NUM_TRACKS * 4` = 28 bytes of
ISR-local static state). On each PPQ tick, increment each track's
accumulator and advance only when it reaches the threshold derived from that
track's scale value — e.g. 1/32=12 ticks/step, 1/16=24 (default), 1/8=48,
1/4=96. **The exact scale-to-ticks mapping must be defined against the
original LXR's documented scale-label table before implementation** — this
was not resolved in the assessment and is not resolved here. Orthogonal to
track length: a track with length=8 and scale=1/8 plays 8 steps at half the
rate of the default grid.

### 3.4 Root cause 3 — shuffle entirely absent from playback (deferred)

The sequencer fires every step at a uniform tick boundary; there is no
shuffle offset calculation anywhere in `sequencer.c` (an existing comment at
line 221 states: "The transport tick stays uniform: fixed-grid patterns have
no shuffle"). `track_shuffle[track]` (init default 0) is stored and edited
but never consumed.

**Potential fix (not implemented)**: delay even-numbered steps (or odd,
convention TBD) by a fraction of the step interval — at 1/16th notes/96 PPQ
the maximum offset is ~23 ticks (one step minus one tick). Two implementation
options were identified: (1) a per-track "delay ticks remaining" counter,
loaded from the shuffle value on an even step and triggering when it expires
on a later PPQ tick (simpler, lower RAM, +7 bytes ISR static per track); or
(2) a deferred trigger queue, queuing the trigger with a future tick offset
and draining it in `seq_processSchedulerTick()`. Option 1 was assessed as
preferred but neither was built. Orthogonal to both length and scale — all
three can compose independently once implemented.

### 3.5 Hardware validation (length only)

Tested on hardware: per-track independent lengths play correctly — tracks
wrap at their configured `track_length` value, not the hardcoded 16; tracks
with length < 16 loop independently of other tracks; the default init value
(now 16, matching historic behavior) produces identical playback to the
pre-fix firmware; realignment after Scene/Bank switch places each track's
cursor at the correct position within its own length via
`seq_masterStepClock % len`; chase LED follows the active voice's step
index within its configured length. **PASS — track length fix accepted.**
Scale and shuffle remain untested because unimplemented; both are recorded
in `SCOPING_TARGETS.md` § Session 068 deferred items.

---

## 4. AutoSave / Pattern-service bounded-CPU — planning only, not implemented

`S069_ATS_PAT_BOUNDED_CPU.md` is a completed, internally-resolved planning
document (explicit final line: "No `Core/` product code is changed by this
planning revision"). It is preserved as a live working document in the
repository root (not deleted with the other four S068 documents this
closeout consumes) because it is the starting point for Session 069. This
section preserves its substance in case it is later deleted too.

### 4.1 What it diagnoses

Three concrete causes of continuous, unforced CPU/AutoSave-write churn from
the Pattern Stack Service, visible in the pattrace.bin maintenance
oscillation noted in §1.7 and §3.5's hardware fixtures (Scene 2/track 0/
step 1 relocated 179 times in one capture):

1. `patSvc_relocateIndex(..., gap, ...)` searches for a run large enough for
   the logical block *plus* a trailing gap, but marks only the logical block
   occupied in the bitmap — the "gap" is ordinary free space indistinguishable
   from any other free chunk to `pat_poolAlloc()` or a later relocation.
2. Proactive Tier 2 deliberately packs blocks downward with **no** gap,
   removing the exact condition Tier 1 just created, then resets the Tier 1
   cursor — guaranteeing another sweep. Tier 1/Tier 2 chase each other
   indefinitely even with no user edits.
3. Both Tier 1 and ordinary allocation also search from the low end, so
   Tier 1 can consume a gap it reserved earlier for a different block.
   Disabling Tier 2 alone would reduce churn but would not make the gap
   policy a stable invariant.

Every successful relocation calls `pat_markPoolMutationDirty()` — turning a
purely physical layout optimization (identical bytes at a new pool offset)
into a semantic Pattern-AutoSave-triggering mutation. The observed 4,587
Tier-1 + 958 Tier-2 moves in the original `pattrace.bin` were therefore both
CPU work *and* filesystem write work, entirely self-generated.

Two additional CPU sinks, independent of the relocation loop: an O(n)
scalar-dirty-bit full scan (`autosave_maskHasDirty()` currently examines all
3,856 volatile mask bytes on ~7,600 idle filesystem calls/second — ~29.3M
byte inspections/second) and an O(n) Pattern logical-occupancy bitmap
popcount (`patSvc_countUsed()` scans 2,048 bits — ~1.024M bitmap tests/second
at the 500 Hz service rate; the backed pool is 2,048 four-byte chunks, not
4,096 — the bitmap's upper half describes the permanently-occupied unbacked
address range).

### 4.2 Required invariants (for whoever implements this next)

1. A semantic edit dirties Pattern AutoSave exactly once; moving identical
   bytes to another pool offset does not.
2. Accepted user/live-record writes outrank every proactive maintenance
   action.
3. With no semantic edits and no blocked allocation, relocation reaches
   sleep and stays asleep — a timer alone must never wake it.
4. A step with available trailing slack can grow without a pool-wide search.
5. A true fragmented-allocation failure may invoke reactive compaction; an
   arbitrary periodic interval must not.
6. No background CPU policy may change the complete bytes, CRC, atomicity,
   or recovery behavior of either AutoSave format.

### 4.3 Implementation plan (6 items, ordered; none implemented this session)

1. **Make physical relocation non-semantic**: remove the
   `pat_markPoolMutationDirty()` call from the relocation transaction itself
   (retire the helper if it gains no other caller); Tier 1/reactive
   compaction keep emitting their PatternTrace witnesses. Safe because
   address/pool/bitmap placement is not musical state — a later explicit
   save or semantic AutoSave serializes whichever valid layout exists at its
   snapshot boundary, and a power loss reloads the prior valid PAT4 layout
   with identical triggers/specials/automation/settings (only the placement
   optimization is lost). Assessed as the smallest, highest-value change —
   breaks the maintenance-to-AutoSave feedback loop immediately, even before
   maintenance itself improves.
2. **Replace the Tier1/Tier2 loop with owned slack + reactive-only
   compaction**: preferred design needs a new 256-byte SRAM1 transient
   reservation bitmap (one bit per pool chunk, separate from the persisted
   allocation bitmap, not serialized — losing slack across reboot loses no
   music) to give a hard one-chunk trailing-slack guarantee per address
   entry; **requires explicit RAM approval before implementation** (not
   yet sought). Tier 1 becomes a finite "slack repair" pass (reserve/relocate-
   once/no-op per address entry, sleeping after one no-progress pass, woken
   only by a semantic mutation/reservation event/handover — never by
   elapsed time); proactive Tier 2 is deleted entirely, surviving only as
   reactive recovery for a queue head that failed allocation despite
   sufficient total reclaimable space. If the reservation bitmap is not
   approved, a documented no-new-RAM fallback exists (directional
   high-to-low Tier 1 scan, "soft slack" only, not a hard guarantee — must
   not be described as one). Also specifies that a direct-when-idle mutation
   that fails allocation must enter the same reactive-recovery path as a
   queued mutation, rather than failing immediately, so a future live-record
   write is never lost merely because of which admission path it arrived on.
   Flags a live-record throughput constraint for later: the queue currently
   drains at most one event per 500 Hz service tick (~500/s), while a future
   recorder emitting note+velocity+multiple automation writes across seven
   tracks at 16th-note tempo could exceed that — recommends an in-place
   value-byte fast path for updating an already-present automation value
   (no allocation/maintenance needed) and/or coalescing per-track/step
   writes into one commit, explicitly **not** simply enlarging the queue.
3. **Remove clean-state full scans without changing file work**: add an
   exact `uint16_t` dirty-bit *count* beside `autosave_dirty_mask[]`
   (incremented/decremented incrementally by the existing OR/take helpers;
   `autosave_maskHasDirty()` becomes `count != 0`) — new 2-byte SRAM
   allocation, needs approval. Keep the existing `logical_chunks_used`
   variable but maintain it incrementally from each mutation's old/new block
   size delta instead of a full bitmap recount; reserve the full 256-byte
   popcount reconciliation for init/handover/filesystem-replacement only.
   Explicitly does **not** touch or reduce any CRC, transform, or full-file
   serialization work — every `.hcprms` write still CRCs all 34,768 bytes,
   every Pattern generation still snapshots the complete 10,519-byte region
   and CRCs the complete 10,656-byte PAT4 file.
4. **One elapsed-time CPU budget for background-only work**: new
   `config.h` constant `BACKGROUND_CPU_BUDGET_US_PER_MS` (starting test
   value 50, i.e. 5% long-run allowance; 0 = disabled/unlimited; hardware
   A/B test at least 50/100/unlimited before choosing a default), charged
   via `timebase_tim2Now()`, refilled per elapsed millisecond, credit capped
   at one millisecond's allowance (prevents idle-seconds catch-up bursts),
   overshoot carried as debt. Applies only *after* items 1-3, so it
   throttles genuinely finite work rather than masking an infinite loop.
   Budgeted: OFF-to-ON full-Bank dirty reseeding (via a new retained scope
   cursor, replacing the current synchronous full walk), scalar AutoSave
   mask classification/CRC slices, Pattern AutoSave staging/CRC at new
   512-byte write-chunk boundaries, proactive Tier 1 slack repair. Explicitly
   **not** budgeted: audio/ISR work, AsyncFATFS/SD polling needed to advance
   an already-admitted transaction, foreground Load/Save, an accepted
   Pattern mutation or bulk user command, reactive compaction needed to
   *complete* an already-accepted mutation, or any completion/close/sync/
   error/rollback boundary. One new aggregate-only `Z` trace record per
   active work class per one-second window (not per slice) is specified,
   with an exact bit layout (class, denied-slice count, charged
   microseconds, max single-slice microseconds) — needs a
   `tools/decode_devlogs.py` update and RAM approval for its retained state.
5. **Pattern AutoSave quiet window + maximum latency**: two more `config.h`
   starting values, `PATTERN_AUTOSAVE_QUIET_MS` (250) and
   `PATTERN_AUTOSAVE_MAX_LATENCY_MS` (5000). A semantic mutation restarts the
   short quiet deadline (don't snapshot a file immediate edits will make
   stale); the oldest pending dirty transition starts the max deadline
   (guarantee eventual durability under continuous editing). Re-enable's
   full-Bank Pattern seed should be one rotating-cursor backlog episode
   (prefer the active Scene once, then resume rotation), not sixteen
   independent 5-second waits. Explicitly notes live-record's future safe-
   snapshot-boundary requirement is a separate, not-yet-designed concern
   that a max-latency deadline must never override.
6. **Measure, then maybe chunk, the Pattern snapshot**: only after items 1-5,
   measure `pat_snapshotScene()`'s actual duration/impact on main-loop
   interval/audio queue pressure/underruns; leave the one-call 10,519-byte
   `memcpy` intact unless it proves to be a meaningful peak. If chunking
   becomes necessary, the later writer must still serialize/CRC the complete
   snapshot as one unit — direct streaming from mutable live storage remains
   prohibited.

### 4.4 Also assessed, no fix needed — `S068_AUTOSAVE_REENABLE.md`

A companion assessment (not a fix; also not part of this closeout's five
source documents, kept as an open reference) examined the suspected
"AutoSave OFF-to-ON never rearms" defect and did **not** find it in current
source: `filesystem_setAutosaveEnabled(1)` correctly queues
`FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES`, and `filesystem_autosaveSetupCompleted()`
correctly enables tracking and calls `autosave_markResidentBankDirty()` to
reseed both scalar and Pattern dirty work. The apparent non-convergence is
explained by exactly the churn diagnosed above (§4.1): re-enable marks the
entire resident Bank, then Pattern maintenance's self-generated relocation
dirtiness can keep Pattern AutoSave perpetually busy, concealing the one
convergence write the user is actually waiting for. One genuine liveness
weakness remains, independent of CPU/churn: a single failed runtime "ensure
AutoSave files" transaction latches `fs_autosave_setup_failed` with no
automatic retry and no UI indication — recommended to be treated as a
separate media-error/retry-policy item once §4's convergence fixes land, so
the re-enable matrix can be retested cleanly first.

### 4.5 Ordering relative to Gate 0 of `S069_GENERAL_FITNESS_AGENDA.md`

A separate pre-feature planning document orders four prerequisite fixes as
"Gate 0" before further CPU/input testing can be trusted: (1) the front-panel
event/hold-ownership repair — §1 above, done; (2) the played-Pattern mirror
alignment — §2 above, done; (3) this bounded-CPU plan — done as a plan,
**not implemented**; (4) the AutoSave re-enable matrix from
`S068_AUTOSAVE_REENABLE.md` — assessed, not run as a focused hardware
matrix. This session closed Gate 0 items 1 and 2 of 4.

---

## 5. Build metrics

### 5.1 Baseline (Session 067 final)

```
text=447,580  data=412  bss=291,140
```

### 5.2 After Patch set 1 (front-panel event ring + hold timer + witness + delay)

```
text=447,724  data=412  bss=291,196
```

Delta from S067: +144 text, +56 bss (measured; ~50 bytes scheduled — the
+6 byte difference is alignment/layout, consistent with the same kind of
small over-schedule seen in Session 067's own RAM note).

### 5.3 After chase-light fix

```
text=447,772  data=412  bss=291,196
```

Delta from 5.2: +48 text (three call sites plus one small function), 0 bss.

### 5.4 After track-length fix (final, this session)

```
text=447,860  data=412  bss=291,196
```

Delta from 5.3: +88 text, 0 bss (logic-only change, no new static storage).

**Total Session 068 delta from S067 baseline: +280 text, +56 bss.** All 56
bytes of new `.bss` are in `buttonHandler.c` (event ring 16->64 entries plus
overflow-detection state); no Pattern-reserved SRAM1 or DTCM delay-line
capacity was drawn on. `make clean && make && make img` all passed; `git
diff --check` passed.

---

## 6. Files changed

| File | Summary |
|------|---------|
| `Core/Hardware/frontPanel/buttonHandler.c` | Event ring 16->64 monotonic-counter entries, overflow flag/counter + reconciliation, `seq_buttons[]` promoted to file scope, `btn_held[]` check in `buttonHandler_tick()`, `K` witness before `pat_toggleStep()`, 5 ISR->foreground comment corrections |
| `Core/Hardware/frontPanel/buttonHandler.h` | 3 public concurrency-boundary comment corrections (ISR -> foreground scan) |
| `Core/Bank/Scene/AutosaveTrace.h` | New stage codes `U` (`EVT_OVERFLOW`) and `K` (`STEP_TOGGLE`), plus K's 4 value32 shift defines |
| `config.h` | `BUTTON_HOLD_DELAY_MS` 100u -> 200u |
| `main.c` | Second `buttonHandler_processEvents()` call site after `buttonHandler_tick()` |
| `Core/Menu/menu.c` | New `menu_setPlayedPattern()` |
| `Core/Menu/menu.h` | `menu_setPlayedPattern()` declaration |
| `Core/Hardware/SD/filesystem.c` | `menu_setPlayedPattern()` call added at 3 Scene/Bank realignment sites |
| `Core/Sequencer/sequencer.c` | `seq_advanceTrackStep()` and `seq_realignActivePatternToMasterClock()` read `region->track_length[track]` instead of `NUM_STEPS_PER_BAR` |
| `Core/Bank/Scene/Pattern/PatternData.c` | Init default `track_length` 128 -> 16 |
| `SCOPING_TARGETS.md` | Chase-light item marked RESOLVED (2 places); new "Session 068 deferred items" section (track scale, track shuffle); "AutoSave CPU usage" context updated |
| `SD_CARD_PAT_LENGTH/` (test fixture, not firmware source) | 35 PAT4 files patched to `track_length=16`, CRC32C recalculated |

No `S069_ATS_PAT_BOUNDED_CPU.md` code changes — planning document only, see §4.

---

## 7. Specification-reference updates made this session

As part of this closeout: `PATTERN_DYNAMIC_STACK.md` (§6 sequencer
consumption of `track_length`/`track_scale`/`track_shuffle`, diagnostic
witness note, session-through header), `DEV_MODES.md` (new `U`/`K`
AutoSaveTrace stage codes), `SRAM_MANIFEST.md` (Session 068 +56-byte
allocation note), and `MODULE_INTERCHANGE_SPEC.md` (buttonHandler
ISR->foreground correction, two-call-site note, `menu_setPlayedPattern()`
entry, session-through header) were updated to reflect the current, real
state of the code as verified against source in this session (not merely
restated from the planning documents).

---

## 8. Source document disposition

The following root-level S068 planning/implementation documents are
**disposable** once this handoff log and the specification-reference
updates above are confirmed in place — all of their durable technical
content is preserved in this log:

- `S068_PAT_ASSIGN_BUG.md` — initial front-panel event-ring investigation,
  preserved in this log §1.1-§1.3, §1.7.
- `S068_PAT_ASSIGN_BUG_IN_DEPTH.md` — independent source-verified deep dive,
  concrete event-trace reconstruction, implementation assessment, and
  hardware test results, preserved in this log §1.2-§1.7.
- `S068_PAT_ASSIGN_BUG_IMPLEMENTATION.md` — exact line-by-line patch
  schedule, preserved in this log §1.4, §1.6, §5.1-§5.2, §6.
- `S068_MISSING_CHASELIGHT.md` — root cause, exact fix specification, risk
  assessment, and hardware acceptance, preserved in this log §2.
- `S068_TRACK_SETTINGS_IGNORED.md` — root cause 1/2/3, fix, hardware
  acceptance, and deferred-item detail, preserved in this log §3.

`S069_ATS_PAT_BOUNDED_CPU.md` is **not** disposed of by this closeout — it
is the settled starting plan for Session 069 and remains a live working
document. Its full substance is nonetheless preserved in this log §4 in
case it is deleted before Session 069 begins. `S068_AUTOSAVE_REENABLE.md`
and `S069_GENERAL_FITNESS_AGENDA.md` are likewise not part of this
closeout's five source documents; their relevant content is summarized in
this log §4.4-§4.5 for continuity.
