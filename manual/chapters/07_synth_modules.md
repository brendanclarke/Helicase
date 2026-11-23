# Synth Modules

A closer look at the synthesis modules used across the voices.

## Oscillator

The main oscillator of a voice has an **oscillator type** (`typ`) that decides how the `wav`
selection is read.

| `typ` | Name | Plays |
|---|---|---|
| `wav` | Stock waves | `Sin`, `Tri`, `Saw`, `Rec` (rectangle), `Noi` (noise). |
| `smp` | One-shot sample | A sample from the installed catalogue, played once from the trigger. |
| `slp` | Looped sample | A sample played in its loop region. |
| `wtb` | Wavetable | A wavetable with frame blending; `pos` selects the frame. |
| `pwm` | Pulse-width modulation | Sin, Tri, Saw or Rec with a variable pulse width set by `pos`. |

Under `smp`, `slp` and `wtb`, `wav` values from the sixth entry upward select installed
catalogue entries, named `s01` onward (samples), `l01` onward (looped samples) and `w01` onward
(wavetables). Which kind of entry plays is decided by `typ`.

\verify{The built-in crash sample no longer exists in flash. A file \texttt{02\_crash.wav} on the card replaces it. Confirm the shipped factory sample set.}

### Position (`pos`)

`pos` is the fourth cell of the first oscillator screen on every voice. Its meaning follows `typ`:

| Type | Meaning of `pos` (0 to 127) |
|---|---|
| `wav` | Start phase: `pos`/128 of a cycle at each trigger. Noise ignores it. A move while the note plays glides within one block. |
| `smp` | Start the sample at `pos`/128 of its length. |
| `slp` | Start at `pos`/128 of the sample, wrapped into the loop. |
| `wtb` | Frame position in the wavetable. |
| `pwm` | Pulse width: 0 is 50 percent (the unmodified wave), 127 is the narrowest pulse. |

### Run mode (`run`)

\begin{paramtable}
rst & Reset      & Each retrigger resets the carrier phase. This is the traditional behaviour. \\
fre & Free       & The carrier phase is never reset. Modulators slew, the amplitude envelope is gain-matched and the filter keeps ringing. \\
itp & Interpolate & The phase is reset and the old waveform is crossfaded out over 132 samples. A fully decayed \texttt{itp} equals \texttt{rst}. \\
\end{paramtable}

### Wavetable interpolation

When a wave-modulated oscillator blends between neighbouring waveforms or wavetable frames, the
firmware interpolates for at most two oscillators at a time; additional ones step. Slowly
modulated oscillators are interpolated; fast ones may step.

## Samples and Wavetables

Samples and wavetables are installed from the SD card into the flash memory with
**Load > Samples** (see the Storage chapter).

- Accepted files are **mono, 16-bit, 44.1 kHz PCM WAV**.
- Put samples in `/samples`, and wavetables in `/wavetables`, one sub-folder per wavetable.
- Names are sorted in natural order (`2` before `10`). On the display a long name shows its first
  four and last four characters, with any numeric prefix removed.
- Every wavetable frame is resampled to 1,024 points at install time. 8-bit files are not
  installed.
- Samples are kept through power-down. Installing erases and rewrites the sample area.

\attention{Installing samples erases the sample area first. A card prepared with an older firmware
shows the catalogue as stale and must be reinstalled before the samples can be used.}

\figplaceholder{sample_folders.png}{Folder layout for samples and wavetables.}{Tree view of the SD card root showing samples, wavetables with two example wavetable folders, and which names appear on the display as s01, l01 and w01.}

## Filter

The voice filter is a two-pole (12 dB) state-variable filter realised as a non-linear
zero-delay-feedback model. It shapes the harmonic content: a hi-hat needs a highpass to remove low
frequencies, a clap benefits from a bandpass.

Three parameters control it: frequency, resonance and drive.

- **Frequency** sets the operating point.
- **Resonance** controls the feedback path and boosts frequencies around the cutoff.
- **Drive** controls how hard the filter is driven. At 0 it is clean and nearly linear; higher values
  engage a soft clipper and the slew limit of the integrators, so peaks distort first and then the
  whole signal.

### Filter types

\figplaceholder{filter_lowpass.png}{Lowpass filter response.}{Magnitude response with three resonance settings: low in red, medium in green, high in blue. Same cutoff in every plot, marked by a vertical dashed line. Use image\_templates/filter\_plot\_template.svg.}

\figplaceholder{filter_highpass.png}{Highpass filter response.}{As the lowpass plot, for the highpass type.}

\figplaceholder{filter_bandpass.png}{Bandpass filter response.}{As the lowpass plot, for the bandpass type.}

\figplaceholder{filter_unitgain_bandpass.png}{Unity-gain bandpass response.}{As the lowpass plot; gain is always 0 dB at the peak and resonance controls only the width of the passband.}

\figplaceholder{filter_notch.png}{Notch filter response.}{As the lowpass plot, for the notch type; resonance controls the width of the stopband.}

\figplaceholder{filter_peak.png}{Peak filter response.}{As the lowpass plot, for the peak type; resonance controls the amount of boost.}

- **Lowpass** removes high frequencies.
- **Highpass** removes low frequencies.
- **Bandpass** removes both sides of the cutoff.
- **Unity-gain bandpass** is a bandpass scaled to 0 dB; resonance controls the width.
- **Notch** removes frequencies around the cutoff.
- **Peak** boosts frequencies around the cutoff.
- **Naive two-pole low-pass** is a cheaper low-pass kept for kick transients.

The Alkaline voice uses a different filter, a **diode ladder** with its own resonance and drive
laws; see the Instruments chapter.

## Envelopes

Whenever a voice is triggered, its envelopes restart. The signal rises at the speed set by attack,
reaches its maximum and falls at the speed set by decay.

\figplaceholder{envelope_linear.png}{Linear envelope curve.}{Amplitude against time showing attack and decay with slope 63 (a straight-line decay). Use image\_templates/envelope\_template.svg.}

- **Attack and decay** control the duration of each stage. Higher values mean longer times. With
  attack at zero the envelope starts directly in the decay stage.
- **Slope** controls the curve. 63 is linear, lower values are exponential and higher values
  logarithmic.

\figplaceholder{envelope_exponential.png}{Low slope: exponential decay.}{As the linear envelope with a low slope value.}

\figplaceholder{envelope_logarithmic.png}{High slope: logarithmic decay.}{As the linear envelope with a high slope value.}

- **Repeat** simulates clap-like attacks. With a repeat count above zero, the attack stage is
  replaced by a repeat stage in which the envelope retriggers rapidly; attack sets the duration of
  the whole repeat phase.

\figplaceholder{envelope_repeat.png}{Envelope in repeat mode.}{Envelope with repeat count 3, repeat phase in blue and decay phase in red.}

Each voice also has a **pitch envelope** (a one-shot decay) that sweeps the oscillator pitch. On
the Drum voice it also scales the depth of the FM, sync and ring modes.

## Transient Generator

The transient generator shapes the first milliseconds of a sound.

- **Snappy (`Snp`):** an additional fast pitch envelope on the oscillator produces a snap or click.
  `vol` is the depth and `frq` the decay.
- **Offset (`Ofs`):** adjusts the start phase of the oscillators. At volume 0 they start at a zero
  crossing; at maximum volume the wave starts at its peak, giving a loud pop.
- **Click samples:** twelve built-in clicks, and then every installed sample (`s01`...) as a
  one-shot. `frq` sets the playback speed (1.0 is the file's own rate); `vol` is applied on top.

## Wavefolder

With the voice chain set to **WFd** the transient stage is replaced by a **wavefolder** that sits
before the filter. The `wav` cell selects the fold type, `vol` its depth and `frq` a secondary
control that depends on the type.

\begin{paramtable}
Tri & Triangle fold    & Classic triangle fold. \\
TrD & Triangle, decaying & Fold depth decays over time (5 ms to 2 s). \\
TrI & Triangle, intense & Deeper fold range. \\
Chb & Chebyshev        & Polynomial fold with unity small-signal gain. \\
Fd2 & Two-stage fold   & Two cascaded folds; \texttt{frq} sets the second stage ratio. \\
Sin & Sine fold        & Smooth sine folding. \\
SnD & Sine, decaying   & Sine fold with decaying depth. \\
Asn & Sine with accent & Sine fold with an even-harmonic accent. \\
\end{paramtable}

\verify{Fold type names and order on screen, and the exact meaning of frq for each type.}

At depth 0 the triangle folds do nothing.

## Distortion and Sample-Rate Reduction

- **Distortion** is a soft saturator that is always in the signal path, with drive 0 acting as a
  nearly clean setting.
- **Sample-rate reduction** (`srt` or `sr`) is sample-and-hold decimation. It is applied only to
  the voices that have it in their chain (Drum, Snare, Cymbal and HiHat). Alkaline does not have it.
- There is **no global sample-rate setting**; the former global control was replaced by the Effect
  Morph (`fxm`) cell on the PERF page.

## LFO

See the LFO page in the Instruments chapter. Further notes:

- **Rate table:** rate 0 stops the LFO, 1 is a 100 second period, and the upper half of the range
  moves in semitone steps so that LFO rates relate musically.
- **Synced rates** run from 4/1 down to 1/32 and follow the sequencer tempo (internal or external).
- An LFO can modulate any modulatable parameter of any voice, the Scene's Morph amounts
  (`scn`) and the Effect parameters (`fx`). It modulates *around* the value set by the menu, Morph
  or automation, and the base is restored when the target is cleared.

## Velocity Modulation

Each voice routes note velocity to one destination parameter with an amount. Velocity cannot
modulate Effect parameters. On Alkaline, velocity is the accent.
