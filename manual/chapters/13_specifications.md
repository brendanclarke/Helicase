# Specifications

\draftnote{Figures are taken from the hardware and DSP documents. Items marked VERIFY are not in
the project documents.}

## Overview

| | |
|---|---|
| Instrument voices | 6 (seven sequencer tracks; tracks 6 and 7 share voice 6) |
| Instrument types | Drum, Snare, Cymbal, HiHat, Alkaline |
| Scenes per Bank | 16 (plus the hidden background Scene 17) |
| Effect | 1 per Scene (off, StFilter, CrumpBit) plus a bus compressor |
| Sliders | 6 |
| Knobs | 4 endless knobs and 1 encoder with push switch |
| Display | 16 $\times$ 2 OLED |
| Audio jacks | 4 outputs, used as 2 stereo pairs (St1 with headphones, St2) |
| MIDI | DIN in and out, USB MIDI |
| Clock | CLK IN, RST IN, CLK OUT |
| Memory | 1 SD card slot (FAT32) |

\verify{Button and LED counts, slider count confirmation, headphone socket, power input.}

## Sequencer

| | |
|---|---|
| Tracks per Pattern | 7 |
| Steps per track | 128 |
| Patterns | One per Scene, 16 resident |
| Per-step data | Trigger, note, velocity, condition, roll, timing, rhythm, fill |
| Automation per step | Several entries (maximum 63 per step), each with a unique target |
| Internal resolution | 96 PPQ |
| Roll rates | 1/8, 8tr, 16, 16t, 32, 32t, 64, 64t, 128 |
| Step scale | 128-position continuous scale, 1/64 to 1/2 |
| External sync | MIDI clock (USB or DIN), clock pulse, auto |
| Live editing | All actions possible without stopping the sequencer |

## Audio

| | |
|---|---|
| Sample rate | 44.108 kHz |
| Output | Two stereo 24-bit I2S converters |
| Internal processing | 24-bit signed paths, 32-sample processing blocks |
| Output clipping | Digital full scale; the bus compressor's soft knee prevents hard clipping |

## Hardware

| | |
|---|---|
| Processor | STM32F765 (ARM Cortex-M7, 216 MHz, hardware floating point, caches) |
| Audio converters | 2 $\times$ CS4344 stereo DACs |
| Flash | 2 MB: bootloader (32 KB), application (736 KB), sample storage (1.25 MB) |
| Storage | SD card (SDHC works), FAT32 |
| Interfaces | DIN MIDI, USB MIDI (isolated), clock and reset jacks |

## Power

\verify{Power requirement of the LXR-02 board; the original LXR was 7 to 9 V DC, centre pin positive, at least 600 mA.}
