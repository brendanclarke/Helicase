# Operation Modes

The mode buttons change what the display, knobs and buttons do. The modes are Voice, Performance
(PERF), Step, Load/Save and, with SHIFT, the Effect page, the pattern generator and the settings
menu.

## Voice Mode

Voice mode edits sounds and basic patterns.

- **Display and knobs:** the parameters of the selected synthesis page for the active voice.
- **SEQ buttons:** set and clear the steps of the visible bar.
- **SELECT buttons:** choose the synthesis page (oscillator, amplitude envelope, modulation, FM,
  transient, filter, LFO, mix).
- **Voice buttons:** select the active voice and track; with SHIFT, mute.

**Example --- change the filter cutoff of voice 3:** press voice button 3, press the SELECT button
for the filter page, turn the first knob to change `frq`.

### Step edit with SHIFT

Hold SHIFT in Voice mode to show the step parameter page of the last-touched step. Turn the knobs
to edit its specials. A flashing LED marks the step.

### Morph view

Hold SHIFT (or latch the view with SHIFT + Voice mode) to see and edit the **Morph endpoint** of
each parameter in Voice mode. A parameter that cannot be morphed shows its normal value.

\figplaceholder{mode_voice.png}{Voice mode controls.}{Front panel with Voice mode regions highlighted: display, knobs, SEQ buttons, SELECT buttons and voice buttons, each with a one-line label.}

## Step Mode

Step mode is the inverse of Voice mode with respect to the sequencer: the SEQ buttons **select**
steps (hold SHIFT to set or clear them) and the display always shows the step parameter page, so
specials and automation are edited without holding SHIFT. The SELECT buttons show and edit the
step's sub-divisions. Holding several steps edits velocity, note and condition on all of them.

Hold SHIFT in Step mode to see the Morph endpoints of the track's length, scale and shuffle.
**SHIFT + Step mode button** enters the pattern generator.

\verify{The previous LXR described Step mode as selecting steps with the SEQ buttons; confirm the behaviour of SHIFT in Step mode in this firmware.}

## Performance Mode

Performance mode is for playing live. Voice editing is not available; the display shows the PERF
pages.

\figplaceholder{mode_perf.png}{Performance mode controls.}{Front panel with the PERF regions highlighted: SEQ buttons as Scene buttons, SELECT buttons as looper divisions, BAR buttons, voice buttons as mutes, display showing mrp fxm 1VM 2VM.}

### Performance menu

\begin{paramtable}
mrp & Global Morph  & Sets every voice's Morph amount and the Effect Morph at once. 0 to 255. \\
fxm & Effect Morph  & Morph amount of the Scene's Effect. \\
1VM -- 6VM & Voice Morph & Morph amount of one voice. \\
\end{paramtable}

The global Morph control is never recorded. The individual voice and Effect Morph cells are.

### Scene change

- **SEQ button:** switch to that Scene. The change happens at the **Scene change division** `scd`
  (immediately if the sequencer is stopped).
- **SHIFT + SEQ button:** *queue* a change at the **queue division** `qcd`; a new queue replaces a
  pending one. The queued Scene's LED flashes at double rate.
- **Reset behaviour:** `pcr` (pattern change reset, on the settings menu) decides whether a direct
  change restarts every track from step 1. If it is off, the new Scene is *realigned* to the
  running master clock and the effect sequence stays aligned too. Queued changes reset only when
  the queue action `qac` includes a reset.

\begin{paramtable}
scd & Scene change division & 0 to 9: instant, 1/8 note, 1/4, 1/2, 1 bar, 2 bars, 3 bars, 4 bars, end of shortest track, end of longest track. \\
qcd & Queue division        & 0 to 17: instant and a longer range of divisions from 1/8 note up to 16 bars, and end of shortest or longest track. \\
qac & Queue action         & What happens when a queued change fires. Includes reset (\texttt{res}), unmute-and-reset (\texttt{u+r}), morph-reset variants (\texttt{mrp}, \texttt{o+r}, \texttt{mmr}), reload (\texttt{rld}) and \texttt{r+r}. 0 to 8. \\
pcr & Pattern change reset & On: direct and chained Scene changes restart the pattern. \\
\end{paramtable}

\verify{Division names shown on screen for scd and qcd, and the full list of nine qac values. The ``1/8 note, 1/4, 1/2, bar'' labels here are derived from the stored step counts.}

### Scene chains

Each Scene can chain automatically to another. Open the chain editor with **SHIFT + Play + SEQ**
(hold SHIFT, tap Play, then press the Scene's SEQ button). The editor, a latched menu, shows
`cn1 nx1 cn2 nx2`: two *conditions*, each with a *next* Scene.

- **Conditions** can be: always, after N repeats of the pattern, after N bars, a probability at a
  repeat or bar end, a bar number within a group of four or eight, or after a particular previous
  Scene. Conditions can also test whether fill or loop is active.
- **Next** can be: the same Scene (a repeat), the next or previous Scene, a specific Scene, a random
  Scene in a range, a random next or previous few, or **stop**.
- Condition 1 has priority; two probabilities that add to 100 or more are drawn proportionally.
- Chain conditions are not evaluated while a Bank is loading.

\figplaceholder{scene_chain_example.png}{Scene chain example.}{Timeline diagram: Scene 1 plays 3 times, then Scene 2 plays once with probability 50 percent, otherwise Scene 1 repeats. Show the chain editor display below the timeline.}

### Looper

Hold a SELECT button to loop the playing tracks. The eight buttons set the division: 1 bar, 1/2,
1/4, 1/8, 1/16, 1/32, 1/64, 1/128. The last pressed wins.

- Hold a **voice button** first, then press SELECT, to loop only those tracks.
- With the loop running, a voice button adds that track to the loop (with a fresh loop start);
  releasing it drops the track, which resumes where it would have been.
- **BAR 1 / BAR 2** while looping apply a momentary **triplet** (2/3) or **dotted** (3/2) length.
- Releasing every loop button returns the sequencer to where it would have been without looping.
- A track that is not *loopable* (see the STEP page) is ignored by the looper.
- While the loop is engaged the SELECT, BAR and voice buttons keep their looper meaning; live
  recording keeps running.

### Trigger overlay (BAR buttons)

When not looping, **BAR 1** and **BAR 2** toggle the `<` and `>` trigger overlays (one at a time).
With an overlay on, the voice buttons **fire the overlay trigger** instead of selecting or muting;
release is the note-off. SHIFT + voice button still mutes.

Each overlay is configured on the record settings page (SHIFT + Rec):

\begin{paramtable}
pb< / pb> & Overlay trigger type & Single hits at velocity 127, 100, 64 or 32 (\texttt{vmx v10 v64 v32}); two flams (\texttt{flm fls}); or a held roll at \texttt{1/8 8tr 16 16t 32 32t 64 64t 128}. \\
pn< / pn> & Overlay note & Pitch of the trigger. \texttt{def} is the default pitch. \\
pv< / pv> & Roll rhythm & Velocity rhythm of the roll repeats (the same list as the step \texttt{rtm}). \\
\end{paramtable}

Rolls repeat until you release the button. Tracks have independent rolls, so several can run at
once. The overlay also plays from MIDI notes (see the MIDI chapter), and rolls keep running while
the sequencer is stopped.

### Manual re-align and per-track Scenes

See *Per-track Scene playback* in the Sequencer chapter.

## Live Recording

Live record writes what you play and turn onto **one recording track** of one Scene.

| Gesture | Mode | Behaviour |
|---|---|---|
| Tap **Rec** | LATCH | Recording toggles on release. The first adjustment of a parameter *latches* it: during the take the parameter owns its automation on the recording track. |
| Hold **Rec** (more than 300 ms) | OVERDUB | Only parameters you adjust are written, only while the button is held. Nothing latches or clears. |
| **SHIFT + Clear** while recording | ERASE | Live erase (see below). |

Arming while stopped is allowed; recording begins when playback does. Any mode change of the
recorder, a change of recording track or any Scene change starts a new take.

### What is recorded

- Triggers from the BAR overlay and from MIDI notes (single hits, flams and held rolls). A held roll
  is stored as a roll step at its true rate and length; there is no slow/fast split.
- Parameter moves on the Voice pages, per-voice Scene mix cells, Effect parameters and the Effect
  Morph cell, track length, scale and shuffle (when the viewed Scene is the recording Scene), and the
  PERF voice and Effect Morph cells.
- **Not recorded:** the global Morph `mrp`, settings and Load/Save pages, step editors, effect
  sequence locks, LFO and velocity targets, the effect `typ`, and Morph-view edits.

### Timing

Hits are placed where they were played, *including* the time the note spent waiting to be handled.
A hit that lands slightly ahead of the playhead is stored on the next step with an echo mark so the
step does not sound twice. The sequence follows the **quantise** setting.

### Record settings (SHIFT + Rec)

\begin{paramtable}
qnt & Quantise & \texttt{off}, \texttt{1/8}, \texttt{1/16}, \texttt{1/32}, \texttt{1/64}, or \texttt{trk} (snap to the track's own step grid). Applied while recording; it cannot be applied afterwards. \\
rtk & Recording track & The track that receives the recording, 1 to 7. Follows the active track; SHIFT + Rec can override it. \\
\end{paramtable}

\verify{Whether the quantise values appear as shown on the display and where qnt and rtk sit: they share a page with pv< pv> on the second SHIFT+REC sub-page.}

### Live erase

Hold **SHIFT + Clear** while recording to erase:

- **Turn a knob** over an automatable parameter: that parameter's automation is erased from the
  recording track for every step the sequence passes while erase is on.
- **Hold a SEQ button** in Voice or Step mode: that step's triggers and specials on the active track
  are removed and not played (automation still plays).
- **Hold a voice button:** that track's triggers, specials, conditions and automation are removed
  and not played.
- On the Effect page, holding a SEQ button removes the locks of each Effect step the effect
  playhead reaches.

## Morph

The Morph function transitions from the Normal value of each parameter to its Morph value.

- **Where Morph values live:** every morphable parameter has a Morph value. Hold SHIFT in the Voice,
  Step or Effect pages to edit the Morph endpoint.
- **Where Morph amounts come from:** the PERF menu (`mrp`, `fxm`, `1VM` to `6VM`), MIDI CC1,
  step automation, velocity, and LFO. CC1 on the global channel sets the global Morph; CC1 on a
  voice's channel sets that voice's Morph and is recorded.
- Morph **never blends** selector parameters such as oscillator type, run mode, chain type or the
  Alkaline waveform; those step instead.
- Global Morph also sets the Effect Morph. A global Morph move wins over the Effect's Morph lane
  lock and over `fxm` automation until the next Morph automation.

## Effect Page

Press **SHIFT + Performance mode**. The Effect page edits the Scene's Effect and its 16-step
effect sequence. See the Mixer and Effects chapter.

## Pattern Generator

\draftnote{The firmware has an Euclidean generator and a SOM generator, but the engineering notes
describe the generator page as a proposal (``one Generator page, pick a type, Off/Overdub/Replace'')
and report that the SOM generator is not currently wired in. Do not document generators in the
release manual until the generator page exists. The earlier LXR's pattern generator
(\texttt{len stp rot ssr}) is the model for the Euclidean part.}

## Load/Save Mode

See the Storage chapter.

## Settings Menu

Press **SHIFT + Load/Save**. The settings menu holds the global settings. Its pages are listed
below. Settings are stored in `settings.cfg` on the card, which is read at boot.

\begin{paramtable}
snc & Sync source & \texttt{off usb din pls aut}: external clock source. \texttt{aut} picks the highest priority source present (jack pulse over DIN over USB over internal). \\
ch  & Global MIDI channel & \texttt{off} or 1 to 16. \\
txf / rxf & MIDI send / receive filter & Which message classes are sent / acted on: notes (N), realtime (R), control (C), program change (P). \\
mrt & MIDI routing & Pass-through routing between DIN and USB. \\
pcr & Pattern change reset & See the Performance mode section. \\
scd qcd qac & Scene change timing & See the Performance mode section. \\
cmp cam ctm csc & Bus compressor & Last page of the menu; see the Mixer and Effects chapter. \\
\end{paramtable}

\verify{The settings menu has further entries that the project documents do not list (tempo, display, interpolation, fetch behaviour, dither and so on). Take the full list from the firmware's Global page.}

\tip{A card with no MIDI filter keys loads with send, receive and routing all set to \texttt{off}:
the LXR will not consume, send or forward any MIDI until you set \texttt{rxf} and \texttt{txf} to
\texttt{all}.}
