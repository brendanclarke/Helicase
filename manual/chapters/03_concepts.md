# How the Machine Is Organised

Helicase LXR stores its work in a hierarchy. Understanding it makes Load/Save, Morph and live
performance much easier to follow.

\figplaceholder{hierarchy_diagram.png}{The Bank, Scene, Kit and Instrument hierarchy.}{Tree diagram. Bank at the top, containing up to 16 Scenes. Each Scene contains: a Pattern (7 tracks of 128 steps), a Kit made of 6 Instruments (one per voice slot), one Effect, mix and MIDI settings, and chain settings. Show Scene 17 as a dashed box labelled background Scene. Use orange for things stored on the SD card as libraries and grey for things that live inside a Scene.}

## Bank

A **Bank** is a complete set of up to **16 Scenes**. One Bank is resident (loaded in memory) at a
time. Loading a Bank replaces the Scenes; saving a Bank writes every Scene back as a folder on the
SD card. Banks can be changed from the Load page or over MIDI (CC0 on the global channel).

## Scene

A **Scene** is everything needed to play one section of a song. Each of the 16 Scenes holds:

- a **Pattern**: 7 tracks of 128 steps, with all step data and step automation;
- a **Kit**: six Instruments, one per voice slot;
- one **Effect** (off, StFilter or CrumpBit) with its parameters and its 16-step effect sequence;
- **mix settings** per voice (output route, effect send, fader mode, Morph endpoints);
- the **bus compressor** settings;
- **per-track timing** (length, step scale, shuffle, run mode) and per-track **MIDI channel
  and note**;
- the Scene's **chain** settings (what plays next).

Switching Scene in Performance mode changes all of these together. Because the Scene owns the
sounds as well as the notes, changing Scene can change the whole sound of the machine.

### Active Scene and played Scene

The **active Scene** is the Scene you are looking at and editing: it decides which values the Voice
and Effect pages show and which Scene your edits are written to. The **played Scene** of a track
is the Scene whose Pattern that track is currently playing. Normally they are the same, but with
*per-track Scene playback* (see the Operation Modes chapter) each track can play a different
Scene.

### Scene 17

While a Bank is loading in the background, the music keeps playing from a hidden copy called
**Scene 17**. It is released when you change Scene.

## Kit and Instrument

An **Instrument** is the patch of one voice: oscillators, filter, envelopes, LFO and so on. There
are several Instrument **types** (Drum, Snare, Cymbal, HiHat and Alkaline), each with its own
parameter set. A **Kit** is a set of six Instruments. Instruments and Kits can be saved and
loaded individually.

Slots have rules:

- **Basic** types may be used in any number of slots.
- **Advanced** types are limited to two per Kit. No current type is Advanced.
- The **HiHat** is a *choke* type: in slot 6 it serves both track 6 (closed) and track 7 (open),
  and the closed hat chokes the open hat.

## Effect

Each Scene owns exactly one **Effect** on a shared effects bus. Voices feed it through their own
send levels. Loading a Kit or an Instrument never changes the Effect; loading a Scene or a Bank
does.

## Pattern, track and step

A **Pattern** belongs to a Scene. It has **7 tracks** of **128 steps**. The 128 steps of a track
are shown as eight bars of 16 steps on the sequencer buttons. A step carries a trigger and
optional *specials*: note, velocity, condition, roll, timing, rhythm and fill, plus step
**automation** entries that set parameters on that step.

## Morph

Most Instrument, Effect and Scene parameters store **two values**: a Normal value and a Morph
value. A **Morph amount** (0 to 255) blends between them. There is a Morph amount per voice, one
for the Effect, and a **global Morph** (`mrp`) that sets all of them at once. Morph is the main
tool for evolving sounds live. See *Morph* in the Operation Modes chapter.

## Edit mask

Edits fan out to every Scene in the **voice edit mask**, so several Scenes with the same
structure can be edited together. Pattern data is never fanned out.

## Automatic saving

Edits are written to the SD card automatically in the background (**AutoSave**). You do not have
to press Save to keep your work in the current Bank; Save is for creating named library files.
See the Storage chapter.

\draftnote{The terms ``Kit'' and ``Instrument'' follow the firmware. The names of the Load/Save
types and the exact way the voice edit mask is chosen from the panel need confirming on the
device.}
