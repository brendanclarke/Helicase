# Appendix

## Glossary

\begin{paramtable}
Active Scene & Term & The Scene being viewed and edited. \\
Played Scene & Term & The Scene whose Pattern a track is playing. \\
Basic / Advanced & Term & Instrument classes; Advanced types are limited to two per Kit. \\
Choke & Term & An Instrument whose second decay is selected by track 7 (HiHat). \\
Edit mask & Term & The set of Scenes that receive an edit together. \\
Lock & Term & A value fixed on one effect-sequence step. \\
Morph & Term & Blend between a parameter's Normal and Morph values. \\
Scene 17 & Term & The hidden background Scene that plays during a Bank load. \\
St1 / St2 & Term & The two stereo output pairs. \\
\end{paramtable}

## References

1. "The Euclidean Algorithm Generates Traditional Musical Rhythms" by G.T. Toussaint,
   *Proceedings of BRIDGES: Mathematical Connections in Art, Music, and Science*, Banff,
   Alberta, Canada, July 31--August 3, 2005, pp. 47--56.

\draftnote{Include this reference only if the Euclidean generator is released.}

## Sample Credits

\draftnote{The sample credits of the original LXR (crash, tick, acoustic kick, rim shot, drip, hi-hat
and 808 clap) described samples embedded in the original firmware. The built-in crash was
removed from this firmware's flash; a card sample replaces it. Replace this section with the
credits of the factory samples and built-in clicks that actually ship. Source credits in the
original manual include Matias Reccius (CC0), patchen (CC BY), KEVOY (CC0), altemark (CC BY),
Neotone (CC0), pjcohen (CC0) and shiftr.}

## Figure checklist

All figures marked "FIGURE TO COME" in the manual are listed with their file names in
`FIGURES_TODO.md` in the manual's source folder, which `build_manual.py --figures` regenerates.
Place finished images in the `images` folder with the listed file name and the placeholder is
replaced automatically at the next build.

## Open questions

All **VERIFY** notes are listed in `DRAFT_QUESTIONS.md`, which `build_manual.py --questions`
regenerates.
