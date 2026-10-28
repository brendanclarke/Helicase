#!/bin/sh
# Freeze the pre-S086 sources (S086 step 0).
#
# What:       copies the files S086 S1 edits into tools/dsp_test/frozen86/,
#             preserving their relative paths.
# Why:        the identity gate (osc86_identity) needs the exact pre-change
#             text. frozen/ (pre-S073) and frozen85/ (pre-S085 P1) must not
#             be refrozen.
# Inputs:     the working tree at e56bb957 (the S085 close-out), before any
#             S086 source edit. Run from anywhere; it cds to the repository
#             root.
# Outputs:    tools/dsp_test/frozen86/<same relative paths>.
# Accessors:  the implementer, once (086_SESSION_HANDOFF_LOG.md §2).
# Affiliates: Makefile osc86_identity, elf_osc86_baseline (writes
#             frozen86/elf_osc.txt).
set -eu
cd "$(dirname "$0")/../.."
DEST=tools/dsp_test/frozen86
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
for f in \
  Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h \
  Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
  Core/SampleRom/SampleMemory.c Core/SampleRom/SampleMemory.h \
  Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
  Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c \
  Core/Menu/MenuText.h
do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
