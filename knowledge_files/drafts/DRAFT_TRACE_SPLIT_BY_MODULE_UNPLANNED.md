# DRAFT — Splitting `asavetrc.bin` Tracing by Module

**Status: draft for discussion only. Nothing in this document should be
implemented this session.** Written up because this session's Bank test
(`S056_BANK_TESTS.md` §3.3) hit the existing single shared trace ring's
capacity limit — 6,134 of 17,760 records dropped during one boot's
whole-resident-Bank dirty mark, which is also the already-known, undecided
`AUTOSAVE_TRACE_RECORD_COUNT` item flagged in `MEMORY.md`.

## 1. The problem

`DEV_MODE_LOGGING` (`config.h:88`) is a single on/off switch gating **every**
trace producer in the firmware. All 20 stage letters (`D I J N L R W F G B
S A V M C P T X O E`, defined in `AutosaveTrace.h`) share one SRAM ring
(`AUTOSAVE_TRACE_RECORD_COUNT`, currently 2,048 records, temporarily
expanded from a normal default of 64) and one file (`/asavetrc.bin`).

These 20 stages are not remotely uniform in volume or purpose. From this
session's decoded trace:

| Stage(s) | Purpose | Observed volume |
|---|---|---|
| `D` | one accepted payload-bit OR (per byte offset) | 11,626 records from six scenes' worth of one dirty-mark sweep alone |
| `I` `J` `N` `L` `B` | mutation/load summary witnesses (per Instrument, per Kit/Scene, per Bank) | tens to low hundreds per operation |
| `S` `A` `V` `M` `C` `P` `T` | AutoSave writer lifecycle (one full cycle) | ~7 records per 5-second write cycle |
| `R` (and the proposed `K`, see `S056_BANK_SETTINGS_CORRECTION.md`) | unconditional Load/Save completion witness | 1 record per operation |
| `O` | Save lifecycle checkpoints | ~5 records per Save |
| `X` | cooperative stall detector | 0 in normal operation |
| `F` `G` | trace-ring's own suppression/drop-count self-report | rare, only under pressure |
| `E` | universal error backstop | 0 in normal operation |

`D` alone is responsible for effectively all of the volume. When something
marks a large amount of state dirty at once (a whole-Bank load, a
whole-Instrument mark), it can burn through the entire ring before the
500 ms periodic flush gets an idle tick to drain it — and when the ring is
full, *new* records are dropped, not old ones evicted (`AutosaveTrace.c:73-77`),
so the records lost are whatever came after the ring first filled, which in
practice this session meant six-plus scenes' worth of `D` records that would
have been useful evidence disappeared, while the low-volume lifecycle
records (`S A V M C P T`) that actually answer "did the write happen"
survived only because they happened to occur after the burst.

That's backwards from what an investigation usually needs: the low-volume
lifecycle/error/completion signal is exactly what you want to never lose,
and the high-volume per-byte dirty detail is exactly what you can afford to
lose (or disable) when you're not specifically debugging the dirty-marking
path itself.

## 2. Proposed module grouping

Group the 20 (soon 21, with `K`) stages by what question they answer, not
by which source file emits them:

| Module | Stages | Typical volume | Question it answers |
|---|---|---|---|
| **Dirty-mark detail** | `D` `I` `J` `N` | very high | "exactly which bytes/fields got marked, and by what" |
| **Load/Save summary** | `L` `B` `R` `K` `O` | low | "what did this Load/Save actually do, and did it reach its completion callback" |
| **AutoSave writer lifecycle** | `S` `A` `V` `M` `C` `P` `T` | low, periodic | "did the AutoSave record actually get read/validated/written this cycle" |
| **Stall/hang diagnostics** | `X` | zero unless wedged | "is a cooperative state machine stuck" |
| **Trace-ring self-diagnostics** | `F` `G` | zero unless overloaded | "is the trace system itself keeping up" |
| **Error backstop** | `E` | zero unless failing | "did any operation reach FS_STATUS_ERROR" |

The last three (stall, ring self-diagnostics, error backstop) are always
cheap and always worth keeping on — they cost nothing in the normal case by
construction. The real split that matters is **Dirty-mark detail** (huge,
optional) vs. everything else (small, usually wanted).

## 3. Toggle mechanism — two options

**Option A: compile-time, one `#define` bitmask per module in `config.h`.**
Matches the project's existing idiom exactly (`DEV_MODE_DIAGNOSTIC`,
`DEV_MODE_LOGGING`, `DEV_LOGGING_IWDG` are all compile-time). Each
`autosaveTrace_record()` call site (or a thin per-module wrapper) is guarded
by `#if TRACE_MODULE_DIRTY_DETAIL` etc., so a disabled module costs *zero*
code size and zero SRAM — the call disappears entirely, same as
`DEV_MODE_LOGGING == 0` today. Changing modules requires a rebuild, and per
the existing **Build-system footgun** note in `MEMORY.md`, requires
`make clean` since the Makefile has no header dependency tracking.

**Option B: runtime bitmask, checked inside `autosaveTrace_record()`.**
One small global (e.g. a `uint8_t` bitmask, 7 modules fit in one byte) that
can be changed from a hidden Dev Mode menu page without rebuilding — much
faster iteration when chasing a live bug on hardware, since you could
disable **Dirty-mark detail** for a Bank test and re-enable it for an
Instrument test without reflashing. Costs one branch per trace call
(negligible) and, more importantly, **one byte of always-resident state**,
which under this project's RAM Allocation Approval Policy
(`MEMORY.md`) needs its exact size/region/lifetime/owner identified and
explicit user sign-off before it's added, same as any other new
allocation — flagging that now so it isn't skipped later just because the
byte count is small.

Leaning toward **A as the default, with B as a possible later addition** —
A is zero-cost and fits the existing pattern with no new approval needed; B
is a real usability win for active debugging sessions but should go through
the RAM approval step explicitly rather than sliding in as part of this
change.

## 4. Ring sizing implications

Filtering happens at the `autosaveTrace_record()` call site, before a
record ever reaches the ring — so a disabled module doesn't just get
dropped under pressure, it's never appended at all, and the *entire* ring
(whatever size it ends up being) is available to whatever modules remain
enabled. This directly fixes this session's specific problem: a Bank-focused
session that only cares about "did settings.cfg/AutoSave/HCNAMES actually
commit" could run with **Dirty-mark detail off**, and the low-volume
lifecycle/completion/error modules would then never be at risk of losing
records to a dirty-mark burst — probably enough on its own to let
`AUTOSAVE_TRACE_RECORD_COUNT` shrink back toward its normal-default 64
outside of a dirty-mark-specific investigation, resolving the still-open
"needs a decision" item without necessarily needing the ring itself to grow
further.

## 5. Open questions to resolve before implementing

- Should the default build ship with **Dirty-mark detail** on or off? (Given
  it's the highest-volume and least-often-needed-day-to-day module,
  off-by-default with an easy opt-in for dirty-marking investigations seems
  right, but this is a call for whoever's driving the next investigation
  that actually needs it.)
- Does `O` (Save lifecycle) belong with Load/Save summary, or does it need
  its own module given it's Save-specific and the others in that group are
  Load-and-Save? Grouping by save vs load could also be considered instead
  of (or in addition to) grouping by volume.
- Per-module compile flags vs. one combined bitmask constant — six
  `#define`s in `config.h` (`DEV_MODE_LOGGING` already lives there) is
  consistent with the existing style, but a single `TRACE_MODULE_MASK`
  bitmask constant would be less verbose and easier to extend. No strong
  preference yet.
- Do the decode tools (`decode_devlogs.py`, `devlog_unpack.py`) need any
  changes? Likely **no** — they decode whatever stage letters are actually
  present in the file; a build with a module compiled out simply never
  produces those letters, and the tools already handle "this stage never
  appears" gracefully. Worth confirming once a real build exists to test
  against.
- Interaction with the proposed `'K'` stage
  (`S056_BANK_SETTINGS_CORRECTION.md`) and the proposed `'H'` drift-check
  stage (`S056_NAMES_CORRUPTION.md`): both should be slotted into the
  **Load/Save summary** and **Dirty-mark detail** modules respectively when/if
  this lands, so they inherit the right default-on/off behavior rather than
  needing their own bespoke toggle.

## 6. Non-goals for this draft

- Not proposing any change to `DEV_MODE_DIAGNOSTIC` (the screen-only
  diagnostic display) — this is specifically about `/asavetrc.bin` file
  tracing.
- Not proposing to change what any individual stage records, only whether
  its call sites compile in or fire at all.
- Not proposing an implementation timeline — this is a draft to react to,
  per this session's request, not a plan to execute.
