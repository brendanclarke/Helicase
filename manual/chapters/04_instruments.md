# Instruments

Each of the six voice slots holds one Instrument. The type of Instrument in a slot can be changed
by loading a different Instrument (see the Storage chapter). Five types are available.

\begin{paramtable}
Drum     & Drum voice     & Pitched oscillator with FM, ring, sync and mix modes. Kicks, toms, cowbells, bass. \\
Snare    & Snare voice    & Pitched oscillator plus a filtered noise source. Snares and claps. \\
Cymbal   & Cymbal voice   & Three-operator FM. Metallic and noisy sounds. \\
HiHat    & HiHat voice    & As Cymbal with two decay times. A \emph{choke} voice for tracks 6 and 7. \\
Alkaline & Alkaline voice & Emulation of the bass-line synthesizer: saw, square, triangle or pulse oscillator into a diode-ladder filter, with accent and slide. \\
\end{paramtable}

All five are *Basic* instruments: any of them can be placed in any slot, and no type is *Advanced*.

\draftnote{The Display column in the tables below uses the three-character tokens the firmware
shows. Where this draft could not confirm the token or the page it sits on, the row is flagged.
Confirm every table against the device before release.}

## Drum Voice

The Drum voice suits kicks, toms, cowbells and other pitched percussion.

\figplaceholder{voice_drum_signal_flow.png}{Drum voice signal flow.}{Block diagram, left to right: main oscillator and FM oscillator combined by the mod mode (fm, mix, snc or rng); transient or wavefolder stage; filter; amplitude envelope and velocity; distortion; sample-rate reduction; to mixer. Pitch envelope drawn modulating the main oscillator pitch and the FM depth. Use image\_templates/signal\_flow\_template.svg.}

### Voice chain: transient or wavefolder

Every voice except Alkaline has a **chain** selector (`chn`) on the transient page:

- **T+D** (default): the transient generator is mixed in before the filter, and the filter has a
  drive stage.
- **WFd**: a **wavefolder** replaces the transient generator. The `wav`, `vol` and `frq` cells
  of the transient page then control the fold type, depth and a type-specific secondary
  control. See the Synth Modules chapter.

### Oscillator page (OSC)

\begin{paramtable}
coa & Coarse tune    & Coarse tuning of the main oscillator in semitones. \\
fin & Fine tune      & Fine tuning, $\pm$50 cents. \\
wav & Waveform       & Stock wave (\texttt{Sin Tri Saw Rec Noi}) or an installed sample, loop or wavetable entry, depending on \texttt{typ}. \\
pos & Position       & Meaning depends on the oscillator type; see the Synth Modules chapter. 0 to 127. \\
typ & Oscillator type & \texttt{wav} stock waves, \texttt{smp} one-shot sample, \texttt{slp} looped sample, \texttt{wtb} wavetable, \texttt{pwm} pulse-width modulation. \\
run & Run mode       & What a retrigger does to the carrier: \texttt{rst} resets the phase, \texttt{fre} never resets it, \texttt{itp} resets it and crossfades from the old waveform. \\
\end{paramtable}

\verify{Order of the six OSC cells across the two screens, and whether typ and run sit on the first or second screen.}

### Amplitude envelope page (AEG)

\begin{paramtable}
att & Attack  & Rise time. 0 starts directly in the decay stage. \\
dec & Decay   & Fall time. \\
slp & Slope   & Curve shape from exponential through linear to logarithmic. \\
rpt & Repeat  & Number of attack retriggers (clap-like attack). \\
\end{paramtable}

\verify{Whether the Drum voice shows rpt; the earlier LXR showed it only on Snare and Cymbal.}

### Modulation page (MOD)

\begin{paramtable}
dec & Pitch envelope decay  & Fall time of the pitch envelope. \\
slp & Pitch envelope slope  & Curve shape of the pitch envelope. \\
mod & Pitch envelope amount & Depth of the pitch sweep. In \texttt{fm}, \texttt{snc} and \texttt{rng} modes the same envelope also scales the FM, sync or ring depth. \\
dst & Velocity destination  & Route note velocity to any parameter of the voice. \\
amt & Velocity amount       & Amount of the velocity modulation. \\
vol & Velocity to volume    & On or off. \\
\end{paramtable}

### FM page (FM)

The FM oscillator (modulator) acts on the main oscillator (carrier) according to the `mod` mode.

\begin{paramtable}
amt & Amount         & Depth of the effect chosen by \texttt{mod}. \\
frq & Frequency      & Coarse tuning of the modulator in semitones. \\
wav & Waveform       & Waveform of the modulator: stock waves, one-shot samples (\texttt{s01}...), looped samples (\texttt{l01}...) or wavetables (\texttt{w01}...). \\
mod & Mode           & \texttt{fm} frequency modulation; \texttt{mix} crossfade of the two oscillators; \texttt{snc} the carrier is hard-synced to a clock at the modulator frequency; \texttt{rng} ring modulation. \\
\end{paramtable}

In `fm`, `snc` and `rng` modes the effective depth follows the square of `amt` times the pitch
envelope, so a short pitch decay gives a short burst of FM, sync or ring modulation at the start
of the note.

### Transient page (Click)

\begin{paramtable}
wav & Transient / fold type & Transient sample (\texttt{Snp}, \texttt{Ofs}, built-in clicks, then installed samples \texttt{s01}...) or, with \texttt{chn} = \texttt{WFd}, the fold type. \\
vol & Volume / fold depth   & Level of the transient or depth of the fold. \\
frq & Frequency             & Pitch or speed of the transient, or the secondary fold control. \\
chn & Chain                 & \texttt{T+D} or \texttt{WFd}. \\
\end{paramtable}

### Filter page (FIL)

\begin{paramtable}
frq & Frequency & Cutoff frequency. \\
res & Resonance & Filter resonance. \\
typ & Type      & \texttt{LP}, \texttt{HP}, \texttt{BP}, \texttt{UBP} (unity-gain bandpass), \texttt{Nch} (notch), \texttt{Pek} (peak), \texttt{LP2} (cheaper low-pass, suited to kick transients). \\
drv & Drive     & Input drive of the filter. \\
\end{paramtable}

\verify{Exact on-screen names of the seven filter types.}

### LFO page (LFO)

Each voice has one LFO with two destinations that share its phase, waveform, rate and sync.

\begin{paramtable}
frq & Rate        & Manual LFO rate. 0 stops the LFO; 1 is a 100 second period; the upper half of the range moves in semitone steps. Ignored while sync is on. \\
snc & Sync        & Locks the rate to the sequencer tempo (note divisions from 4/1 to 1/32). \\
amt & Amount      & Depth for destination 1. \\
wav & Waveform    & Sine, triangle, saw up, saw down, square, random, exponential up, exponential down, and the one-shot variants. \\
rtg & Retrigger   & \texttt{off}, tracks \texttt{v1} to \texttt{v6}, or \texttt{scn}: keep the LFO phase across a Scene change. \\
off & Phase       & Start phase on retrigger (also a pre-trigger delay for one-shot waves). \\
pol & Polarity    & Negative, positive or bipolar. \\
voi & Destination voice & Target voice, \texttt{scn} for Scene targets (voice Morph amounts and so on), or \texttt{fx} for the Effect. \\
dst & Destination parameter & Parameter to modulate. \\
\end{paramtable}

The second destination has its own amount, voice and parameter.
\verify{Exact tokens and screen order of the second destination cells.}

### Mix page (Mix)

\begin{paramtable}
vol & Volume  & Voice level, applied after the instrument's distortion and sample-rate reduction. \\
pan & Pan     & Stereo position. Displays $-63$ to $+64$ with 0 at centre. Not applied when the voice is routed to a single output jack. \\
drv & Drive   & Distortion amount. \\
srt & Sample-rate reduction & Sample-and-hold decimation of the voice. \\
\end{paramtable}

\verify{Tokens for drive and sample-rate reduction on the Mix page; whether output route, effect send and fader mode are shown here or only on the Scene mix screen (SHIFT+TRACK from the Effect page).}

## Snare Voice

For snares and claps. A noise source and a pitched oscillator are mixed; **only the noise is
filtered**. There is no FM oscillator.

\figplaceholder{voice_snare_signal_flow.png}{Snare voice signal flow.}{Noise oscillator into the filter; tonal oscillator and transient (or wavefolder) added after the filter; mix control between noise and tone; amplitude envelope; distortion; sample-rate reduction.}

The OSC page has `coa fin wav pos` plus **`noi`** (noise frequency) and **`mix`** (balance of
oscillator and noise). The AEG, MOD, Click, FIL, LFO and Mix pages are as for the Drum voice. The
FM page is empty.

## Cymbal Voice

Three-operator FM: the main oscillator is modulated by two modulation oscillators. Good for
metallic and noisy sounds.

\figplaceholder{voice_cymbal_signal_flow.png}{Cymbal voice signal flow.}{Two modulator oscillators with their own gains summed and applied to the carrier as FM; filter; transient or wavefolder; amplitude envelope; distortion; sample-rate reduction.}

### FM page

\begin{paramtable}
f1  & Frequency 1 & Frequency of modulator 1. \\
f2  & Frequency 2 & Frequency of modulator 2. \\
g1  & Gain 1      & Gain of modulator 1. \\
g2  & Gain 2      & Gain of modulator 2. \\
wav & Waveform 1  & Waveform of modulator 1. \\
wav & Waveform 2  & Waveform of modulator 2. \\
\end{paramtable}

The AEG page offers `att dec rpt slp`. The MOD page holds only the velocity destination, amount and
volume cells.

## HiHat Voice

As the Cymbal voice, but with two amplitude decay times. In slot 6 the voice serves track 6
(closed hat, decay `d1`) and track 7 (open hat, decay `d2`). The closed hat **chokes** the open
hat.

\figplaceholder{voice_hihat_signal_flow.png}{HiHat voice signal flow.}{As the Cymbal diagram, with the amplitude envelope showing two decay times selected by track 6 or track 7 and a choke arrow from track 6 to track 7.}

The AEG page offers `att d1 d2 slp`. When a choke instrument sits in slot 6 the Voice page of
track 7 shows the open-hat sibling of each choke parameter.

## Alkaline Voice

Alkaline is an emulation of a classic bass-line synthesizer: one oscillator, a diode-ladder
low-pass filter, an accent circuit and slide. It has no separate filter-type, transient or
modulation-oscillator sections.

\figplaceholder{voice_alkaline_signal_flow.png}{Alkaline voice signal flow.}{Table-wave oscillator (saw, square, triangle or pulse) into the diode-ladder filter with envelope, accent and resonance feedback; VCA with overdrive; one-pole tone filter; volume and pan to mixer. Mark slide as a pitch lag on the oscillator and accent as a velocity-driven path.}

\begin{paramtable}
coa & Coarse tune & Coarse tuning of the oscillator. \\
tun & Tuning      & $\pm$700 cents, 63 is centre. \\
wav & Waveform    & \texttt{Saw}, \texttt{Sqr}, \texttt{Tri}. \\
pos & Position    & 0 free-running; 1 to 127 starts every note at that phase of the cycle. \\
atk & Attack      & Envelope attack of the amplifier; a very short attack after rests. \\
dec & Decay       & Decay of the filter envelope. \\
dst & Velocity destination & Velocity is the \emph{accent}: the destination row exists but velocity has no separate volume cell. \\
amt & Velocity amount & Amount. \\
sld & Slide       & On or off. If a slide-flagged note is followed by another, the pitch glides (22 ms) and the envelopes are not retriggered. \\
acc & Accent      & Amount of accent applied to notes with velocity 101 or more. \\
cut & Cutoff      & Filter cutoff. \\
res & Resonance   & Filter resonance. Above about 85 the filter feeds back beyond the original circuit and can self-oscillate, boundedly. \\
env & Env mod     & Depth of the envelope on the cutoff. \\
drv & Drive       & Input drive of the oscillator into the filter, $\times$1 to $\times$16. \\
\end{paramtable}

The Alkaline LFO page matches the Drum LFO page. The Mix page is **`vol pan ton drv`**: volume,
pan, a one-pole low-pass **tone** control (127 is open, 0 is darkest) and a VCA **overdrive**
(0 is unity gain, 127 is maximum).

\verify{Cell order on each page and the second use of drv (filter-input drive versus VCA overdrive): the firmware has two rows, alk\_drive on the filter page and instrument\_drive on the Mix page.}

### 303-style sequencing

- A note with **velocity 101 or more** is an accent; velocity 100 (the default) is not.
- The gate lasts 7/12 of a step. A **slide**-flagged note holds its gate through the next step.
- A note after a slide-flagged note is **legato**: the pitch glides and the envelopes are not
  retriggered.
- A MIDI note-off does not end the gate; the step length does.

\tip{Because velocity is the accent selector, use step velocity 127 for accented notes and 100
for plain ones.}

## Planned instruments

\draftnote{The engineering notes describe two further instrument families (a stereo
``Colloid'' voice and Advanced variants of Alkaline and Colloid with insert effects, and a drone
synthesizer). They are investigations only and are not implemented. Do not document them in the
release manual.}
