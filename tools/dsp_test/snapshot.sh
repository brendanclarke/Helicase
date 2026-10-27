#!/bin/sh
# Freeze pre-change DSP sources for the golden harness (S073 Step 0; S085 C0.1).
#
# What:       copies every source file a change set will edit into a frozen
#             directory, preserving the relative path. With no argument it is
#             the S073 snapshot (tools/dsp_test/frozen); with
#             tools/dsp_test/frozen85 it freezes the pre-S085 oscillator,
#             section and engine sources.
# Why:        the old side of an S0 proof must be the exact pre-change source
#             text, independent of commits and later working-tree edits. The
#             S073 snapshot predates S074-S084 and cannot serve S085 (S085 D10).
# Inputs:     the working tree before the first edit; optional DEST.
# Outputs:    DEST/<same relative paths>.
# Accessors:  the implementer, once per change set.
# Affiliates: extract.py, the Makefile targets osc_s0 and armcheck-osc.
set -eu
cd "$(dirname "$0")/../.."
DEST=${1:-tools/dsp_test/frozen}
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
case "$DEST" in
  *frozen85)
    FILES="Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h \
      Core/DSPAudio/wavetable.c Core/DSPAudio/Samples.c \
      Core/DSPAudio/modulationNode.c Core/DSPAudio/mixer.c \
      Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
      Core/DSPAudio/ResonantFilter.c Core/DSPAudio/ResonantFilter.h \
      Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
      Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c" ;;
  *)
    FILES="Core/DSPAudio/ResonantFilter.c Core/DSPAudio/ResonantFilter.h \
      Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
      Core/DSPAudio/distortion.c Core/DSPAudio/distortion.h \
      Core/DSPAudio/Oscillator.c Core/DSPAudio/mixer.c Core/DSPAudio/squareRootLut.c \
      Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
      Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c \
      Core/Hardware/AudioCodecManager.c" ;;
esac
for f in $FILES; do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
