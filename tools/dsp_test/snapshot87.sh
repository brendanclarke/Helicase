#!/bin/sh
# Freeze the pre-S087 sources (S087 step 0).
#
# What:       copies every file S087 edits into tools/dsp_test/frozen87/,
#             preserving relative paths.
# Why:        the S087 identity gates (osc87_identity, engine87_identity)
#             compare the edited files with their exact pre-change text;
#             frozen/, frozen85/ and frozen86/ belong to earlier sessions and
#             must not be refrozen.
# Inputs:     the working tree at 0e2d61dd, before any S087 source edit.
#             Run from anywhere; it cds to the repository root.
# Outputs:    tools/dsp_test/frozen87/<same relative paths>.
# Accessors:  the implementer, once.
# Affiliates: Makefile targets osc87_identity, engine87_identity,
#             elf87_baseline, elf87_mixer_snapshot.
set -eu
cd "$(dirname "$0")/../.."
DEST=tools/dsp_test/frozen87
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
for f in \
  Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h \
  Core/DSPAudio/modulationNode.c Core/DSPAudio/modulationNode.h \
  Core/DSPAudio/mixer.c \
  Core/MIDI/MidiVoiceControl.c \
  Core/Sequencer/sequencer.c Core/Sequencer/sequencer.h \
  main.c \
  Core/SampleRom/SampleMemory.c Core/SampleRom/SampleMemory.h \
  Core/SampleRom/sampleFlash.c Core/SampleRom/sampleFlash.h \
  Core/Hardware/SD/filesystem.c Core/Hardware/SD/filesystem.h \
  Core/Hardware/SD/storageTypes.c Core/Hardware/SD/storageTypes.h \
  Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Drum/DrumVoice.h \
  Core/DSP/Instruments/Drum/DrumParameters.c Core/DSP/Instruments/Drum/DrumParameters.h \
  Core/DSP/Instruments/Snare/Snare.c Core/DSP/Instruments/Snare/Snare.h \
  Core/DSP/Instruments/Snare/SnareParameters.c Core/DSP/Instruments/Snare/SnareParameters.h \
  Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/Cymbal/CymbalVoice.h \
  Core/DSP/Instruments/Cymbal/CymbalParameters.c Core/DSP/Instruments/Cymbal/CymbalParameters.h \
  Core/DSP/Instruments/HiHat/HiHat.c Core/DSP/Instruments/HiHat/HiHat.h \
  Core/DSP/Instruments/HiHat/HiHatParameters.c Core/DSP/Instruments/HiHat/HiHatParameters.h \
  Core/DSP/Instruments/InstrumentManager.c Core/DSP/Instruments/InstrumentManager.h \
  Core/Bank/Scene/Preset/presetManager.c \
  Core/Menu/menu.c Core/Menu/MenuText.h \
  config.h
do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
