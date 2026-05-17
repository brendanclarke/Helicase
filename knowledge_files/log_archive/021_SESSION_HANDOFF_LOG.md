# Session 021 Handoff Log

```
DATE: 2026-05-16
SESSION GOAL: Trace rear audio jack-detect inputs, confirm final MCU pin mapping, and
              integrate low-overhead runtime jack-detect reads into the current ISR
              architecture for mixer routing decisions.
COMPLETED: Hardware trace was closed and validated: OUT1L=PD6, OUT1R=PD7,
           OUT2L=PB4, OUT2R=PB6. Firmware now updates jack-detect state via an
           ISR split strategy matching current architecture constraints:
           - PB4/PB6 sampled in TIM6_DAC_IRQHandler (1kHz service tick)
           - PD6/PD7 handled by EXTI9_5 both-edge events (shared IRQ23)
           mixer_checkOutJackAvailable() now reads ISR-fed cached states through
           l1/r1/l2/r2 availability variables. Hardware docs/log index updated.
VERIFIED ON HARDWARE: Yes. User confirmed trace and runtime detection behavior works,
                      including routing behavior updates (with additional user-side
                      fallback logic when no outputs appear connected).

CHANGES THIS SESSION:
- Core/DSPAudio/mixer.c:
  - Added ISR-fed cached jack availability state:
    - OUT1L (PD6) -> mixer_out_l1_available
    - OUT1R (PD7) -> mixer_out_r1_available
    - OUT2L (PB4) -> mixer_out_l2_available
    - OUT2R (PB6) -> mixer_out_r2_available
  - Added setter APIs:
    - mixer_setOutJackDetectPB(pb4_high, pb6_high)
    - mixer_setOutJackDetectPD(pd6_high, pd7_high)
  - Switched mixer_checkOutJackAvailable() to consume cached values via local
    variables exactly named:
    - uint8_t l1_Available
    - uint8_t r1_Available
    - uint8_t l2_Available
    - uint8_t r2_Available
  - Removed placeholder "all available" behavior used before trace completion.
- Core/DSPAudio/mixer.h:
  - Declared mixer_setOutJackDetectPB() and mixer_setOutJackDetectPD().
- Core/Hardware/timebase.c:
  - Included mixer.h.
  - In TIM6_DAC_IRQHandler(), added PB4/PB6 sampling from GPIOB_IDR and forwarded
    to mixer_setOutJackDetectPB().
  - This piggybacks on existing TIM6 service cadence (1kHz), avoiding a new timer
    or foreground poll path.
- Core/Hardware/triggerJacks.c:
  - Included mixer.h.
  - Added PD6/PD7 mask definitions and configured PD6/PD7 as plain inputs,
    no pulls (alongside PD4/PD5).
  - Extended SYSCFG_EXTICR2 routing from EXTI4/5-only to EXTI4..7 on Port D.
  - Extended EXTI mask/trigger setup so PD6/PD7 generate both-edge EXTI events.
  - Extended EXTI9_5_IRQHandler() to:
    - snapshot GPIOD_IDR once,
    - process existing PD5 reset semantics,
    - update mixer PD jack-detect cache on PD6/PD7 pending edges,
    - clear only relevant pending bits.
  - Seeded initial PD6/PD7 state in triggerJacks_init() so mixer has valid state
    before first jack edge event.
- knowledge_files/hardware_archive/HARDWARE_MAP.md:
  - Added confirmed OUT jack-detect rows for PD6/PD7/PB4/PB6.
  - Updated package notes to mark PD6/PD7 as confirmed in-use.
- knowledge_files/hardware_archive/XP_CONNECTOR_MAPS.md:
  - Added confirmed connector mappings:
    - XP12 pin 9 -> OUT1R -> PD7
    - XP12 pin 13 -> OUT1L -> PD6
    - XP13 pin 2 -> OUT2R -> PB6
    - XP13 pin 20 -> OUT2L -> PB4
  - Removed stale unknown-note wording for PD6/PD7.
- main.c (temporary diagnostic phase during trace):
  - Added then narrowed candidate pin-change diagnostic display using
    lcd_diagDisplayInt(); final discovery path reduced to unmapped candidates.

KNOWN ISSUES INTRODUCED:
- None identified in build or user hardware validation.

KNOWN ISSUES RESOLVED:
- Rear output jack-detect mapping uncertainty resolved:
  - OUT1L=PD6, OUT1R=PD7, OUT2L=PB4, OUT2R=PB6.
- mixer_checkOutJackAvailable() no longer depends on stale placeholder "always on"
  states; it now consumes ISR-fed runtime detect state.

NEXT SESSION RECOMMENDED GOAL:
Validate and harden routing policy edge cases now that detect inputs are live,
including explicit behavior when all detects read disconnected (headphone/default
fallback policy) and any desired debounce/hysteresis for rapid plug churn.

BLOCKERS:
- None. Functional trace and integration are complete.

CRITICAL REMINDERS FOR NEXT SESSION:
- EXTI line sharing on STM32F7 matters: PB6 and PD6 cannot both be EXTI6 sources.
  Current design intentionally uses:
  - PB4/PB6 via TIM6 polling
  - PD6/PD7 via EXTI9_5
- EXTI_IMR must still be cleared to 0 at top of main() before sysclk_init().
- Do not add internal pulls on these jack-detect lines unless hardware behavior is
  re-characterized; current circuit behavior is: no plug=LOW, plug inserted=HIGH.
- Keep PD5 reset semantics unchanged (active-low run/reset gate) while sharing
  EXTI9_5 with PD6/PD7.
```

---

## Detailed Notes

### 1) Trace Outcome (Final Confirmed Mapping)

Confirmed by user hardware test in this session:
- OUT1L -> PD6
- OUT1R -> PD7
- OUT2L -> PB4
- OUT2R -> PB6

Electrical behavior observed and used in firmware logic:
- No plug present: line held LOW (near 0V, switch to GND closed)
- Plug inserted: line goes HIGH (~3.2V, switch opens)

### 2) Why the ISR Split Is Required

The selected split was driven by both architecture and EXTI mux constraints.

- PB4/PB6 are on GPIOB and are cheap to sample in already-running TIM6 service ISR.
- PD6/PD7 can be edge-driven in EXTI9_5 with near-zero steady-state overhead.

Important STM32 EXTI constraint:
- EXTI lines are shared by line number across ports (e.g., EXTI6 can map to PB6 or
  PD6, not both simultaneously).
- Because OUT2R uses PB6 and OUT1L uses PD6, only one of those can own EXTI6.
- This ruled out an all-EXTI solution and justified the mixed design.

### 3) TIM/EXTI Interaction Model (Debug Reference)

#### TIM6 path (PB4/PB6)
- TIM6_DAC_IRQHandler runs continuously at 1kHz for front-panel service.
- After existing LED/button SPI exchange, firmware reads GPIOB_IDR and updates:
  - l2 from PB4
  - r2 from PB6
- Cost is a small constant per tick and avoids main-loop polling.

#### EXTI path (PD6/PD7)
- EXTI9_5 IRQ23 already exists for PD5 reset input handling.
- Session 021 extends same handler to consume pending PD6/PD7 edges.
- Handler snapshots GPIOD_IDR once per interrupt, updates PD cache, and clears only
  pending PD6/PD7 bits that fired.
- PD5 handling remains intact in same ISR and continues to push timestamped reset
  events into trigger event ring.

#### Sequencer timing owner remains unchanged
- TIM3 still owns seq_tick + MIDI realtime event drain + trigger jack event drain.
- Session 021 does not move sequencing timing work.

### 4) Mixer Hook Contract (Now Active)

The routing hook continues to use the established local naming convention:

- l1_Available (OUT1L / PD6)
- r1_Available (OUT1R / PD7)
- l2_Available (OUT2L / PB4)
- r2_Available (OUT2R / PB6)

These are now sourced from cached ISR-updated states, not direct GPIO reads inside
mixer hot path and not static placeholders.

### 5) Build / Verification

- Build status: `make -j4` passes.
- Hardware validation status (user): jack-detect tracing and behavior confirmed
  working; user additionally tuned fallback behavior in routing logic for
  "nothing connected" case.

### 6) Documentation Synchronization in Session 021

Session 021 sync includes:
- `knowledge_files/hardware_archive/HARDWARE_MAP.md`
- `knowledge_files/hardware_archive/XP_CONNECTOR_MAPS.md`
- `knowledge_files/log_archive/000_SESSION_INDEX.md`
- this handoff file (`021_SESSION_HANDOFF_LOG.md`)

