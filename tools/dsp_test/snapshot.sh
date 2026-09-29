#!/bin/sh
# Freeze the pre-change DSP sources for the S073 golden harness (Step 0).
#
# What:       copies every source file the CPU steps will edit into
#             tools/dsp_test/frozen/, preserving the relative path.
# Why:        the old side must remain the exact pre-change source text,
#             independent of commits and later working-tree edits.
# Inputs:     the working tree before Step 1. Run once from the repository root.
# Outputs:    tools/dsp_test/frozen/<same relative paths>.
# Accessors:  the implementer or user, once, at Step 0.
# Affiliates: extract.py and the Makefile harness targets.
set -eu
cd "$(dirname "$0")/../.."
DEST=tools/dsp_test/frozen
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
for f in \
  Core/DSPAudio/ResonantFilter.c Core/DSPAudio/ResonantFilter.h \
  Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
  Core/DSPAudio/distortion.c Core/DSPAudio/distortion.h \
  Core/DSPAudio/Oscillator.c Core/DSPAudio/mixer.c Core/DSPAudio/squareRootLut.c \
  Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
  Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c \
  Core/Hardware/AudioCodecManager.c
do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
