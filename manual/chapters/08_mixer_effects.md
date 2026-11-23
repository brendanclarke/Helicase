# Mixer and Effects

## Signal Path

\figplaceholder{signal_path.png}{Signal path from voices to outputs.}{Block diagram. Six instrument voices each pass through slider and volume and pan into an output route (St1, St2 or a single jack). A per-voice effect send taken before volume goes to the shared effects bus, then to the Scene's single Effect, then to the effect return (out, vol, pan) rejoining the output routes. Last stage: bus compressor on St1 or St2. Show headphones following St1.}

Each of the six voices is rendered, then passes through the mixer:

1. The voice's **volume** and the **slider** set its dry level (depending on the fader mode).
2. **Pan** places it in the stereo field (not applied for a single-jack route).
3. The voice goes to its **output route**.
4. A copy goes to the **effect bus** according to its **effect send**. The send is taken *before*
   the voice volume, but after the voice's own distortion.
5. The single **Effect** of the Scene processes the bus and returns, 100 percent wet, to its own
   output route with its own volume and pan.
6. The optional **bus compressor** processes one stereo pair last.

The master output clips at digital full scale. There is no master limiter unless the bus
compressor is on.

## Output routes and jack fallback

Each voice and the Effect return can go to **St1** (MAIN jacks and headphones), **St2** (OUT2 jacks)
or one of the four single jacks. The firmware checks which jacks are plugged in:

\begin{paramtable}
St1 & Stereo pair 1 & Stays on St1. If no MAIN jack is plugged in but an OUT2 jack is, it moves to St2. With nothing plugged in it stays on St1 (headphones). \\
St2 & Stereo pair 2 & Stays on St2 if an OUT2 jack is plugged in; otherwise it falls back to St1. \\
single jack & One output & Stays on its own jack; otherwise goes to the other side of the same pair, then the other pair; with nothing plugged in, to St1. \\
\end{paramtable}

A stereo signal routed to a single jack is summed to mono. The bus compressor has **no** jack
fallback: if the compressor is set to St2 and nothing is plugged into OUT2, the mixer has already
moved everything to St1 and the compressor processes nothing.

\tip{If only the MAIN jacks or the headphones are used, set the compressor target to \texttt{St1}.}

\verify{On-screen labels of the output routes and which page they are on (the Scene mix screen).}

## Faders, volume and effect send

Each voice has a physical **slider**. Its taper is logarithmic over a 30 dB range: the bottom is
exact silence, the top is unity, and the centre is about $-16$ dB. The slider's reading is smoothed
to remove zipper noise.

The **fader mode** of each voice decides what the slider controls:

\begin{paramtable}
pre & Both      & Dry level $=$ volume $\times$ slider. Send $=$ send $\times$ slider. The slider scales the dry sound and the effect send together. \\
pst & Dry only  & Dry level $=$ volume $\times$ slider. The send does not depend on the slider. \\
fx  & Send only & Dry level $=$ volume. Send $=$ send $\times$ slider. The slider is a pure send control. \\
xfd & Crossfade & Slider down is all effect, slider up is all dry; at the centre both are about $-16$ dB. \\
\end{paramtable}

\verify{The names pre and pst look reversed against everyday mixing language: in the firmware, mode pre scales the send with the slider, and mode pst leaves the send alone. Confirm the on-screen wording and the default fader mode before describing these modes in the release manual.}

The **effect send** is 0 to 127 per voice per Scene. The send can be automated per step. The
Morph value of the send lets the voice Morph amount blend between two send levels.

Voice **volume** is a pure level; it no longer acts as a hidden drive on any voice.

### Pan

All pans use one rule: 0 to 127 stored, displayed $-63$ to $+64$, with a stored 63 (display 0) at
centre. Voice pan uses a constant-power law. The Effect return and the CrumpBit delay stage use a
**balance** law: unity at centre, attenuating one side as you move away.

## Effects

A Scene has **one Effect**. The Effect types are:

\begin{paramtable}
off & Off & No effect processing. Existing tails stop. \\
flt & StFilter & A stereo resonant filter inline on the bus. \\
cbt & CrumpBit & An 8-bit lo-fi converter with five sub-effects and an output filter. \\
\end{paramtable}

\draftnote{The engineering notes mention further bus effects (reverb, echo or ``Space'' types)
from a later session whose documents were not supplied. Before release, confirm the full effect
type list and add the missing types here.}

Both shipped types are stereo in, stereo out. A Scene change to a Scene with the same Effect type
keeps the effect running so that delay tails ring on. A different type replaces it.

### The Effect page

Open it with **SHIFT + Performance mode**. Press again to toggle between the two screens of
SELECT 1.

\figplaceholder{effect_page.png}{The Effect page.}{Display close-ups of the two manager screens (typ out vol pan, and mrp run len scl) and a type page, with the SELECT buttons labelled 1 to 8 and the SEQ buttons labelled as effect steps.}

**SELECT 1** holds the manager cells shown on every type:

\begin{paramtable}
typ & Effect type  & \texttt{off}, \texttt{flt}, \texttt{cbt}. Edit with the encoder: click in, turn, click out to commit. \\
out & Return route & Output route of the effect return. \\
vol & Return level & 0 to 127, unity at 127. \\
pan & Return pan   & Balance, centre displays 0. \\
mrp & Effect Morph & 0 to 255. \\
run & Sequence run mode & \texttt{fwd rev pip rnd sel}. \\
len & Sequence length   & 1 to 16 steps. \\
scl & Step scale        & The same curve as the track step scale, with a default of 1/16. \\
\end{paramtable}

\attention{Changing the Effect type resets every type parameter to its defaults in both the Normal
and Morph values, **clears all 16 effect steps** and applies to every Scene in the edit mask. The
return level, pan, sequence settings and Effect Morph amount are kept.}

SELECT 2 to 8 hold the type's own pages, four cells per screen. Hold **SHIFT** to edit the Morph
value of a cell. Push the encoder for the full-size view.

### Effect sequence

The Effect has its own **16-step sequence** with 16 lanes (lane 0 is the Effect Morph). It follows
the master tempo; its step length is `scl`, and its run mode is `run`.

- **Hold one or more SEQ buttons, then edit a cell:** the value is written and **locked** on every
  held step. The screen shows the last-held step; a locked cell has its last glyph underlined.
- In `sel` mode there is no clock: tap or hold a SEQ button to select the step that plays.
- SEQ LEDs show the steps that have locks; a chase runs while playing, and the selected step blinks
  in `sel`.
- **SHIFT + Copy/Clear + SEQ** clears one step's locks. The clear menu clears the whole sequence.
- **SHIFT + TRACK** shows that voice's Scene mix screen (output, effect send, fader mode, Morph)
  for as long as the button is held, then returns to the Effect page.
- Effect locks are **not** recordable. Effect parameter edits and the Effect Morph cell are.

### Who wins: editing, locks and automation (rule R13)

For each Effect parameter, strongest first:

1. A **menu edit** (including a MIDI write) wins until the next automation of that parameter.
2. An **effect lock** applies for its own step and cancels any menu edit or Pattern automation on
   that parameter at the step start. When the step ends the parameter returns to its menu value.
3. **Pattern automation** (category \texttt{fx} on the step automation page) sets the parameter and
   holds until a lock, a menu edit or a reset event.
4. The **menu value**, with the Effect Morph blending between Normal and Morph.

An LFO is added on top of whatever value results. A reset (transport start, stop or reset, Scene
change or reload) clears every automation hold. Velocity cannot modulate the Effect.

## StFilter (`flt`)

The same filter as the voice filter, in floating point, applied to the stereo bus. It has four
parameters of its own: **frequency**, **resonance**, **drive** and **type**, plus the common `out
vol pan`. Its sequence lanes are frequency, resonance, drive, type, volume and pan.

\verify{On-screen tokens and type labels for the four StFilter cells.}

## CrumpBit (`cbt`)

CrumpBit is a lo-fi effect. The stereo send is converted as an 8-bit bipolar converter would, and
the eight data lines of the converter can each be forced off or inverted. The signal then passes
through one of five **sub-effects**, a shared **mix** crossfades the crushed dry signal and the
sub-effect, and a stereo **output filter** shapes the result.

### Bit buttons

The SELECT buttons act as bit buttons on the CrumpBit home screen. The home screen row shows the
eight bits from LSB at the left: `-` normal, `0` forced off, `!` inverted.

- **SELECT $n$** cycles bit $n-1$ through normal, off, invert.
- **SHIFT + SELECT $n$** resets the bit to normal.
- With SEQ steps held, the masks are locked on every held step.
- The masks are not automatable and not morphable. They are sequenced through effect lanes.

\attention{Setting or inverting the highest bit causes a thump because it creates a DC step. Bounded
self-oscillation appears when feedback is above 100 of 127; keep the monitor level low when
exploring.}

### Common cells

\begin{paramtable}
bof & Bit off mask & Which of the eight converter lines are forced off. \\
biv & Bit invert mask & Which lines are inverted. \\
mix & Mix & 0 is the crushed signal only, 127 is the sub-effect only. Default 0. \\
sub & Sub-effect & \texttt{dly cmb pit grn rvb}. Automatable per step only; not morphable. \\
typ & Filter type & Output filter type; \texttt{off} bypasses it. \\
frq & Filter frequency & Default 64. \\
res & Filter resonance & Default 0. \\
bal & Filter balance & Splits the cutoff between channels. 63 is centre. \\
\end{paramtable}

### Sub-effects

\begin{paramtable}
dly & 8-bit tape delay & Tape-style delay with a glide (about 150 ms) on time changes. Time follows a tempo-relative curve with pinned rhythmic stops from 1/64 to two bars. Feedback above 100 passes unity and can self-oscillate. Tone is non-resonant: 0 to 62 darkens, 63 is flat, 64 to 127 thins. \\
cmb & Comb / resonator & The loop retuned to a semitone comb: \texttt{not} is the note, \texttt{fbk} resonance, \texttt{pol} polarity, damping and balance. \\
pit & Pitch shifter & Two-head pitch shift. \texttt{aux} interval $\pm$36 semitones, \texttt{fin} fine pitch $\pm$52 cents, \texttt{rte} window size, \texttt{fbk} regeneration, tone and balance. \\
grn & Granular + delay & Two grains over a record line plus a delay tap: \texttt{den} density, \texttt{lng} length, position, \texttt{snc} freeze, pitches \texttt{p1} to \texttt{p3}, spread, width, delay time \texttt{rte} and echo gain \texttt{fbk}. \\
rvb & 8-bit reverb & Four-line reverb: \texttt{siz} size, \texttt{dcy} decay, \texttt{frz} freeze, \texttt{wid} width, \texttt{dmp} damping. \\
\end{paramtable}

The sub-effects share one loop of up to about 2.8 seconds of mono 8-bit audio. Switching `sub`
during playback produces deliberate glitches because the stored memory is reinterpreted.

\figplaceholder{crumpbit_overlay.png}{CrumpBit home screen.}{Display close-up showing the eight bit states row (for example dash 0 bang dash dash dash 0 dash) above the mrp frq res bal cells, with the eight SELECT buttons labelled as bits 0 to 7.}

\verify{Labels and cell order for each sub-effect, particularly aux versus p1 on grn, the pit snap option, and how to reach SELECT 1 manager screens while on CrumpBit.}

## Bus Compressor

The master bus compressor is a soft-knee, RMS-detecting, optical-style stereo-linked compressor
with a trigger-driven **ducker** and a gentle saturation stage. It sits last in the chain on **one**
output pair. It is a Scene setting, found on the last page of the Settings menu, and is not
automatable, morphable or modulatable.

\begin{paramtable}
cmp & Target    & \texttt{off}, \texttt{St1} or \texttt{St2}. Default off. \\
cam & Amount    & 0 to 127, default 48. One knob that sets threshold, ratio, make-up gain and saturation together. \\
ctm & Time      & 0 to 127, default 48. Sets attack (5 to 15 ms) and release (fast 60 to 600 ms, with a slow memory stage of 0.5 to 5 s). \\
csc & Sidechain & \texttt{off} or voice 1 to 6. The selected voice's triggers duck the bus. Track 7 counts as voice 6. \\
\end{paramtable}

| `cam` | Threshold | Ratio | Make-up |
|---:|---:|---:|---:|
| 0 | $-3$ dBFS | 1:1 | 0 dB |
| 48 (default) | $-13$ dBFS | 2.4:1 | $+1.1$ dB |
| 96 | $-23$ dBFS | 5:1 | $+7.1$ dB |
| 127 | $-30$ dBFS | 8:1 | $+11$ dB |

The ducker is **trigger-based**, not audio-based. When the selected voice triggers, the bus is
ducked by an amount that follows both `cam` (about 2 dB at 0, 9 dB at 48, 21 dB at 127 for velocity
127) and the trigger velocity (it scales with velocity cubed). The duck recovers according to
`ctm`. Setting `csc` to off disables ducking.

The compressor adds a fixed processing load whenever it is on.
