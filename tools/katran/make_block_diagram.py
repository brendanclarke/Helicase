#!/usr/bin/env python3
"""Write katran_block_diagram.svg: the Katran block diagram beside the source patch.

What:       draws every Katran module (oscillators, the arena read, both
            feedback cores, filters, ring modulator, VCAs, output tilt, stereo
            matrix, modulators), the per-sample and per-block connections, the
            rows that act on each block with their laws, and the AIRA Modular
            patch (knowledge_files/drafts/patch.png, embedded as base64) with
            42 numbered controls and a table mapping each to its Katran row.
Why:        the S103 parameter review (KATRAN_BLOCK_DIAGRAM_SVG.md); one
            script so the drawing can be redone when the rows change.
Inputs:     the patch image. The laws in the drawing are transcribed by hand
            from KatranVoice.c / KatranTables.c (S103 P5 tree), not parsed.
Outputs:    katran_block_diagram.svg in the repository root.
Accessors:  python3 tools/katran/make_block_diagram.py
"""
import base64
import html
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
W, H = 3000, 1470
out = []
E = html.escape


def add(s):
    out.append(s)


def text(x, y, s, size=11, weight="normal", fill="#1d1d1f", anchor="start", style=""):
    add(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}" {style}>{E(s)}</text>')


def box(x, y, w, h, title, lines=(), fill="#ffffff", stroke="#333", tsize=13, lsize=10.5,
        tfill="#111", dash=None, rx=6):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" '
        f'stroke="{stroke}" stroke-width="1.4"{d}/>')
    if title:
        text(x + 8, y + 17, title, tsize, "bold", tfill)
    yy = y + (34 if title else 15)
    for ln in lines:
        if isinstance(ln, tuple):
            s, col = ln
        else:
            s, col = ln, "#333"
        text(x + 8, yy, s, lsize, "normal", col)
        yy += lsize + 4


def pill(x, y, s, fill="#fff3c4", stroke="#c9a227", size=10.5, w=None):
    w = w or (len(s) * size * 0.58 + 12)
    add(f'<rect x="{x}" y="{y - size - 3}" width="{w:.0f}" height="{size + 8}" rx="8" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="1"/>')
    text(x + 6, y + 1, s, size, "bold", "#5a4300")
    return w


def line(pts, color="#111", width=1.8, dash=None, arrow=True):
    d = " ".join(f"{x},{y}" for x, y in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    mk = ' marker-end="url(#ah)"' if arrow and color == "#111" else (
        f' marker-end="url(#ah_{color[1:]})"' if arrow else "")
    add(f'<polyline points="{d}" fill="none" stroke="{color}" stroke-width="{width}"{da}{mk}/>')


def circ(x, y, r=10, fill="#fff", stroke="#111", label="", size=12):
    add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="1.4"/>')
    if label:
        text(x, y + size * 0.36, label, size, "bold", "#111", "middle")


MOD = "#2e7d32"     # modulation lines
CTL = "#1565c0"     # control-rate laws
WARN = "#c62828"    # review flags
BUF = "#6a1b9a"     # the arena read

add(f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
    f'width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica, Arial, sans-serif">')
add('<defs>')
for name, col in (("ah", "#111"), ("ah_" + MOD[1:], MOD), ("ah_" + CTL[1:], CTL),
                  ("ah_" + BUF[1:], BUF), ("ah_" + WARN[1:], WARN)):
    add(f'<marker id="{name}" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
        f'<path d="M0,0 L9,4.5 L0,9 z" fill="{col}"/></marker>')
add('</defs>')
add(f'<rect x="0" y="0" width="{W}" height="{H}" fill="#fafafa"/>')

# ------------------------------------------------------------------ title
text(20, 34, "Katran — block diagram (S103 P5 tree: cores A/B, stereo rotation, bipolar width)", 22, "bold")
text(20, 56, "Every block runs every 32-sample render block (constant CPU). Per-block laws are blue; per-sample audio is black. "
     "Yellow pills are rows (short label · key · range). Red ! = review flag (see KATRAN_BLOCK_DIAGRAM_SVG.md §6).", 12, fill="#444")
# legend
lx = 1180
line([(lx, 78), (lx + 40, 78)], "#111")
text(lx + 46, 82, "audio (per sample)", 11)
line([(lx + 170, 78), (lx + 210, 78)], MOD, dash="5,3")
text(lx + 216, 82, "modulation", 11, fill=MOD)
line([(lx + 300, 78), (lx + 340, 78)], BUF, dash="2,3")
text(lx + 346, 82, "buffer (arena) read", 11, fill=BUF)
pill(lx + 480, 82, "row · key · range")

# =================================================================== SOURCES
SX = 20
box(SX, 100, 360, 120, "Pitch (per block)", [
    "OSC A f = note + coa + fin   (platform pitch rows,",
    "    x LFO/velocity multiplier on coa: modNodeValue)",
    "OSC B f = OSC A's pitch + (bCoarse − 48) + bFine/63×0.5 st",
    ("    + patch-LFO cv·lfl·0.3 st   (! lfl applied twice)", WARN),
    "Table forms capped 0.45 fs (core B copy: 0.45 fs/2)",
], fill="#eef4ff", stroke=CTL, tfill=CTL)
pill(SX + 4, 238, "coa · osc1_pitch_coarse")
pill(SX + 188, 238, "fin · osc1_pitch_fine")

box(SX, 252, 360, 205, "OSC B — the ring/FM modulator (full rate, 1 copy)", [
    "Own type + wave (S105 P1 A8): wav Saw Rec Tri Sin / pwm / wtb,",
    "  or buf (the arena window); its own wavetable bind",
    "r/f: rng / fmA / fmB / fAB (loop PM on that core), sdb: feedback",
    "pos sets pwm width / wtb frame / arena window (+ cv·lfl·24 steps)",
    "Output B[n] (int16), LSB = 1 on the sample its phase wraps",
    "Row / frame / window change only at a cycle start",
    "pwm D sum halved (B2)",
], fill="#ffffff")
yy = 420
for s in ("coa · kat_b_coarse  −48..+79 st", "fin · kat_b_fine ±0.5 st"):
    pass
pill(SX + 6, 425, "coa · kat_b_coarse  (−48..+79 st)")
pill(SX + 200, 425, "fin · kat_b_fine")
pill(SX + 6, 448, "pos · kat_b_pos")
pill(SX + 112, 448, "wav · osc1_wave")
pill(SX + 216, 448, "typ · osc1_type")

box(SX, 470, 360, 190, "OSC A — the exciter (one copy per core)", [
    "Own type + wave (S105 P1 A8): wav / pwm / wtb, or buf",
    "Core A copy: full rate, inc = f / ratio, pos − dtn·16",
    "Core B copy: half rate (16 smp/block), f × ratio, pos + dtn·16",
    "  ratio = 2^(dtn'² × 25 cents)  — dtn moves detune AND pos spread",
    "Hard sync: reset when (D_core + 0.5·32767·wrapB) crosses th upward",
    "  th = sth × 258 (int16 units); sth 127 = never",
    "Same forms as OSC B, from Osc 1's own type and wave",
], fill="#ffffff")
pill(SX + 6, 640, "pos · osc1_position")
pill(SX + 132, 640, "dtn · kat_spread_a")
pill(SX + 252, 640, "snc · kat_sync")

box(SX, 674, 360, 200, "Arena read — the 'buf' oscillator type (DTCM FxBuffer, ~63 k smp)", [
    ("Chosen per oscillator by its typ (Osc 1 or Osc 2)", BUF),
    "x = arena[S + (phase >> 22)]  — 1,024-smp window, no interp",
    "S = pos × (N − 1024)/127, taken at the osc's next cycle start",
    "Speed = the pitch: root note, coa 0 → 1 smp/smp (43 Hz); unclamped",
    "→ DC block (1-pole HP ≈ 10 Hz)",
    "→ damp 1-pole LP (dmp)  → guard (peak/8 smp → ≤ 0.7 FS)  → SSAT16",
    ("! dmp: 0 = open, 1 = 20 Hz … 127 = 300 Hz (jump, darkest at 1)", WARN),
], fill="#f6eefc", stroke=BUF, tfill=BUF)
pill(SX + 6, 862, "typ · osc1_type / kat_b_type", fill="#f1e0fb", stroke=BUF)
pill(SX + 128, 862, "dmp · kat_damp", fill="#f1e0fb", stroke=BUF)
circ(SX + 344, 690, 9, "#fff", WARN, "!", 12)

box(SX, 888, 360, 70, "Noise (hardware RNG, every sample)", [
    "core A: 1 read/sample; core B: 1 read/half-rate sample",
    "Mixed into the exciter by nse (crossfade with OSC A)",
], fill="#ffffff")

box(SX, 972, 360, 150, "Kick / run (per block)", [
    "Any trigger (step, pad, MIDI, chromatic): kE = velocity,",
    "  decays −60 dB in 80 ms; re-arms the run gain gr (5 ms)",
    "burst = min(1, 2·kik'): exc += burst·kE·0.8, nse += burst·kE·0.5",
    "swell = max(0, 2·kik' − 1): feedback += swell·kE·0.3",
    "gr: run 127 = on (20 ms rise); 0..126 = decay 0.25 s .. 60 s",
    ("  transport stop decays at T(126) = 60 s even at run 127", WARN),
], fill="#eef4ff", stroke=CTL, tfill=CTL)
pill(SX + 6, 1138, "run · kat_run")
pill(SX + 112, 1138, "kik · kat_kick")
pill(SX + 214, 1138, "amt/dst · velocity")

# =================================================================== CORES
CX = 440


def core(y0, name, rate, out_label, vca_text, vca_pill):
    X0 = CX
    add(f'<rect x="{X0}" y="{y0}" width="760" height="430" rx="10" fill="#ffffff" stroke="#888" '
        f'stroke-width="2" stroke-dasharray="8,4"/>')
    text(X0 + 12, y0 + 22, f"{name}  ({rate})", 15, "bold")
    # the loop
    line([(X0 + 715, y0 + 50), (X0 + 715, y0 + 36), (X0 + 89, y0 + 36), (X0 + 89, y0 + 50)])
    text(X0 + 748, y0 + 31, "feedback loop: ring write → delay read (a comb at T)", 10.5, "bold", "#ad1457", "end")
    box(X0 + 14, y0 + 50, 150, 150, "Short Delay", [
        "ring 2,048 smp (one", "  FxBuffer voice unit)",
        "timed tap at w − T", "  (Q16 linear interp)",
        "dry tap: 1 block back",
        "D = (timed + dry) / 2",
    ], fill="#fde7ef", stroke="#ad1457", tfill="#ad1457")
    box(X0 + 200, y0 + 45, 175, 115, "TORCIDO ch 1 (loop)", [
        "x = D × tGain (drv: ×1..×32)",
        "y = x' − 4/27 x'³ + tube·x'²",
        "  (x' = clamp ±1.5)",
        "t1 = LV1 (wet·y + (1−wet)·D)",
        "  LV1 = 60/127 fixed",
    ], fill="#fff8e6", stroke="#ef6c00", tfill="#ef6c00")
    box(X0 + 200, y0 + 175, 175, 100, "TORCIDO ch 2 (exciter)", [
        "e = exc·((1−nse)·A + nse·noise)",
        "t2 = LV2 · cubic(tGain · e)",
        "  LV2 = 64/127 fixed",
        "  (bare cubic, no tube)",
    ], fill="#fff8e6", stroke="#ef6c00", tfill="#ef6c00")
    circ(X0 + 410, y0 + 160, 13, "#fff", "#111", "+", 15)
    box(X0 + 440, y0 + 60, 225, 125, "The band filter (S105 P1 A4)", [
        "1-pole HP -> 1-pole LP; cut / wid / ofs",
        "resonance rises as wid narrows",
        "+ the slow envelope 48·d_s·(L−0.5)",
        "+ LFO; tracks the loop pitch at narrow wid",
    ], fill="#e8f5e9", stroke="#2e7d32", tfill="#2e7d32")
    box(X0 + 678, y0 + 50, 74, 160, "write", [
        "gr ×", "( u", "+ fb·D", "+ xfb·", "  D_oth )", "SSAT16", "→ ring",
    ], fill="#f3f3f3", stroke="#333")
    box(X0 + 440, y0 + 225, 140, 55, "Ring mod", [
        "r = t1·((1−rk) + rk·lvB'·B)",
    ], fill="#e3f2fd", stroke="#1565c0", tfill="#1565c0", lsize=9.5)
    box(X0 + 600, y0 + 225, 152, 75, vca_text[0], list(vca_text[1:]),
        fill="#e3f2fd", stroke="#1565c0", tfill="#1565c0")
    # audio arrows
    line([(X0 + 164, y0 + 100), (X0 + 200, y0 + 100)])
    line([(X0 + 375, y0 + 100), (X0 + 410, y0 + 100), (X0 + 410, y0 + 147)])
    line([(X0 + 375, y0 + 225), (X0 + 410, y0 + 225), (X0 + 410, y0 + 173)])
    line([(X0 + 423, y0 + 160), (X0 + 440, y0 + 160)])
    line([(X0 + 580, y0 + 160), (X0 + 595, y0 + 160)])
    line([(X0 + 665, y0 + 160), (X0 + 678, y0 + 160)])
    line([(X0 + 392, y0 + 100), (X0 + 392, y0 + 252), (X0 + 440, y0 + 252)])      # t1 -> ring
    line([(X0 + 428, y0 + 252), (X0 + 428, y0 + 288), (X0 + 590, y0 + 288), (X0 + 590, y0 + 282),
          (X0 + 600, y0 + 282)], "#111", 1.4)                                      # t1 -> VCA (raw loop)
    line([(X0 + 580, y0 + 250), (X0 + 600, y0 + 250)])                             # r -> VCA
    line([(X0 + 752, y0 + 262), (X0 + 790, y0 + 262)])
    text(X0 + 752, y0 + 316, out_label, 11, "bold", anchor="end")
    # D -> sync / other core
    line([(X0 + 40, y0 + 200), (X0 + 40, y0 + 212), (X0 - 12, y0 + 212)], CTL, 1.3, "4,3")
    text(X0 + 14, y0 + 226, "D → OSC A sync test,", 9.5, fill=CTL)
    text(X0 + 14, y0 + 238, "D → other core (xfb)", 9.5, fill=CTL)
    text(X0 + 196, y0 + 247, "A ▶", 10, "bold", "#111", "end")
    text(X0 + 196, y0 + 268, "noise ▶", 10, "bold", "#555", "end")
    # B into the ring
    text(X0 + 576, y0 + 340, "B[n] ▲", 10, "bold", "#1565c0", "end")
    # pills
    pill(X0 + 440, y0 + 203, "cut/wid/ofs · kat_flt_*")
    pill(X0 + 598, y0 + 203, "lct · kat_low_cut")
    pill(X0 + 200, y0 + 300, "drv · kat_drive")
    pill(X0 + 304, y0 + 300, "bln · kat_wet")
    pill(X0 + 200, y0 + 326, "lvA · kat_level_a")
    pill(X0 + 318, y0 + 326, "noi · kat_noise")
    pill(X0 + 440, y0 + 312, "lvB · kat_b_level")
    pill(X0 + 600, y0 + 336, vca_pill)
    pill(X0 + 14, y0 + 380, "tim · kat_time  32..1792 smp")
    pill(X0 + 200, y0 + 380, "fbk · kat_feedback 0..0.95")
    pill(X0 + 384, y0 + 380, "wow · kat_drift")
    pill(X0 + 494, y0 + 380, "dtk · kat_track")
    pill(X0 + 14, y0 + 410, "spr · kat_spread (bipolar: <63 xfb to 0.6, >63 T ±12 %, F1 ±12 st)")
    return X0 + 790, y0 + 262


ax, ay = core(100, "CORE A", "full rate: 32 samples / block",
              "core A → buf", ("AMP2 (VCA)", "out = r + g2·t1", "g2 = min(1,.38(1−v))", " × clamp(.42 + inv)"),
              "bal · kat_vca (v)")
bx, by = core(560, "CORE B", "half rate at fs/2, output ×2 interp.",
              "core B → right[]", ("AMP1 (VCA)", "out = r · (1 + g1)", "g1 = min(1,.67(1+v))", " × clamp(.42 + cv)"),
              "bal · kat_vca (v)")
text(CX, 1012, "Core B differs: delay time and dry tap in half-rate samples (same comb pitch), F1 at fs/2 (ceiling 8.1 kHz), "
     "F2 coefficient k(2−k),", 10.5, fill="#444")
text(CX, 1027, "OSC B read at 2n, core A's D decimated (core A reads core B's D held), its own noise read.", 10.5, fill="#444")

# source -> core arrows (the gap column x 380..440)
line([(380, 540), (400, 540), (400, 343), (CX + 200, 343)], "#111", 1.5)          # OSC A (core A copy)
line([(380, 600), (410, 600), (410, 803), (CX + 200, 803)], "#111", 1.5)          # OSC A (core B copy)
line([(380, 920), (420, 920), (420, 364), (CX + 200, 364)], "#666", 1.1, "3,3")   # noise -> core A
line([(420, 824), (CX + 200, 824)], "#666", 1.1, "3,3")                           # noise -> core B
line([(380, 300), (388, 300), (388, 445), (CX + 570, 445), (CX + 570, 380)], "#1565c0", 1.5)   # B -> ring A
line([(388, 445), (388, 905), (CX + 570, 905), (CX + 570, 840)], "#1565c0", 1.5)              # B -> ring B
text(386, 292, "B", 11, "bold", "#1565c0")
text(386, 532, "A", 11, "bold")
line([(240, 457), (240, 469)], CTL, 1.3, "4,3")
text(250, 466, "wrapB → OSC A sync", 9.5, fill=CTL)
line([(370, 674), (370, 662)], BUF, 1.8, "2,3")
text(120, 669, "buf → OSC A (src A/AB), OSC B (src B/AB)", 9.5, "bold", BUF)

# =================================================================== OUTPUT
OX = 1250
box(OX, 260, 260, 160, "F3 output tilt (per core)", [
    "a = core A, b = core B; one-pole,",
    "  one shared coefficient",
    "cut < 63: LP 16 kHz → 190 Hz",
    "cut = 63: exact pass",
    "cut > 63: HP → up to ~2 kHz",
    "+ CE2 × ±24 st",
], fill="#e8f5e9", stroke="#2e7d32", tfill="#2e7d32")
pill(OX + 6, 438, "cut · kat_out_cut (bipolar)")
line([(ax, ay), (1236, ay), (1236, 320), (OX, 320)])
line([(bx, by), (1243, by), (1243, 345), (OX, 345)])
box(OX, 450, 260, 230, "Stereo matrix (rotation + width)", [
    "pA = −cos 2π·rotPh; core B opposite",
    "gL = √((1−p)/2), gR = √((1+p)/2)",
    "base: L0 = gL·a + gR·b",
    "      R0 = k·gR·a + √(1−k²gR²)·b",
    "wid 0: k = −1 (no collapse)",
    "wid −12: k = +1 (P4 law, collapses)",
    "wid < −12: narrows to mono at −63",
    "wid > 0: M/S side × up to 2",
    "× CE1 gain × 1.49 norm; ramped",
    "→ F3 tone (ton, after it, S105 P1 A5)",
], fill="#e3f2fd", stroke="#1565c0", tfill="#1565c0")
line([(OX + 130, 420), (OX + 130, 450)])
pill(OX + 6, 700, "wid · kat_width (bipolar)")
pill(OX + 160, 700, "rot · kat_rotate")
box(OX, 725, 260, 95, "Mixer (platform)", [
    "L = buf, R = right[] → the slot's stereo",
    "vol (instrument_vol), pan (instrument_pan)",
    "→ master / FX bus",
], fill="#f3f3f3")
line([(OX + 130, 680), (OX + 130, 725)])
pill(OX + 6, 838, "vol · instrument_vol")
pill(OX + 140, 838, "pan · instrument_pan")
text(OX, 880, "◀ rot, CE1 (chs), wid feed the matrix; the drive is in the cores", 10.5, fill=MOD)
text(OX, 896, "◀ the slow envelope (ce[1]) feeds the band and F3", 10.5, fill=MOD)

# =================================================================== MODULATORS (bottom band)
MY = 1215
text(20, MY - 10, "MODULATORS (per block) — their destinations are written into the blocks above", 15, "bold", MOD)
box(20, MY, 470, 190, "Patch LFO (the patch's unstable LFO)", [
    "rate = lfr (0.02..20 Hz log) × 2^(lfm·(1.5·ringPrev + inv) + jitter)",
    "  ringPrev = mean |D| of both cores (the loop's level)",
    "  jitter: chs × 0.1 oct, redrawn each LFO cycle",
    "shape: sine, phase-warped by w = 0.5 + 0.45·lfm·inv",
    "cv = shape × lfl,  inv = −cv",
    ("destinations ×lfl AGAIN (depth ∝ lfl²): OSC B pos +cv·lfl·24,", WARN),
    ("  OSC B fine +cv·lfl·0.3 st, F1 +cv·lfl·12 st, tube +0.15·cv·lfl, fb +0.2·inv·lfl", WARN),
    "destinations ×lfl once: VCA g1 offset .42 + cv, g2 offset .42 + inv; own rate",
], fill="#f1f8e9", stroke=MOD, tfill=MOD, lsize=10)
pill(26, MY + 208, "rte · kat_plfo_rate")
pill(156, MY + 208, "slf · kat_plfo_mod")
pill(290, MY + 208, "dpt · kat_plfo_level")
circ(472, MY + 14, 9, "#fff", WARN, "!", 12)

box(505, MY, 410, 190, "Chaos: one fast envelope, one slow (free-running)", [
    "cyc: both rates (10 ms..60 s, faster at the top), slow = N x fast",
    "chs: depths (fast d_f = x^2, slow d_s = x(2−x)); never reset",
    "skw: the rise pair, opposite / rotated; imd: the relationship",
    "fast -> output gain 1 − d_f + d_f·L (the matrix)",
    "slow -> the band 48·d_s·(L−0.5), F3 24·d_s·(L−0.5)",
], fill="#f1f8e9", stroke=MOD, tfill=MOD)
pill(511, MY + 208, "chs cyc · kat_chaos / kat_chaos_cycle")
pill(700, MY + 208, "skw imd · kat_chaos_skew / kat_chaos_imod")

box(930, MY, 270, 90, "Rotation LFO (S103 P4)", [
    "rot 0 holds; 1..127 = 0.01..4 Hz log",
    "chs jitter ±0.5 oct per turn",
    "→ stereo matrix (rotPh)",
], fill="#f1f8e9", stroke=MOD, tfill=MOD)
box(930, MY + 100, 270, 90, "Delay-time law (per core)", [
    "T = tim × 2^(−dtk·OSC-B offset in oct)",
    "  × (1 ± 0.12·spr⁺) × (1 + drift)",
    "drift: ±1.5 %·drf per 16 blocks, slewed",
    "core B: T / 2 in half-rate samples",
], fill="#eef4ff", stroke=CTL, tfill=CTL, lsize=10)

box(1215, MY, 295, 190, "Platform modulation (every instrument)", [
    "LFO page (unchanged): rate, sync, wave, offset,",
    "  retrigger, polarity, two amounts, two",
    "  destinations — any MODULATABLE Katran row",
    "  (coa / fin through the pitch multiplier)",
    "Velocity: dst / amt → one row; velocity also",
    "  scales the kick burst (kE)",
    "Morph, step automation, MIDI: every row flagged",
    "  MORPHABLE / AUTOMATABLE",
], fill="#f1f8e9", stroke=MOD, tfill=MOD, lsize=10)
pill(1221, MY + 208, "lfo_* · 12 rows")
pill(1330, MY + 208, "velo_mod_* · dst amt")

# =================================================================== PATCH
PX, PY = 1810, 100
text(PX, 84, "The source patch (AIRA Modular, TORCIDO_Noise_Synth) — numbered controls ↔ Katran rows", 15, "bold")
with open(f"{ROOT}/knowledge_files/drafts/patch.png", "rb") as fh:
    b64 = base64.b64encode(fh.read()).decode()
add(f'<image x="{PX}" y="{PY}" width="600" height="823" xlink:href="data:image/png;base64,{b64}"'
    f'/>')

marks = [
    (1, 75, 125, "Ring ON/OFF", "lvB 0 = ring off (carrier passes, 1-block xfade)  [GRF6]"),
    (2, 50, 210, "Ring IN (carrier)", "t1 = TORCIDO ch 1 out of each core"),
    (3, 100, 210, "Ring MOD IN", "B = OSC B"),
    (4, 75, 340, "Ring OUT", "r → both outputs (+AMP1); ringPrev → LFO rate (lfm)"),
    (5, 255, 117, "SQR range", "OSC B coa (kat_b_coarse, range+coarse in one row)"),
    (6, 195, 185, "SQR COLOR", "OSC B pos (kat_b_pos)  + LFO cv·lfl·24"),
    (7, 255, 185, "SQR FINE", "OSC B fin (kat_b_fine)  + LFO inv·lfl·0.3 st"),
    (8, 195, 305, "SQR LEVEL", "lvB (kat_b_level): ring depth"),
    (9, 255, 305, "SQR SYNC OUT", "OSC B wrap → OSC A sync (weight P = 0.5, fixed)"),
    (10, 195, 357, "SQR CV IN", "GRF2 → OSC B coa, and dtk (delay follows)"),
    (11, 385, 125, "LFO WAVE", "folded into mod (kat_plfo_mod): sine, warped"),
    (12, 335, 165, "LFO MOD IN", "mod (kat_plfo_mod): self / audio rate mod"),
    (13, 395, 205, "LFO RATE", "frq (kat_plfo_rate) 0.02..20 Hz"),
    (14, 350, 360, "LFO OUTPUT", "dpt (kat_plfo_level)  ! applied twice (∝ lfl²)"),
    (15, 395, 340, "LFO CV / INV OUT", "cv, inv → see Patch LFO box"),
    (16, 545, 130, "AMP1", "bal (kat_vca) → g1: core B = ring × (1+g1)"),
    (17, 485, 160, "AMP1 OFFSET", "fixed 0.42 (+ cv)"),
    (18, 545, 295, "AMP2", "bal (kat_vca) → g2: core A = ring + g2·loop"),
    (19, 485, 322, "AMP2 OFFSET", "fixed 0.42 (+ inv)"),
    (20, 105, 465, "SAW range", "OSC A coa (osc1_pitch_coarse)  [GRF1]"),
    (21, 45, 460, "SAW SYNC TRIG IN", "snc (kat_sync): threshold on D + SQR wrap"),
    (22, 45, 530, "SAW COLOR", "OSC A pos (osc1_position)  [GRF5]"),
    (23, 105, 530, "SAW FINE", "OSC A fin (osc1_pitch_fine)"),
    (24, 45, 645, "SAW LEVEL", "lvA (kat_level_a): exciter level into ch 2"),
    (25, 257, 465, "TORCIDO DIST", "drv (kat_drive) ×1..×32, both channels"),
    (26, 340, 467, "TORCIDO TONE", "the band: cut / wid / ofs (kat_flt_*)  [GRF3]"),
    (27, 195, 500, "LO BOOST", "not carried (F2 lct low-cut instead)"),
    (28, 407, 500, "BYPASS", "not carried"),
    (29, 300, 570, "LEVEL ×4", "fixed: LV1 60/127 (ch 1), LV2 64/127 (ch 2)"),
    (30, 260, 672, "TUBE WARM", "follows drv: 0.25·drv' (+ LFO cv·lfl·0.15)"),
    (31, 340, 672, "TORCIDO DRY/WET", "bln (kat_wet)  [GRF4]"),
    (32, 545, 465, "DELAY TIME", "tim (kat_time) 32..1792 smp (0.7..41 ms)"),
    (33, 545, 542, "DELAY FEEDBACK", "fbk (kat_feedback) 0..0.95 + LFO + kick swell"),
    (34, 545, 620, "DELAY DRY/WET", "fixed 50/50 (timed tap + 1-block dry tap)"),
    (35, 167, 767, "GRF 1", "→ OSC A coa"),
    (36, 220, 767, "GRF 2", "→ OSC B coa + dtk"),
    (37, 272, 767, "GRF 3", "→ frq (F1)"),
    (38, 325, 767, "GRF 4", "→ bln"),
    (39, 380, 767, "GRF 5", "→ pos (OSC A)"),
    (40, 431, 767, "GRF 6", "→ lvB (ring on/off)"),
    (41, 497, 767, "OUTPUT 1", "core A (AMP2 side) — now rotated by rot / wid"),
    (42, 550, 767, "OUTPUT 2", "core B (AMP1 side) — now rotated by rot / wid"),
]
for n, px, py, _, _ in marks:
    circ(PX + px, PY + py, 10, "#ffeb3b", "#111", str(n), 10)

TX = 2425
text(TX, PY + 4, "#", 12, "bold")
text(TX + 24, PY + 4, "patch control", 12, "bold")
text(TX + 150, PY + 4, "Katran row / law", 12, "bold")
ty = PY + 22
for n, _, _, ctl, kat in marks:
    circ(TX + 9, ty - 4, 9, "#ffeb3b", "#111", str(n), 9.5)
    text(TX + 24, ty, ctl, 10.5, "bold")
    text(TX + 150, ty, kat, 10.5)
    ty += 19

# Katran-only rows
KY = 940
box(1810, KY, 1170, 120, "Katran additions with no patch control", [
    "run, kik  ·  chaos: chs cyc skw imd  ·  typ/wav per oscillator (buf is a type)  ·  dmp (buffer)  ·  noi (noise)",
    "dtn (detune + pos spread)  ·  wow (drift)  ·  spr (times / xfb)  ·  dtk (tracks OSC B)  ·  the band cut wid ofs",
    "r/f (ring or loop PM per core), sdb (Osc 2 feedback)  ·  ton (F3 after the matrix), drv (voice drive)  ·  wid, rot  ·  vol, pan, lfo_*, velo_mod_*",
    "Second core (B): half rate, its own OSC A copy (detuned), its own delay ring and loop — the patch had one loop.",
    "Fixed by design (no row): LV1/LV2, AMP offsets 0.42, the delay dry/wet 50/50, sync weight P 0.5, kick decay 80 ms,",
    "  burst targets (0.8 exc, 0.5 nse, 0.3 fb), tube = 0.25·drv, output norm 0.5, guard 0.7, DC block 10 Hz.",
], fill="#ffffff", stroke="#555")

RY = 1075
box(1810, RY, 1170, 230, "Review flags (details and suggestions: KATRAN_BLOCK_DIAGRAM_SVG.md §6)", [
    ("1  Patch-LFO depth lfl is applied twice for 5 of its 7 destinations (cv already contains lfl): depth ∝ lfl²;"
     " at init (51) ≈ 0.16 of full.", WARN),
    ("2  src (off/A/B/AB) is a separate selector; a 'buf' oscillator type would be clearer — needs a per-oscillator type for 'one or both'.", WARN),
    ("3  dmp: 0 = open, 1 = 20 Hz low-pass, 127 = 300 Hz — the darkest setting is next to 'open', and the top is still dark.", WARN),
    ("4  OSC B coarse spans −48..+79 semitones (bCoarse − 48), far wider than useful; the plan said −36..+24.", WARN),
    ("5  The transport-stop decay uses T(126) = 60 s, so stopping lets the drone ring for about a minute.", WARN),
    ("6  dtn couples A's detune (to ±25 cents, squared law) and its pos spread (±16 steps): two effects on one knob.", WARN),
    ("7  CE2 moves F1 by ±48 semitones at full depth — four octaves; most of the range is extreme.", WARN),
    ("8  bal (vca) mixes two products with 0.42 offsets; its centre is the patch balance, but its ends are not obvious levers.", WARN),
    ("9  Several fixed laws hide levers the patch had (AMP offsets, TORCIDO levels, delay dry/wet, tube vs drive).", WARN),
    ("10 Page order: OSC A's controls on osc, OSC B and the exciter on fm, the patch LFO on mod, the delay on click, the", WARN),
    ("    loop filter on filter, run/kick + both CEs on aeg — the signal chain is split across six pages.", WARN),
    "",
    "Labels: short label · key. Ranges are the code's laws (KatranVoice.c, KatranTables.c, S103 P5 tree).",
], fill="#fff5f5", stroke=WARN, tfill=WARN)

add("</svg>")
with open(f"{ROOT}/katran_block_diagram.svg", "w") as fh:
    fh.write("\n".join(out))
print("ok", sum(len(s) for s in out))
