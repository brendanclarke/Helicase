#!/bin/sh
# Freeze the pre-S085 P1 sources (S085 P1 step 1).
#
# What:       copies the files S085 P1 edits into tools/dsp_test/frozen85/,
#             preserving their relative paths.
# Why:        the identity gate (oscmod_identity) needs the exact pre-change
#             text. frozen/ holds the pre-S073 sources and must not be
#             refrozen (snapshot.sh refuses).
# Inputs:     the working tree at 1ea234ea, before any S085 P1 source edit.
#             Run from anywhere; it cds to the repository root.
# Outputs:    tools/dsp_test/frozen85/<same relative paths>.
# Accessors:  the implementer, once (S085_P1_OSC_MOD_IMPLEMENTATION.md §6
#             step 1).
# Affiliates: Makefile oscmod_identity, elf_osc_baseline (writes
#             frozen85/elf_osc.txt).
set -eu
cd "$(dirname "$0")/../.."
DEST=tools/dsp_test/frozen85
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
for f in \
  Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h \
  Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
  Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Drum/DrumVoice.h \
  Core/DSP/Instruments/Drum/DrumParameters.c Core/Menu/MenuText.h
do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
