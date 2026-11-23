# Helicase LXR Owner's Manual (DRAFT) — Source

This directory contains the source files for the Helicase LXR Owner's Manual, structured for
version-control-friendly editing and reproducible PDF generation.

## Directory Structure

```
manual/
├── build_manual.py       # Build script — entry point for all operations
├── requirements.txt      # Python dependencies
├── chapters/             # Editable Markdown source for each chapter
│   ├── 01_overview.md  ... 13_specifications.md
│   │   (overview, quickstart, concepts, instruments, sequencer, operation modes,
│   │    synth modules, mixer and effects, MIDI, storage, firmware update,
│   │    drum synthesis intro, specifications)
│   └── 99_appendix.md
├── latex/
│   ├── main.tex          # Master document — assembles chapters
│   ├── preamble.tex      # Packages, colours, custom commands
│   ├── title_page.tex    # Cover page
│   └── midi_tables.tex   # Hand-maintained MIDI CC and NRPN tables
├── images/               # All images used in the manual
│   ├── logos/            # Extracted original logos (kept but not rendered in PDF)
│   ├── front_panel.png
│   ├── back_panel.jpeg
│   ├── voice_*_signal_flow.jpeg
│   ├── filter_*.png
│   ├── envelope_*.jpeg / *.png
│   └── ...
├── image_templates/      # Editable SVG templates for each image type
│   ├── signal_flow_template.svg
│   ├── filter_plot_template.svg
│   ├── envelope_template.svg
│   ├── panel_diagram_template.svg
│   └── substep_diagram_template.svg
└── build/                # Generated output (git-ignored)
    └── Helicase_LXR_Owners_Manual.pdf
```

## Draft workflow

This is a draft. Three commands in `latex/preamble.tex` mark unfinished material:

| Command | Effect |
|---|---|
| `\figplaceholder{file.png}{Caption}{What it should show}` | Uses `images/file.png` if it exists, otherwise draws a grey "FIGURE TO COME" box with the description |
| `\verify{text}` | Inline red note: a fact to confirm on the device before release |
| `\draftnote{text}` | Boxed note for a block of unconfirmed content |

```bash
python build_manual.py --figures     # writes FIGURES_TODO.md (every placeholder)
python build_manual.py --questions   # writes DRAFT_QUESTIONS.md (every VERIFY / DRAFT NOTE)
```

To supply a figure, save it in `images/` under the listed file name and rebuild. Use the SVG
files in `image_templates/` as starting points. Underscores in the second and third arguments
of `\figplaceholder` must be written `\_`.

## Prerequisites

### System tools (must be on PATH)

| Tool | Purpose | Install |
|---|---|---|
| `pdflatex` | Compile LaTeX to PDF | TeX Live: `apt install texlive-full` or [tug.org/texlive](https://tug.org/texlive/) |
| `pandoc` | Convert Markdown to LaTeX | [pandoc.org](https://pandoc.org/installing.html) or `apt install pandoc` |

### Python packages

```bash
pip install -r requirements.txt
```

## Building the Manual

```bash
# Full build — produces build/Helicase_LXR_Owners_Manual.pdf
python build_manual.py

# Convert a single chapter to LaTeX (for quick iteration, no PDF generated)
python build_manual.py --chapter 09

# Watch mode — rebuilds automatically on any .md or .tex change
python build_manual.py --watch

# Convert .md -> .tex only, skip pdflatex
python build_manual.py --no-pdf

# Remove all generated files
python build_manual.py --clean

# Verbose output for debugging
python build_manual.py --verbose
```

## Editing Content

### Modifying an existing chapter

Edit the relevant `.md` file in `chapters/`. Raw LaTeX is supported inline anywhere in the
Markdown using fenced blocks or inline `\command{}` syntax. Rebuild to regenerate the PDF.

### Adding a new chapter

1. Create `chapters/NN_chapter_name.md` (use a number prefix to control ordering).
2. Add a corresponding `\input{chapters/NN_chapter_name.tex}` line in `latex/main.tex`
   at the desired position.
3. Rebuild.

### Updating MIDI tables

Edit `latex/midi_tables.tex` directly (it is `\input` from `chapters/09_midi.md`) — it is plain LaTeX and not generated from Markdown.
Each row follows the pattern:

```latex
<NRPN ID> & <Target> & <Works on> \\
```

### Replacing or adding images

Place new images in `images/`. Reference them in Markdown using a standard LaTeX figure block:

```latex
\begin{figure}[H]
  \centering
  \includegraphics[width=0.85\linewidth]{your_image.png}
  \caption{Caption text.}
\end{figure}
```

SVG templates for each image type are in `image_templates/`. Open them in Inkscape or
GIMP to create edited versions, then export as PNG/JPEG into `images/`.

### Custom LaTeX commands

Defined in `latex/preamble.tex`:

| Command | Usage | Effect |
|---|---|---|
| `\param{name}` | `\param{frq}` | Bold monospace parameter name |
| `\menu{path}` | `\menu{OSC > wav}` | Monospace menu path |
| `\oled{line1}{line2}` | See chapters | Green-on-black OLED display mockup |
| `\oledsingle{line}` | See chapters | Single-line OLED mockup |
| `\attention{text}` | See chapters | Orange attention callout box |
| `\tip{text}` | See chapters | Blue "Did you know?" tip box |
| `paramtable` env | See chapters | 3-column parameter table |
| `miditableCC` env | midi_tables.tex | Paginating MIDI CC table |
| `miditableNRPN` env | midi_tables.tex | Paginating MIDI NRPN table |

### Updating the version string

Edit the `\manualversion` definition near the top of `latex/preamble.tex`.

## The `build/` directory

The `build/` directory is entirely generated and should be added to `.gitignore`:

```gitignore
docs/manual/build/
```
