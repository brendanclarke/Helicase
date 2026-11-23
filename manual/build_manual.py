#!/usr/bin/env python3
"""
build_manual.py — Helicase LXR Owner's Manual build script.

Converts Markdown chapters to LaTeX via Pandoc, then compiles the
master LaTeX document with pdflatex.

Usage:
    python build_manual.py                  # Full build
    python build_manual.py --chapter 03     # Build single chapter (preview tex only)
    python build_manual.py --clean          # Remove build artefacts
    python build_manual.py --watch          # Rebuild on file changes
    python build_manual.py --no-pdf         # Convert md->tex only, skip pdflatex
    python build_manual.py --figures        # Write FIGURES_TODO.md (placeholder figures)
    python build_manual.py --questions       # Write DRAFT_QUESTIONS.md (VERIFY / DRAFT NOTE items)
"""

import argparse
import re
import logging
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

# ── Paths ────────────────────────────────────────────────────────────────────

ROOT        = Path(__file__).parent.resolve()
CHAPTERS_MD = ROOT / "chapters"
LATEX_DIR   = ROOT / "latex"
IMAGES_DIR  = ROOT / "images"
BUILD_DIR   = ROOT / "build"
BUILD_CHAP  = BUILD_DIR / "chapters"
OUTPUT_PDF  = BUILD_DIR / "Helicase_LXR_Owners_Manual.pdf"

# ── Logging ──────────────────────────────────────────────────────────────────

logging.basicConfig(
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
    level=logging.INFO,
)
log = logging.getLogger("build_manual")

# ── Helpers ──────────────────────────────────────────────────────────────────

def require_tool(name: str) -> str:
    """Return the path to an external tool or exit with a clear error."""
    path = shutil.which(name)
    if path is None:
        log.error(
            "Required tool '%s' not found on PATH.\n"
            "  Install TeX Live for pdflatex, and Pandoc from https://pandoc.org",
            name,
        )
        sys.exit(1)
    return path


def md_to_tex(src: Path, dst: Path) -> None:
    """Convert a single Markdown file to LaTeX using Pandoc."""
    pandoc = require_tool("pandoc")
    cmd = [
        pandoc,
        str(src),
        "--from", "markdown+raw_tex",   # allow raw LaTeX blocks in .md files
        "--to",   "latex",
        "--output", str(dst),
        "--top-level-division=section",  # treat # headings as \section{}
    ]
    log.debug("pandoc: %s -> %s", src.name, dst.name)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        log.error("Pandoc failed on %s:\n%s", src.name, result.stderr)
        sys.exit(1)


def run_pdflatex(main_tex: Path, build_dir: Path, passes: int = 2) -> None:
    """Run pdflatex (multiple passes for TOC/cross-references)."""
    pdflatex = require_tool("pdflatex")
    cmd = [
        pdflatex,
        "-interaction=nonstopmode",
        "-halt-on-error",
        f"-output-directory={build_dir}",
        str(main_tex),
    ]
    for i in range(passes):
        log.info("pdflatex pass %d/%d …", i + 1, passes)
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=build_dir)
        if result.returncode != 0:
            # Print the last 40 lines of the log to help diagnose
            lines = result.stdout.splitlines()
            log.error(
                "pdflatex failed (pass %d). Last output:\n%s",
                i + 1,
                "\n".join(lines[-40:]),
            )
            sys.exit(1)

# ── Build steps ───────────────────────────────────────────────────────────────

def step_clean() -> None:
    """Remove all generated files from the build directory."""
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
        log.info("Cleaned build directory.")
    else:
        log.info("Build directory does not exist — nothing to clean.")


def step_prepare_dirs() -> None:
    """Create required output directories."""
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    BUILD_CHAP.mkdir(parents=True, exist_ok=True)
    log.debug("Build directories ready.")


def step_convert_chapters(chapter_filter: str | None = None) -> list[Path]:
    """
    Convert all (or a filtered subset of) Markdown chapters to LaTeX.

    Returns the list of .md files that were converted.
    """
    md_files = sorted(CHAPTERS_MD.glob("*.md"))
    if not md_files:
        log.error("No .md files found in %s", CHAPTERS_MD)
        sys.exit(1)

    converted = []
    for md in md_files:
        if chapter_filter and chapter_filter not in md.stem:
            continue
        dst = BUILD_CHAP / md.with_suffix(".tex").name
        md_to_tex(md, dst)
        log.info("Converted  %s", md.name)
        converted.append(md)

    if not converted:
        log.error("No chapters matched filter '%s'", chapter_filter)
        sys.exit(1)

    return converted


def step_copy_latex_sources() -> None:
    """
    Copy hand-maintained LaTeX files (preamble, title page, MIDI tables,
    main.tex) into the build directory so pdflatex can find them.
    The build directory mirrors the expected relative paths used in main.tex.
    """
    # Copy the whole latex/ directory as build/latex/
    dest_latex = BUILD_DIR / "latex"
    if dest_latex.exists():
        shutil.rmtree(dest_latex)
    shutil.copytree(LATEX_DIR, dest_latex)
    log.debug("Copied latex/ sources to build/latex/")


def step_compile_pdf() -> None:
    """Compile main.tex from the build directory."""
    # pdflatex is run with build/ as CWD; main.tex lives at build/latex/main.tex
    main_tex = BUILD_DIR / "latex" / "main.tex"
    if not main_tex.exists():
        log.error("main.tex not found at %s", main_tex)
        sys.exit(1)
    run_pdflatex(main_tex, BUILD_DIR, passes=2)
    # pdflatex names the output after the input file: main.pdf
    generated = BUILD_DIR / "main.pdf"
    if generated.exists():
        generated.rename(OUTPUT_PDF)
        log.info("PDF written to  %s", OUTPUT_PDF.relative_to(ROOT))
    else:
        log.error("Expected output PDF not found at %s", generated)
        sys.exit(1)



# ── Draft helpers ─────────────────────────────────────────────────────────────

def _balanced(text: str, start: int) -> tuple[str, int]:
    """Return (content, end_index) of the {...} group opening at text[start]."""
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c == "\\":
            i += 2
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
        i += 1
    return text[start + 1:], len(text)


def _find_commands(text: str, name: str, nargs: int):
    """Yield (args, line_number) for every \\name{..}{..}.. in text."""
    pat = "\\" + name + "{"
    pos = 0
    while True:
        i = text.find(pat, pos)
        if i < 0:
            return
        j = i + len(name) + 1
        args = []
        for _ in range(nargs):
            if j >= len(text) or text[j] != "{":
                break
            arg, j = _balanced(text, j)
            args.append(" ".join(arg.split()))
        yield args, text.count("\n", 0, i) + 1
        pos = max(j, i + 1)


def step_figures() -> None:
    """Write FIGURES_TODO.md listing every \\figplaceholder, and whether its image exists."""
    rows = []
    for md in sorted(CHAPTERS_MD.glob("*.md")):
        for args, line in _find_commands(md.read_text(encoding="utf-8"), "figplaceholder", 3):
            if len(args) == 3:
                rows.append((md.name, line, *args))
    out = ["# Figures to produce", "",
           "Generated by `python build_manual.py --figures`. Put each finished image in `images/`",
           "with the exact file name below; the placeholder is then replaced at the next build.", "",
           "| # | File | Done | Chapter | Caption | What it should show |", "|---|---|---|---|---|---|"]
    for n, (ch, line, fname, cap, desc) in enumerate(rows, 1):
        done = "yes" if (IMAGES_DIR / fname).exists() else "no"
        out.append(f"| {n} | `{fname}` | {done} | {ch}:{line} | {cap} | {desc} |")
    (ROOT / "FIGURES_TODO.md").write_text("\n".join(out) + "\n", encoding="utf-8")
    log.info("Wrote FIGURES_TODO.md (%d figures)", len(rows))


def step_questions() -> None:
    """Write DRAFT_QUESTIONS.md listing every \\verify and \\draftnote."""
    out = ["# Open questions in the draft", "",
           "Generated by `python build_manual.py --questions`.", ""]
    total = 0
    for md in sorted(CHAPTERS_MD.glob("*.md")):
        items = [("VERIFY", a[0], l) for a, l in _find_commands(md.read_text(encoding="utf-8"), "verify", 1) if a]
        items += [("NOTE", a[0], l) for a, l in _find_commands(md.read_text(encoding="utf-8"), "draftnote", 1) if a]
        items.sort(key=lambda t: t[2])
        if not items:
            continue
        out += [f"## {md.name}", ""]
        for kind, text, line in items:
            out.append(f"- **{kind}** (line {line}): {text}")
            total += 1
        out.append("")
    (ROOT / "DRAFT_QUESTIONS.md").write_text("\n".join(out), encoding="utf-8")
    log.info("Wrote DRAFT_QUESTIONS.md (%d items)", total)

# ── Watch mode ────────────────────────────────────────────────────────────────

def step_watch() -> None:
    """Watch for changes in chapters/ or latex/ and rebuild automatically."""
    try:
        from watchdog.observers import Observer
        from watchdog.events import FileSystemEventHandler
    except ImportError:
        log.error(
            "The 'watchdog' package is required for --watch mode.\n"
            "Install it with:  pip install watchdog"
        )
        sys.exit(1)

    class RebuildHandler(FileSystemEventHandler):
        def __init__(self):
            self._last = 0.0

        def on_modified(self, event):
            if event.is_directory:
                return
            # Debounce: ignore events within 2 seconds of the last build
            now = time.monotonic()
            if now - self._last < 2.0:
                return
            self._last = now
            path = Path(event.src_path)
            if path.suffix in (".md", ".tex"):
                log.info("Change detected in %s — rebuilding …", path.name)
                full_build()

    observer = Observer()
    observer.schedule(RebuildHandler(), str(CHAPTERS_MD), recursive=False)
    observer.schedule(RebuildHandler(), str(LATEX_DIR), recursive=False)
    observer.start()
    log.info("Watching for changes. Press Ctrl-C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
    observer.join()

# ── Full build ────────────────────────────────────────────────────────────────

def full_build(chapter_filter: str | None = None, pdf: bool = True) -> None:
    step_prepare_dirs()
    step_copy_latex_sources()
    step_convert_chapters(chapter_filter)
    if pdf and chapter_filter is None:
        step_compile_pdf()
    elif pdf and chapter_filter:
        log.info(
            "Single-chapter mode: LaTeX written to build/chapters/. "
            "Skipping full PDF compilation."
        )

# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build the Helicase LXR Owner's Manual PDF from Markdown + LaTeX sources.",
    )
    parser.add_argument(
        "--chapter", "-c",
        metavar="PREFIX",
        help="Convert only chapters whose filename contains PREFIX (e.g. '06').",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove the build directory and exit.",
    )
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Watch for file changes and rebuild automatically.",
    )
    parser.add_argument(
        "--no-pdf",
        action="store_true",
        help="Convert .md -> .tex only; skip pdflatex.",
    )
    parser.add_argument(
        "--figures",
        action="store_true",
        help="Write FIGURES_TODO.md listing placeholder figures, then exit.",
    )
    parser.add_argument(
        "--questions",
        action="store_true",
        help="Write DRAFT_QUESTIONS.md listing VERIFY notes, then exit.",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable debug logging.",
    )
    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    if args.clean:
        step_clean()
        return

    if args.figures or args.questions:
        if args.figures:
            step_figures()
        if args.questions:
            step_questions()
        return

    if args.watch:
        # Run one full build first, then enter watch loop
        full_build(pdf=not args.no_pdf)
        step_watch()
        return

    full_build(chapter_filter=args.chapter, pdf=not args.no_pdf)


if __name__ == "__main__":
    main()
