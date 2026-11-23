# Overview

Helicase LXR is a drum machine and synthesizer built on the LXR-02 hardware. It has six
instrument voices (seven sequencer tracks), sixteen Scenes that each carry their own pattern,
sounds and effect, a stereo effects bus, a bus compressor, and an SD card for storing everything.

This chapter describes the physical controls, the connections on the back, and the basic rules of
menu navigation.

\draftnote{This draft was assembled from the project's engineering documents, not from the
finished front panel. Control names follow the firmware's own terms. Check every control
description in this chapter against the real panel and fix the numbered callouts once the
panel figure exists.}

## The Front

\figplaceholder{front_panel.png}{Helicase LXR front panel with numbered control regions.}{Top-down photo or clean vector of the front panel. Number the regions 1 to 11 in the order used in this chapter: display, four knobs, play and record, shift, copy/clear, 16 step buttons, encoder, 8 select buttons, 4 mode buttons, 7 voice buttons, six sliders, BAR buttons. Orange numbered callouts as in image\_templates/panel\_diagram\_template.svg.}

### 1) Display

A 16-character by 2-line OLED. The top line shows parameter names and the bottom line shows their
values. Most pages show four parameters at a time as three-character cells (see
*Menu Navigation* below).

### 2) Knobs

The four endless knobs edit the four parameters shown on the display. Because they are endless
(they have no end stops) the knob never jumps to a position: it changes the stored value from
wherever it was. Turning a knob quickly moves the value faster. For Morph amounts the knobs run
at double angular speed.

### 3) Play and Record buttons

**Play** starts and stops the sequencer.

**Record** controls live recording: tap it to switch recording on (LATCH), hold it for more than
300 ms to overdub while the button is held. See the Live Recording section of the Operation Modes
chapter. **SHIFT + Record** opens the record settings menu.

### 4) Shift button

Hold SHIFT while pressing another button to reach its alternative function. Several menus
(the record settings and the chain editor) are *latched*: they stay open after you release
SHIFT and close with the next SHIFT or MODE press.

\verify{The hardware map lists one button, SW43, as ``SHIFT/BAR1''. Confirm whether SHIFT and the BAR buttons are separate keys on the panel.}

### 5) Copy/Clear button

Used to copy, paste and clear sequencer data and Scene data. **SHIFT + Copy/Clear** enters the
clear function. See *Copy and Clear* in the Sequencer chapter.

### 6) 16 Sequencer buttons (SEQ)

Function depends on the active mode:

- **Voice mode:** set and clear the 16 steps of the visible bar of the selected track.
- **Step mode:** select a step for editing.
- **Performance mode:** select a Scene (with SHIFT: queue a Scene change).
- **Effect page:** edit the 16 steps of the Effect's own sequence.

### 7) Encoder

The main encoder navigates the menu. Turn it to move between parameters and pages; push it to
open the full-size view of a parameter, or to confirm in Load/Save. Hold SHIFT in a single
parameter view to edit its **Morph** value.

### 8) 8 Select buttons

Function depends on the active mode:

- **Voice mode:** choose the synthesis page (oscillator, envelope, filter, and so on).
- **Step mode:** show and edit the eight sub steps of the selected step.
- **Performance mode:** act as the looper divisions while held.
- **Effect page:** choose the effect page; on CrumpBit they are also the eight bit buttons.

### 9) 4 Mode buttons

Switch between operating modes. The alternative function is reached with SHIFT.

- Button 1 --- Voice mode \[SHIFT: Voice Morph view\]
- Button 2 --- Performance mode \[SHIFT: Effect page\]
- Button 3 --- Step mode \[SHIFT: pattern generator (SOM)\]
- Button 4 --- Load/Save \[SHIFT: settings menu\]

\verify{Mode button numbering, the Load/Save versus Settings assignment, and the generator entry are taken from the firmware's mode table; confirm against the panel legends.}

### 10) 7 Voice buttons

Select the active voice and track. Press SHIFT with a voice button to mute or unmute the track.
In Performance mode the function is inverted (see the Sequencer chapter). Tracks 6 and 7 both
play the hi-hat voice (closed and open).

### 11) 6 Sliders

One slider per voice. The slider scales the voice's level according to the voice's *fader mode*
(see the Mixer and Effects chapter): it can control the dry level, the effect send, both, or
crossfade between them.

### 12) BAR buttons

BAR 1 and BAR 2 are the two performance triggers. In Performance mode they switch the
`<` and `>` trigger overlays; while the looper is running they apply a triplet or dotted
modifier to the loop length.

## The Back

\figplaceholder{back_panel.png}{Helicase LXR rear panel connections.}{Rear panel photo or vector with each jack labelled: MAIN L and R, OUT2 L and R, MIDI IN and OUT, USB, CLK IN, RST IN, CLK OUT, SD card slot, DC power. Mark which jacks belong to St1 and St2.}

### Audio outputs

Four 6.3 mm line-level mono outputs, used as two stereo pairs:

- **St1** (MAIN L/R, also the headphone output).
- **St2** (OUT2 L/R).

Each voice and the effect return can be routed to St1, St2, or to a single jack. If a jack is not
plugged in, the firmware moves the signal to a jack that is (see the Mixer and Effects chapter).

\verify{Confirm the physical jack types and the headphone socket location.}

### MIDI

Standard 5-pin DIN MIDI input and output, plus a class-compliant USB MIDI port. Both carry the
same data. See the MIDI chapter.

### Clock and reset jacks

- **CLK IN:** an external clock pulse, rising edge.
- **RST IN:** a reset pulse, rising edge.
- **CLK OUT:** the LXR's own clock when it is the master.

Clock and reset sources should idle low and trigger on the low-to-high transition.

### SD card slot

Insert an SD card to load and save Banks, Scenes, Kits, Instruments, Patterns and samples, and
to update the firmware. The card must be formatted **FAT32**. See the Storage chapter.

### Power

\verify{Power input voltage and polarity for the LXR-02 board are not in the project documents. The original LXR specified 7--9 V DC, centre pin positive, at least 600 mA.}

## Menu Navigation

The upper row of the display shows the three-character short name of each parameter and the
lower row shows its value.

\oled{frq \quad res \quad typ \quad drv}{ 64 \quad\; 0 \quad LP \quad\;\; 0}

- Turn the **encoder** to move the cursor between cells and between pages. A cue character in the
  last column (`>`, `<`, `^` or `+`) shows that more cells or pages follow.
- Turn a **knob** to change the value of the cell above it.
- **Push the encoder** to open the single-parameter view. It shows the full name and the value in
  a larger form, which is useful for fine adjustment.

\oled{\textbf{Filter}}{Resonance \hfill 0}

- Hold **SHIFT** in the single-parameter view to see and edit the **Morph** value of the
  parameter. A parameter that cannot be morphed shows its normal value instead.
- Parameters that have step automation on the visible track or pattern have their name
  **underlined**. The underline reports stored automation, whether or not it would sound.
- Some value cells show `LRN` while MIDI Learn is waiting for a CC (see the MIDI chapter).

\figplaceholder{menu_navigation.png}{Reading the display.}{Annotated close-up of the OLED showing the compact four-cell view, an underlined parameter name, a scroll cue in the last column, and the single-parameter full view below it.}
