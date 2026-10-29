One-shot samples as the mod oscillator

A new osc_modulatorTrigger() in Oscillator.c/.h restarts the modulator's sample at its start on every hit when its wav selects an s entry.
It binds the current selection first, so a sample chosen by automation on the same step is the one that restarts.
Stock waves and l/w entries return straight away, so they behave as before.
It's called from the Drum and HiHat triggers, whose modulators never reset. Cymbal needed no change: it already resets both modulators to phase 0 on every hit.
One-shot samples in the click/transient list

The list: Snp, Ofs, the 12 built-in clicks, then one entry per installed sample, s01, s02, …
One-shots only, no loop or wavetable entries.
Compact cells show sNN; the single-parameter view shows the sample's 8-character name on the left.
The menu limit and the step-automation range follow the sample count.
Playback: the sample plays once from the trigger.
frq sets the speed (1.0 = the file's own rate).
vol is applied on top of the install normalisation.
It goes silent at the end until the next hit.
After a reinstall: a stored value past the installed samples plays the last sample, as the modulator list does. If no samples are installed it falls back to the last built-in click, with one silent block first.
CPU: no worst-case increase.

The new sample loop is 20 instructions per sample. The built-in click loop stays at 22 and its per-block setup is unchanged. I compared the linked click loops in the mixer against a build of the base commit and they're identical.
Getting there took restructuring: my first version cost 24 per sample, so I switched to an integer interpolation and moved the sample path out of line.
The one cost added is at trigger time, not per sample: Drum and HiHat each make one extra call per hit, which returns immediately for stock waves.
Behaviour to know: if you switch a transient between a built-in click and a sample in the middle of a hit, it stays silent until the next trigger.