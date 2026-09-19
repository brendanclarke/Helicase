# S068 missing chaselight assessment

## Root cause

The boot alignment path updates three of the four Pattern/Scene authorities
used by the chaselight and leaves the fourth at its BSS default:

| State | After restoring active Scene N |
|---|---:|
| `scene_active_index` | N |
| `seq_activePattern` | N |
| `menu_shownPattern` | N |
| `menu_playedPattern` | **0** |

After Bank restore, `filesystem.c` calls
`seq_alignActivePatternToScene(op_bank_active_scene)` and
`menu_setShownPattern(op_bank_active_scene)`. The sequencer helper deliberately
does not call `led_notifyPatternChanged()` because that notifier also performs
runtime presentation/performance side effects that are inappropriate during
pre-audio boot. The helper does not otherwise update `menu_playedPattern`.

`menu_playedPattern` is zero-initialized and is assigned only by
`led_notifyPatternChanged()`. The chase renderer reads that mirror, not
`seq_activePattern`:

```text
shownPattern = menu_getViewedPattern();
playedPattern = menu_playedPattern;
show chase only if shownPattern == playedPattern
```

Therefore a Bank that boots with active Scene N != 0 has
`menu_shownPattern == N` and `menu_playedPattern == 0`. Every chase update is
deliberately cleared by `led_updateCurrentStep()`, even though the producer is
healthy. A Bank that boots into Scene 0 happens to work because the stale BSS
default equals the real Scene, which explains why the failure is intermittent
across fresh boots.

Changing Scenes in PERF mode calls `seq_selectActivePattern()`, which calls
`led_notifyPatternChanged(seq_activePattern)`. That notifier finally assigns
`menu_playedPattern`, making it equal to the shown Pattern; the next chase
dirty event becomes visible. This exactly matches the reported self-repair.

## What is not failing

- `seq_realignActivePatternToMasterClock()` writes `seq_ledState.chaseStep`
  and sets `SEQ_LED_DIRTY_CHASE` during boot alignment.
- Normal scheduler advances continue to set the same dirty bit.
- `led_processSeqLedState()` drains it in the foreground.
- The missing LED is not caused by absence of producer events or by the
  temporary LED toggle itself. It is rejected at the shown/played equality
  predicate.

## Targeted correction

Keep the no-MIDI/no-note-off/no-PERF-repaint property of the boot alignment
path, but align the played-Pattern UI mirror at the same commit boundary.
Either:

1. extend `seq_alignActivePatternToScene()` to publish the validated Scene to a
   side-effect-free Menu/LED state setter, or
2. add a dedicated `led_alignPlayedPattern()`/`menu_setPlayedPattern()` and call
   it beside `menu_setShownPattern()` after every filesystem Scene/Bank
   realignment.

Do not call the existing `led_notifyPatternChanged()` blindly from pre-audio
filesystem code: its follow, PERF repaint, program-change companion path, and
ownership assumptions are the reason the alignment helper was split out.
The invariant should instead be explicit:

> At every committed playback realignment, `menu_playedPattern` must equal
> `seq_activePattern` before a chase dirty event can be drained.

This invariant should also be applied to the other filesystem realignment call
sites, not only the Bank Load phase shown by the report.

## Acceptance test

- Save/restore a Bank with each active Scene 0-15 and follow both OFF and ON.
- Start playback without entering PERF. On VOICE, STEP, and EUKLID pages, the
  correct visible step must chase immediately.
- Confirm PERF still suppresses chase because it owns the SEQ row.
- Change Scene from PERF and through runtime Bank/Scene Load; verify
  `scene_getActiveIndex()`, `seq_activePattern`, `menu_shownPattern`, and
  `menu_playedPattern` at each completion boundary.
- Confirm boot alignment emits no MIDI program change and no all-notes-off.

No `Core/` code was changed in this assessment.
