# Sequencer

The sequencer plays the Pattern of each track's played Scene. The basic idea is familiar from
classic step machines, with considerably more per-step control.

## Tracks

There are **7 tracks**: one per voice, plus an extra track that plays the open hi-hat of voice 6.
Each track has its own length, step scale, shuffle and run mode, so tracks can drift against one
another and realign.

### Select, mute and unmute

- The active track is chosen with the 7 **voice buttons**. Its LED is lit.
- **SHIFT + voice button** mutes or unmutes the track.
- In Performance mode the roles are inverted: a short tap on a voice button toggles mute without
  SHIFT, and holding it allows other gestures (per-track Scene playback, overlay triggers).
- A muted track does not play and does not play its step automation. Effect tails keep ringing.

## Steps

Each track has up to **128 steps**, shown as eight bars of 16 steps. The 16 sequencer buttons show
the bar currently being played or viewed. Press a button to set or clear a step.

### Bars and sub steps

The eight SELECT LEDs show the bar of the active track that is playing; the LED of a new bar
pulses once. Sub steps for fine placement are edited in Step mode.

\figplaceholder{substep_diagram.png}{Steps, bars and sub steps on the sequencer buttons.}{Row of 16 step buttons with the LEDs of the current bar, and below it the eight SELECT LEDs showing which of the eight bars is shown. Second panel: one step expanded into its sub-step divisions.}

\verify{The earlier LXR had 16 main steps with 8 sub steps each. The new Pattern model is 7 tracks of 128 steps, shown as 16 steps per bar. Confirm how sub steps are presented in Step mode and update this section and the figure.}

## Per-track settings (the STEP page)

Open Step mode and press the encoder to the track settings page. Page 1 has `len scl shf run`; page 2
has `ch nte lop rmp`.

\begin{paramtable}
len & Length      & Number of steps in the track (1 to 128). Each track may have a different length. \\
scl & Step scale  & How fast the track steps. A continuous scale from 1/64 (0) through 1/16 (76, the default) to 1/2 (127), with named stops: \texttt{/64 32t /32 16t d32 /16 8Tr d16 /8 4Tr d/8 /4 d/4 /2}. Other positions show the raw number. \\
shf & Shuffle     & Delays every second step on the 1/16 grid. Independent of scale. \\
run & Run mode    & \texttt{fwd} forward, \texttt{rev} reverse, \texttt{pip} ping-pong (end steps play twice), \texttt{rnd} random, \texttt{onc} once from the master clock then stop, \texttt{1fr} once from step 1 then stop. \\
ch  & MIDI channel & \texttt{off}, \texttt{glb} (follow the global channel), or 1 to 16. \\
nte & MIDI note   & \texttt{off}, \texttt{on} (no root note), or the root note of this track's receive range. \\
lop & Loopable    & Whether the PERF looper may loop this track. \\
rmp & Automation ramp & On: step automation values glide from step to step instead of jumping. \\
\end{paramtable}

Length, step scale and shuffle are also Morph targets (hold SHIFT on the page to edit the Morph
endpoint) and can be automated per step. Changing the scale mid-playback never causes drift.

\verify{Confirm the abbreviation of the Loopable cell and that SHIFT shows Morph endpoints for len scl shf only.}

## Step parameters

Hold a step and edit with the knobs (or select the step in Step mode) to reach its **specials**.
Every special is optional; a step with no specials plays at the default velocity and pitch.

\begin{paramtable}
vel & Velocity   & Note-on velocity, 0 to 127. Velocity 0 is an \emph{automation-only} step: envelopes are not retriggered but automation still applies. \\
nte & Note       & Pitch offset in semitones relative to the default pitch. \\
cnd & Condition  & Whether the step plays; see below. \\
rol & Roll       & Retrigger rate: \texttt{off 1/8 8tr 16 16t 32 32t 64 64t 128}. \\
tim & Timing     & Micro-timing: delays the step by a percentage of the step length. \\
rtm & Rhythm     & Velocity pattern for the repeats of a roll. \\
fil & Fill       & Percentage of the step length that the roll lasts (100 is the full step). \\
\end{paramtable}

\verify{On-screen tokens for condition, timing and fill are taken from the data model; only rol, rtm and the roll labels are confirmed in the menu code.}

### Conditions

A step with a condition plays only when the condition is true. A failed condition suppresses the
trigger **and** the step automation. The default is always-play.

- **Probability:** play the step with a fixed chance (ten levels).
- **Repeat X:Y:** play on the X-th of every Y passes through the track (ten combinations).
- **Fill / not fill:** play only while a fill is held (hold the Performance fill gesture longer than
  300 ms), or only while no fill is held.
- **Loop / not loop:** play only while the track is being looped by the PERF looper, or not.
- **Last / not last:** depends on whether the previous conditioned step fired.
- **Mute / not mute of track 1 to 7:** play only when the named track is muted (or not).
  A step that tests its own track's mute state is never silenced by that mute.

\tip{Conditions are evaluated once per step. A roll on the step uses the same decision for all
its repeats.}

### Rolls

A step with a roll special retriggers at the chosen rate for as long as its **window** lasts, which
is the step length multiplied by the fill percentage. The rate is an absolute note value; the track
scale decides only how long the window is, never the spacing of the hits.

- Two adjacent roll steps with the **same rate and rhythm** and a fill of 100 are *carried*: the
  roll continues across the boundary without an extra hit and without losing its rhythm.
- A step whose fill is below 100 ends its roll with a short gate duck, and the next step starts a
  fresh roll.
- A plain hit ends a running roll; a different roll replaces it. An empty or automation-only step
  does not interrupt it.
- The **rhythm** (`rtm`) shapes the velocities of the repeats: `off`, `1-2` to `4-5` (every n-th
  repeat is halved) or `1+2` to `4+5` (every n-th repeat is doubled).

## Automation

Each step can carry **parameter automation**: any Instrument, Effect or Scene parameter that is
marked automatable. Automation affects only that step and is ignored if the step does not play.

### Priority rules

- **Instrument parameters** (rule R12): a step's value holds until the voice's next *first*
  trigger of a step. Roll repeats after the first hit never reset it. A **menu edit**, including a
  MIDI write, overrides the automation immediately and keeps it suppressed until the voice's next
  trigger, a transport change or a Scene change.
- **Effect and Scene parameters** (rule R13): the last automation sets the value and it stays until
  the next automation; an effect-sequence lock beats track automation for its own step; a menu
  edit beats both until the next automation.
- With the track's **`rmp`** setting on, numeric parameters ramp linearly between consecutive
  played steps that both hold them. Ramps end at the next played step and at every position break,
  and never cross a loop wrap.
- The LFO and Morph modulate **around** whatever value automation sets.

### Adding automation

**Hold a step and turn a knob** (or encode a value on the Effect page): the value is locked on that
step. The step's LED flashes while it is held. Hold several steps to write the same value to all
of them.

**Edit in Step mode:** the step's automation page lists its entries. Choose a destination
category and parameter and set a value. Each step holds a number of entries, each with a unique
target.

**Record live:** see *Live Recording* in the Operation Modes chapter.

### Underlines

A parameter name is underlined when any step automation addresses it. The search runs in the
background, so underlines can appear a moment after you enter a page.

## Per-track Scene playback

In Performance mode, hold a voice button and press a SEQ button to make that track play a
different Scene's Pattern, instrument parameters, MIDI routing, fader and effect send. A
double-click on a SEQ button toggles the override. Tracks 6 and 7 move together. Recording is
disabled while any track plays a different Scene.

Pressing the pattern button of the currently viewed Scene again realigns every track to the master
clock, which is useful after tracks with different scales have drifted.

## Copy and Clear

\draftnote{The Copy/Clear specification is 85 KB; this section is a summary. Verify every gesture
on the device.}

Copy and clear work at several levels: single steps and ranges, bars, tracks, automation, whole
Patterns, and Scenes and their children (Kit, Effect, Morph). The Instrument and Effect level
operations follow the voice edit mask.

### Copy

1. Hold **Copy**; its LED flashes.
2. Press the source (a voice button for a track, a SEQ button for a step, a SELECT button for a
   bar or sub step).
3. Press the destination. The data is pasted at once and overwrites the destination.
4. Release Copy to abort.

### Clear

1. Press **SHIFT + Copy/Clear**.
2. The display offers what to clear (for example track, pattern, automation, Effect, Scene,
   Morph). Use the encoder to select.
3. Press Copy/Clear again to confirm, or release SHIFT to abort.

While a copy or clear is running, background saving is suspended until it finishes.

\verify{Exact choice list shown by the clear menu in each mode.}
