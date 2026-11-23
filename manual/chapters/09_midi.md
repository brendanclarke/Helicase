# MIDI

Helicase LXR has two MIDI interfaces: standard DIN connectors and a class-compliant USB port. Both
provide an input and output. Messages the LXR generates are sent on **both** ports.

## Settings

\begin{paramtable}
snc & Sync source & \texttt{off usb din pls aut}. MIDI realtime is used for \texttt{usb}, \texttt{din} and \texttt{aut}. In \texttt{aut} the priority from low to high is internal, USB, DIN, jack pulse. \\
ch  & Global channel (G) & \texttt{off} or 1 to 16. With \texttt{off}, nothing is received or sent on a global channel. \\
txf & Send filter & What the LXR sends from itself: any combination of N (notes), R (realtime), C (control) and P (program and bank change). \\
rxf & Receive filter & What the LXR acts on, with the same letters. \\
mrt & Routing & Pass-through routing: \texttt{off UtD DtD DtU bth DtA AtD all}. \\
\end{paramtable}

\attention{A card without these settings loads with receive, send and routing all \texttt{off}. The
LXR will not respond to, send or forward any MIDI until you set \texttt{rxf} and \texttt{txf} to
\texttt{all}.}

**Filter letters.** N is notes. R is realtime (clock and transport, plus song position and MTC).
C is control: NRPN, learned CCs, CC1 and CC2. P is program change and bank change (CC0). There is
no separate bank change enable.

**Routing (`mrt`).** `UtD` USB to DIN; `DtD` DIN to DIN; `DtU` DIN to USB; `bth` both of USB to DIN
and DIN to USB; `DtA` DIN to DIN and DIN to USB; `AtD` USB to DIN and DIN to DIN; `all` all three.
USB to USB is never routed. A message the LXR acts on is **not** forwarded; everything else is.
Clock and transport messages are always forwarded according to `mrt` whether or not they were used.

## Channels

- **G** is the global channel from `ch`.
- Each track has its own channel on the STEP page, per Scene: `off`, `glb` (follow G) or 1 to 16.
  A track with `ch off` has no MIDI channel at all.
- A **voice channel** is a channel that some track uses that is not G. Tracks 6 and 7 both play voice
  6 and move together.

## Notes

### The note map

Each track can have a **root note** (`nte` on the STEP page): the lowest note of its receive range.
The root plays the voice's default pitch, and higher notes in the range transpose up by the
difference.

- Tracks with a root own the notes from their root up to one below the next track's root (or 127).
- Tracks without a root share what is left of the channel. On **G** the notes are spread in a
  **stripe**: note $n$ triggers one of the rootless tracks in rotation at the default pitch. On a
  **voice channel** the notes are split into contiguous **blocks**, one per rootless track, each
  playing chromatically from its block start.
- A note triggers at most one track. The pitch is clamped to 64 semitones above the root.
- `nte off` removes the track from the note map but keeps its channel for CC and NRPN.

\figplaceholder{note_map_example.png}{A worked note map.}{Keyboard strip from note 0 to 127 on channel 10 (G) with tracks 3 and 5 having roots 48 and 60. Show notes 0 to 47 striped across tracks 1, 2, 4, 6 and 7, notes 48 to 59 playing track 3 chromatically, and notes 60 to 127 playing track 5.}

### What a note does

- A note-on is a trigger of the resolved track at the note's velocity and pitch. A muted track
  does not sound, though the note is still consumed.
- A note-off ends a held roll, but a single hit ignores it.
- With a BAR overlay on, a note performs the overlay action (flam or held roll) instead, at the note's
  own pitch, in any mode.
- On the Alkaline voice, velocity 101 to 127 is an accent.
- With record on, notes are recorded with their true arrival time.

## Control changes

### Reserved CCs

\begin{paramtable}
0 & Bank change & CC0 value $v$ on G loads Bank slot $v$. \\
1 & Morph & On G the global Morph (never recorded). On a voice channel that voice's Morph (recorded). \\
2 & BAR overlay & On G only: 0 off, 1 the \texttt{<} overlay, 2 the \texttt{>} overlay. \\
6, 38 & Data entry & NRPN value. CC38 is accepted and ignored. \\
32 & Bank select LSB & Ignored. \\
96, 97 & Data increment, decrement & NRPN step $\pm1$. \\
98, 99 & NRPN number & LSB and MSB. \\
100, 101 & RPN number & Deselects NRPN. \\
120 to 127 & Channel mode & Not used for parameters. \\
\end{paramtable}

All other CCs (3 to 5, 7 to 31, 33 to 95, 102 to 119) can be assigned with **MIDI Learn**.

### NRPN

The **NRPN number is the parameter's canonical 9-bit ID**: send CC99 as ID shifted right by seven
and CC98 as ID and 127, then CC6 for the value. All values are absolute 7-bit values, clamped to
the parameter's own range. Morph amounts are the only exception: values 0 to 126 are doubled and
127 becomes 255.

\input{../latex/midi_tables.tex}

**Instrument-agnostic.** A voice ID addresses whichever parameter sits at that position in the slot's
current Instrument type, so the same NRPN means different things on different types. Unused IDs
are ignored and forwarded.

**Which channels work.** On G every ID works. On a voice channel only that voice's own IDs work
(its parameters, its voice Morph, output, effect send and the timing of its tracks). Effect IDs and
the other Scene IDs are ignored on voice channels.

**A MIDI write is a menu edit.** It has the priority of a menu edit against automation, is fanned
out through the voice edit mask, always writes the Normal value (never a Morph endpoint), is
recorded in LATCH or OVERDUB, and is never echoed back out.

### MIDI Learn

Learn maps any non-reserved CC on one channel kind to one parameter. There are up to 128 entries.

1. **Arm:** press SHIFT, then Play, then Rec, holding the earlier buttons. SHIFT, Play and Rec flash.
   While armed these buttons do none of their usual functions.
2. **Touch a parameter:** turn a knob or edit a cell. It shows `LRN` and its value is not changed.
   Cells with no MIDI address (tempo, settings, step data) edit normally and Learn keeps waiting.
3. **Send a CC.** The entry is stored and `midilrn.cfg` is written at once.
4. **Cancel** with SHIFT, Play or Rec, or by changing mode or page.

A new entry when all 128 are used is refused (`FUL` for one second). One CC can control many
parameters; one parameter has at most one CC. An entry applies only while its Instrument or Effect
type is loaded. Learn has no Morph endpoint: in a Morph view it still maps the parameter itself.
There is no remove in the UI; delete the line from `midilrn.cfg`.

\figplaceholder{midi_learn_gesture.png}{Arming MIDI Learn.}{Three-step diagram of the SHIFT, Play and Rec buttons held in order with the flashing LEDs, then a display showing LRN, then a CC message arriving.}

## Program change and bank change

- **Program change 0 to 15** selects Scenes 1 to 16.
  - On **G** it is a whole Scene change exactly like a PERF SEQ press, with the usual division and
    reset rules.
  - On a **voice channel** every track on that channel switches to that Scene, like holding the
    voice button and pressing SEQ.
- A program change to a Scene that is not loaded, or above 15, is not used and is forwarded.
- **Bank change:** CC0 on G selects Bank slot 0 to 127 and loads it in the background; playback
  continues through Scene 17 (see the Storage chapter). A missing slot fails.
- While a Bank is loading, program change and other non-essential messages are dropped.

## Clock and transport

| Message | Behaviour |
|---|---|
| Clock | Drives the sequencer when `snc` selects its source (24 PPQ). |
| Start | Restarts from the Pattern start. |
| Continue | Resumes from where it stopped or where a song position located it. |
| Stop | Stops and remembers the resume position. |
| Song position | Honoured while stopped. |
| MTC | Starts when eight zero quarter-frames arrive; stops after a 100 ms timeout. |
| MMC | Not supported. |

When slaved to an external clock the LXR does not generate its own; rolls, shuffle and
micro-timing stay even inside each clock period. When the LXR is the clock master it sends clock
and start, continue and stop. Received start, stop and continue are forwarded per `mrt` but not
re-sent.

## What the LXR sends

- **Notes (N):** every trigger the LXR plays, on the track's channel, with the note converted back
  through the note map so recorded output plays back identically. A track that cannot express the
  pitch in its own range sends nothing for it.
- **Realtime (R):** clock and transport while the master.
- **Control (C):** parameters you change by hand (not automation playback), as the learned CC if
  there is one, otherwise as NRPN on G. With G off, voice parameters go as NRPN on the voice channel.
  At most one parameter message is sent every 10 ms so notes and clock are never delayed.
- **Program (P):** a program change on G for a whole-Scene change, program changes on the track
  channels for per-track changes, and CC0 after a successful Bank load. Changes that arrived over
  MIDI are never echoed.

## Jacks

The CLK IN, RST IN and CLK OUT jacks are described in the Overview. With `snc` set to `pls` or
`aut` a clock pulse on CLK IN drives the sequencer.

\verify{Clock pulse resolution on CLK IN and CLK OUT (the firmware defaults to a pre-4 PPQ setting) and any setting that changes it.}
