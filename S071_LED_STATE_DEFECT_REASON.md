# S071 LED State Defect — Direct Reason

`LED_SEQ1` (user-facing SEQ index 0) is being displayed through an unintended, persistent sequencer **CHASE inversion layer**. The Pattern step, Scene edit-mask, Scene-present mask, and Scene activity values are not the common cause of the three opposite readings.

During boot, the filesystem restore calls `seq_alignActivePatternToScene()`. Although that API deliberately omits `led_notifyPatternChanged()` so the restore has no explicit LED notification, it calls `seq_realignActivePatternToMasterClock()`. That function unconditionally writes `seq_ledState.chaseStep` and sets `SEQ_LED_DIRTY_CHASE`; it does not test `seq_running`. At boot `seq_masterStepClock` is zero, so every track realigns to step 0 and the queued chase step is 0 even though transport is stopped.

`menu_start()` then paints the correct Pattern base LEDs. On the first foreground call to `led_processSeqLedState()`, the pending boot event reaches `led_updateCurrentStep(0)` and `led_setActive_step(0)`. Because boot is on a VOICE page and the viewed and played Scene indices match, this installs `LED_LAYER_CHASE` on `LED_STEP1`/`LED_SEQ1`. A chase is rendered by restoring the stored base state and toggling it, so it is an inversion, not an independent “on” state.

That one inversion produces all three reported results:

| View | Correct base for SEQ index 0 | CHASE-rendered result |
|---|---:|---:|
| Track 0, step 0 active | on | off |
| VOICE-held, Scene 0 absent from the edit mask | off | on |
| PERF, Scene 0 present and containing active steps | on | off |

The layer survives those view changes because `led_clearSequencerLeds()`, `menu_refreshVoiceHeldSceneLeds()`, and `menu_refreshPerfSceneLeds()` rewrite base states and cancel blink layers, but none calls `led_clearActive_step()`. `led_setValue()` subsequently invokes `led_renderFromStack()`, which deliberately reapplies the active CHASE inversion over every new base value. Thus the shared row changes meaning while retaining a chase layer created for its earlier step-view meaning.

This failure is not inherently specific to Scene 0 or to mask data. Cold boot always selects SEQ index 0 because the master step clock is zero. Any of the 16 STEP/SEQ LEDs can be complemented later if a chase is installed at that column (the current step modulo 16) and transport stops or the row is repurposed before a chase update calls `led_clearActive_step()`. LEDs outside the shared STEP/SEQ row cannot be affected by this mechanism.
