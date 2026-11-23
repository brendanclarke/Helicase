# Storage, Load/Save and the SD Card

Everything you keep lives on the SD card. The card must be formatted **FAT32**. Files are called
*files*, not cards.

\draftnote{This chapter summarises the filesystem and Load/Save documents, which are written for
firmware developers. Confirm every step on the device, especially the names of the Load/Save
types and the on-screen prompts.}

## Library types

\begin{paramtable}
Bank & Bank & A folder of up to 16 Scenes. Slot numbers 000 to 999; over MIDI CC0 reaches slots 0 to 127. \\
Scene & Scene & Pattern, Kit, Effect, mix, timing and chain settings for one Scene. \\
Kit & Kit & Six Instruments. \\
Instrument & Instrument & One voice patch, filed by type (Drum, Snare, Cymbal, HiHat, Alkaline). \\
Pattern & Pattern & The Pattern of a Scene (7 tracks of 128 steps), saved on its own. \\
Samples & Samples & Not a library: installs samples and wavetables into flash memory. \\
\end{paramtable}

The Effect is stored as part of its Scene. There is currently no separate Effect library.

\verify{The Load/Save page may also offer a Settings or Globals type; the settings file is described below.}

## Load and Save

The Load/Save mode button opens the Load/Save pages (a second press toggles between Load and Save).

\figplaceholder{load_save_page.png}{The Load page.}{Display close-ups: top-level Load with the type cell, slot number and name; then a nested Instrument load showing the Instrument type and slot; then the Save page with the name editor and the OK cell. Mark the busy indicator with three dots and the error overlay W followed by a code.}

- The **type** cell selects Bank, Scene, Kit, Instrument, Pattern or Samples. **Knob 1** steps
  through the types directly. For Instrument, the types offered follow the selected track's
  Instrument types.
- The **slot** number and **name** show what is selected. Turning the encoder previews the slot; the
  name appears after the card index has been read, and stays blank while the card is busy.
- Moving to a different type while an index is still being read cancels that read; only the
  settled type is read.
- Writes, name updates and loading do not allow the controls to be used until they finish, but a
  watchdog ends every operation that makes no progress for **12 seconds**: the display then shows an
  error code beginning with `W`.
- Commands are confirmed by moving to **OK** and pushing the encoder; **OW** appears when a Save
  would overwrite an existing slot.

\verify{The exact wording of the OK, OW and overwrite prompts.}

### Saving

1. Choose the type and slot. A slot number in use is overwritten after confirmation; a free slot
   creates a new file.
2. Edit the name (see below).
3. Move to OK and push the encoder.

**Names.** A name stem is at most **eight characters** long. A blank name is stored as `none.drm` for
Instruments (the extension follows the type) and as `none.fx` for the Effect. An empty file stem
becomes `inst`.

### Loading a Bank

A Bank load can run in the background while the sequencer plays. The music continues from Scene 17
(a hidden copy of the Scene that was playing); the panel is available again once the load reaches
the point where playback no longer depends on the files being read. The Load page stays open until
the load ends if the sequencer was stopped. A Bank loaded over MIDI always replaces all 16 Scenes.

### Loading a Scene or Kit, Instrument and Pattern

- **Scene** load replaces the Scene's Pattern, Kit, Effect, mix settings and chain.
- **Kit** load replaces all six Instruments; the Effect is not touched.
- **Instrument** load replaces one slot. The previous state of the slot is kept in memory so the
  load can be reversed.
- **Pattern** load can fan out to the Scenes in the edit mask.

### Morph targets

Morph values are stored inside Scenes, Kits and Instruments: Kits and Instruments carry their own
Morph endpoints (`[morph]`), and a Scene stores the Morph endpoints of everything it owns. There is
no separate "Morph sound" file type.

## Samples

**Load > Samples** walks `/samples` and `/wavetables` on the card, erases the sample area of
flash memory, installs the files and writes a new catalogue.

- Files must be mono, 16-bit, 44.1 kHz PCM WAV.
- Wavetables: one sub-folder per wavetable inside `/wavetables`.
- The sample area holds five 256 KB sectors (about 1.25 MB). The catalogue sits at the top.
- Audio is stopped during the write and restarted afterwards.

\attention{Samples are held in flash memory, not on the card. A device whose catalogue was written by
an older firmware shows STALE and must reinstall its samples before they can be used. Reinstall at
low volume before rolling the firmware back to an older version, because older firmware plays
program code as audio.}

## AutoSave

The machine saves its working state in the background: scalar settings, each Scene's Pattern,
names and the Effect. AutoSave never makes you wait:

- It pauses during Load/Save, copy/clear and while a Bank load is running.
- It restarts from the beginning after you leave the Load/Save page if work is still owed.
- At start-up the firmware first **settles** any unfinished AutoSave work (this is why start-up can
  take a few seconds) before audio starts.
- Settings are written to `settings.cfg` and the MIDI Learn table to `midilrn.cfg`.

AutoSave writes alternating hidden files so that an interrupted write can never destroy the last
good copy.

## SD card layout

\figplaceholder{sd_card_layout.png}{SD card folder layout.}{Tree view of the card root: Bank, Scene, Kit, Pattern, Instrument with a sub-folder per type (for example Alkaline containing init.alk), samples, wavetables, and the root files settings.cfg and midilrn.cfg. Mark hidden files in grey.}

\begin{paramtable}
/Bank, /Scene, /Kit, /Pattern & Library folders & Numbered folders or files with a three-digit slot and a name. \\
/Instrument & Instrument library & One sub-folder per Instrument type. \\
/samples, /wavetables & Sample sources & Used by Load > Samples. \\
settings.cfg & Settings & Global settings, read at boot. \\
midilrn.cfg & MIDI Learn & One readable line per learned CC. \\
\end{paramtable}

Hidden index and name files (`.hcindex`, `.hcnames` and the AutoSave records) are managed by the
firmware.

**Editing the card on a computer** is fine, but changes are seen only after a power cycle; the
firmware never remounts the card while running. Do not delete the hidden files unless told to.

### Preparing an older card

A card prepared with an older firmware may have incompatible hidden files. Delete these from the
card root once and power-cycle: `.hcnames`, `.hcnamtmp`, `.hcprms1`, `.hcprms2`.

### Start-up repair

At start-up the firmware repairs any directories whose end-of-directory marker was lost after
deleting files. This happens once; subsequent boots skip clean directories.

## Error codes

\begin{paramtable}
W + code & Load/Save watchdog & An operation made no progress for 12 seconds. The code names the operation and the phase it was in. Note it and power-cycle. \\
Img BAD s:... & Flash image check & A stamped checksum in a flash sector does not match. The device waits for BAR1. See the Firmware Update chapter. \\
STALE & Sample catalogue & The sample area was written by an older firmware. Reinstall with Load > Samples. \\
\end{paramtable}

\verify{The firmware's own wording and layout for each message, and whether the flash image check stays in release firmware (the project lists this as an undecided item).}
