#!/usr/bin/env python3
"""Write katran_block_diagram_p2.svg: Katran as discrete modules and cables.

What:       page 2 of the Katran block diagram. The two cores are collapsed to
            their essential modules (delay, TORCIDO 1 / 2, the band, the loop
            write, ring, VCA); the oscillators, the noise, the buffer read and
            the output are their own modules; every internal modulator (the
            patch LFO, the rotation LFO, CE1, CE2, the kick envelope, the run
            gain, the delay drift, the loop-level follower, the chaos source)
            is its own module with one coloured bus, dropped onto labelled
            input jacks. A cable list gives each connection's amount and row.
            The platform LFO and velocity (system modulation) are left out.
Why:        the S103 parameter review (your request, 2026-11-25): which
            modulator moves what, without the per-sample detail of page 1
            (katran_block_diagram.svg, tools/katran/make_block_diagram.py).
Inputs:     none; the laws are transcribed from KatranVoice.c (S103 P5 tree).
Outputs:    katran_block_diagram_p2.svg in the repository root.
Accessors:  python3 tools/katran/make_block_diagram_p2.py
"""
import html
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
W, H = 2420, 1080
o = []
E = html.escape


def add(s):
    o.append(s)


def text(x, y, s, size=11, weight="normal", fill="#1d1d1f", anchor="start"):
    add(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}">{E(s)}</text>')


def rect(x, y, w, h, fill="#fff", stroke="#333", sw=1.4, rx=6, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{sw}"{d}/>')


def module(x, y, w, h, title, col, lines=(), lsize=10.5):
    rect(x, y, w, h, "#ffffff", col, 2, 8)
    add(f'<rect x="{x}" y="{y}" width="{w}" height="22" rx="8" fill="{col}"/>')
    add(f'<rect x="{x}" y="{y + 12}" width="{w}" height="10" fill="{col}"/>')
    text(x + 8, y + 16, title, 12.5, "bold", "#fff")
    yy = y + 38
    for ln in lines:
        s, c = (ln, "#333") if isinstance(ln, str) else ln
        text(x + 8, yy, s, lsize, "normal", c)
        yy += lsize + 4


def mini(x, y, w, h, title, sub="", col="#555", fill="#f7f7f7"):
    rect(x, y, w, h, fill, col, 1.4, 5)
    text(x + w / 2, y + (h / 2 if not sub else h / 2 - 3), title, 11, "bold", col, "middle")
    if sub:
        text(x + w / 2, y + h / 2 + 11, sub, 9.5, "normal", "#444", "middle")


def path(pts, col="#111", w=1.8, dash=None, arrow=True):
    d = " ".join(f"{x},{y}" for x, y in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    mk = f' marker-end="url(#a{col[1:]})"' if arrow else ""
    add(f'<polyline points="{d}" fill="none" stroke="{col}" stroke-width="{w}"{da}{mk}/>')


def jack(x, y, col, label="", above=True, out=False):
    add(f'<circle cx="{x}" cy="{y}" r="5.5" fill="{"#fff" if not out else col}" stroke="{col}" stroke-width="2"/>')
    if label:
        text(x, y - 9 if above else y + 18, label, 9.5, "bold", col, "middle")


# colours (one per modulator bus)
C_LFO, C_CE2, C_KICK, C_RUN, C_DRIFT = "#2e7d32", "#00897b", "#ef6c00", "#c62828", "#6d4c41"
C_DTK, C_CE1, C_ROT, C_LVL, C_CHS = "#1e88e5", "#283593", "#8e24aa", "#616161", "#d81b60"
C_AUD, C_B, C_BUF, C_SYNC = "#111111", "#1565c0", "#6a1b9a", "#0277bd"
COLS = (C_LFO, C_CE2, C_KICK, C_RUN, C_DRIFT, C_DTK, C_CE1, C_ROT, C_LVL, C_CHS, C_AUD, C_B, C_BUF,
        C_SYNC, "#555555")

add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
    f'font-family="Helvetica, Arial, sans-serif">')
add('<defs>')
for c in COLS:
    add(f'<marker id="a{c[1:]}" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">'
        f'<path d="M0,0 L8,4 L0,8 z" fill="{c}"/></marker>')
add('</defs>')
add(f'<rect width="{W}" height="{H}" fill="#fafafa"/>')
text(20, 30, "Katran — page 2: modules and modulation cables (S103 P5 tree)", 21, "bold")
text(20, 50, "Cores collapsed to their essential modules. Internal modulators are their own modules; each has one coloured "
     "bus, dropped onto labelled input jacks (○). Platform LFO / velocity not shown. Amounts: the cable list (right).",
     11.5, fill="#444")

# =============================================================== AUDIO SOURCES (left column)
LX, LW = 40, 290
module(LX, 120, LW, 95, "BUFFER READ  (typ = buf, dmp)", C_BUF, [
    "DTCM FxBuffer arena, raw int16, 1,024-smp",
    "window; position = the osc's pos, speed =",
    "its pitch; DC block → dmp LP → guard 0.7",
])
module(LX, 228, LW, 55, "NOISE  (hardware RNG)", "#555555", ["one value per sample (core B: per half-rate)"])
module(LX, 296, LW, 112, "OSC 1 — exciter  (copies 1a, 1b)", C_AUD, [
    "pitch: note + coa + fin; 1a / 1b ∓dtn detune",
    "form: typ / wav / pos (shared) — or buffer",
    "1a → core A (full rate) · 1b → core B (half)",
    "hard sync in: Osc 2 wrap + own core's D > snc",
])
module(LX, 420, LW, 80, "OSC 2 — ring modulator (one, shared)", C_B, [
    "pitch: Osc 1's + coa(2) + fin(2)",
    "form: shared typ / wav, own pos(2)",
])
# buffer -> oscillators
path([(LX, 170), (LX - 14, 170), (LX - 14, 340), (LX, 340)], C_BUF, 1.8, "3,3")
path([(LX - 14, 340), (LX - 14, 460), (LX, 460)], C_BUF, 1.8, "3,3")
text(LX - 18, 255, "src", 10, "bold", C_BUF, "end")
# OSC B wrap -> OSC A sync
path([(LX + 250, 420), (LX + 250, 408)], C_SYNC, 1.4, "4,3")
text(LX + 130, 416, "Osc 2 wrap → Osc 1 sync", 9.5, "bold", C_SYNC)
# OSC B jacks (bottom)
jack(110, 500, C_LFO, "pos")
jack(170, 500, C_LFO, "pitch")
jack(260, 500, C_DTK, "pitch▾", out=True)

# =============================================================== CORES


def core(X0, name, rate, vca, osc1):
    Y0 = 120
    rect(X0, Y0, 430, 380, "#ffffff", "#777", 2, 10, "8,4")
    text(X0 + 10, Y0 + 20, name, 14, "bold")
    text(X0 + 10, Y0 + 34, rate, 9.5, fill="#555")
    # top ports
    jack(X0 + 50, Y0, C_AUD, osc1)
    jack(X0 + 112, Y0, "#555555", "noise")
    jack(X0 + 200, Y0, C_B, "Osc 2")
    # row 1: ring -> vca -> out
    mini(X0 + 150, Y0 + 45, 100, 45, "RING", "t1 × Osc 2 (lvB)", C_B, "#e3f2fd")
    mini(X0 + 280, Y0 + 45, 100, 45, vca[0], vca[1], C_B, "#e3f2fd")
    path([(X0 + 200, Y0 + 6), (X0 + 200, Y0 + 45)], C_B, 1.6)
    path([(X0 + 250, Y0 + 67), (X0 + 280, Y0 + 67)])
    path([(X0 + 380, Y0 + 67), (X0 + 424, Y0 + 67)])
    jack(X0 + 424, Y0 + 67, C_AUD, "", out=True)
    text(X0 + 400, Y0 + 104, "out", 10, "bold", anchor="middle")
    # row 2: exciter mix -> torcido 2
    mini(X0 + 20, Y0 + 130, 105, 50, "EXCITER MIX", "lvA level · noi mix", "#555", "#f3f3f3")
    mini(X0 + 150, Y0 + 130, 100, 50, "TORCIDO 2", "clip (drv)", "#ef6c00", "#fff8e6")
    path([(X0 + 50, Y0 + 6), (X0 + 50, Y0 + 130)], C_AUD, 1.6)
    path([(X0 + 112, Y0 + 6), (X0 + 112, Y0 + 130)], "#555555", 1.3, "3,3")
    path([(X0 + 125, Y0 + 155), (X0 + 150, Y0 + 155)])
    # row 3: delay -> torcido 1 -> + -> F1 -> F2
    mini(X0 + 20, Y0 + 215, 105, 50, "DELAY", "tim (0.7–41 ms)", "#ad1457", "#fde7ef")
    mini(X0 + 150, Y0 + 215, 100, 50, "TORCIDO 1", "drv tube bln", "#ef6c00", "#fff8e6")
    add(f'<circle cx="{X0 + 280}" cy="{Y0 + 240}" r="11" fill="#fff" stroke="#111" stroke-width="1.4"/>')
    text(X0 + 280, Y0 + 245, "+", 14, "bold", anchor="middle")
    mini(X0 + 305, Y0 + 215, 113, 50, "THE BAND", "cut wid ofs", "#2e7d32", "#e8f5e9")
    path([(X0 + 125, Y0 + 240), (X0 + 150, Y0 + 240)])
    path([(X0 + 250, Y0 + 240), (X0 + 269, Y0 + 240)])
    path([(X0 + 250, Y0 + 155), (X0 + 280, Y0 + 155), (X0 + 280, Y0 + 229)])
    path([(X0 + 291, Y0 + 240), (X0 + 305, Y0 + 240)])
    path([(X0 + 360, Y0 + 240), (X0 + 368, Y0 + 240)])
    # row 4: loop write
    mini(X0 + 150, Y0 + 295, 268, 42, "LOOP WRITE", "gr × (u + fb·D + xfb·D_other) → delay", "#333", "#f0f0f0")
    path([(X0 + 393, Y0 + 265), (X0 + 393, Y0 + 295)])
    path([(X0 + 150, Y0 + 316), (X0 + 72, Y0 + 316), (X0 + 72, Y0 + 265)])
    text(X0 + 20, Y0 + 300, "loop", 10, "bold", "#ad1457")
    # t1 -> ring and raw t1 -> vca
    path([(X0 + 250, Y0 + 228), (X0 + 262, Y0 + 228), (X0 + 262, Y0 + 112), (X0 + 238, Y0 + 112),
          (X0 + 238, Y0 + 90)], C_AUD, 1.4)
    path([(X0 + 262, Y0 + 112), (X0 + 300, Y0 + 112), (X0 + 300, Y0 + 90)], C_AUD, 1.4)
    text(X0 + 266, Y0 + 125, "t1", 9.5, "bold")
    # D -> sync note
    text(X0 + 10, Y0 + 362, "D → own Osc 1 copy's sync (snc); D → other core (xfb)", 9.5, fill=C_SYNC)
    # bottom jacks
    js = {}
    for key, dx, col, lab, out in (("T", 30, C_DRIFT, "T", False), ("F1", 85, C_LFO, "F1", False),
                                   ("tube", 140, C_LFO, "tube", False), ("fb", 195, C_LFO, "fb", False),
                                   ("exc", 250, C_KICK, "exc/nse", False), ("gr", 305, C_RUN, "gr", False),
                                   ("vca", 360, C_LFO, "VCA", False), ("D", 410, C_LVL, "D▾", True)):
        jack(X0 + dx, 500, "#333", lab, True, out)
        js[key] = X0 + dx
    return js


JA = core(470, "CORE A", "full rate (32 smp / block)", ("AMP2", "+ g2·t1 (bal)"), "Osc 1a")
JB = core(940, "CORE B", "half rate (16 smp / block, fs/2)", ("AMP1", "× (1 + g1) (bal)"), "Osc 1b")
# xfb between the cores
path([(900, 360), (940, 360)], C_SYNC, 1.4, "4,3")
path([(940, 372), (900, 372)], C_SYNC, 1.4, "4,3")
text(920, 352, "xfb", 9.5, "bold", C_SYNC, "middle")
text(920, 392, "spr<63", 9.5, "bold", C_SYNC, "middle")

# =============================================================== OUTPUT
OX = 1440
module(OX, 120, 260, 380, "OUTPUT", "#37474f", [
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "to the mixer: vol, pan (system)",
])
mini(OX + 30, 175, 200, 50, "F3 TILT (per core)", "cut: LP ← 63 → HP", "#2e7d32", "#e8f5e9")
mini(OX + 30, 260, 200, 90, "", "", "#1565c0", "#e3f2fd")
text(OX + 130, 290, "STEREO MATRIX", 11, "bold", "#1565c0", "middle")
text(OX + 130, 326, "rotate (rot) · width (wid)", 10, anchor="middle")
text(OX + 130, 341, "level × CE1 · norm 0.5", 10, anchor="middle")
path([(OX + 130, 225), (OX + 130, 260)])
path([(OX + 230, 300), (OX + 254, 300)])
text(OX + 256, 296, "L", 11, "bold")
text(OX + 256, 312, "R", 11, "bold")
jack(OX + 40, 500, "#333", "F3")
jack(OX + 130, 500, "#333", "level")
jack(OX + 215, 500, "#333", "rot")
# core outputs -> output
path([(894, 187), (912, 187), (912, 72), (OX + 60, 72), (OX + 60, 175)], C_AUD, 1.8)
text(OX + 66, 166, "a", 11, "bold")
path([(1364, 187), (OX + 30, 187)], C_AUD, 1.8)
text(OX + 18, 182, "b", 11, "bold")

# =============================================================== top-channel audio routing
# noise
path([(LX + LW, 255), (350, 255), (350, 80), (JA["T"] - 30 + 112, 80), (JA["T"] - 30 + 112, 114)],
     "#555555", 1.3, "3,3")
path([(JA["T"] - 30 + 112, 80), (JB["T"] - 30 + 112, 80), (JB["T"] - 30 + 112, 114)], "#555555", 1.3, "3,3")
# OSC A copy a -> core A, copy b -> core B
path([(LX + LW, 340), (372, 340), (372, 96), (520, 96), (520, 114)], C_AUD, 1.6)
text(376, 335, "1a", 10, "bold")
path([(LX + LW, 380), (394, 380), (394, 88), (990, 88), (990, 114)], C_AUD, 1.6, "6,3")
text(398, 375, "1b", 10, "bold")
# OSC B -> both rings
path([(LX + LW, 460), (416, 460), (416, 104), (670, 104), (670, 114)], C_B, 1.8)
path([(670, 104), (1140, 104), (1140, 114)], C_B, 1.8)

# =============================================================== MODULATORS
MY, MW, MG = 730, 168, 18
mods = [
    ("PATCH LFO", C_LFO, ["rate frq · self-mod mod", "depth dpt (cv, inv = −cv)",
                          "sine, warped by mod·inv", "rate ← loop level, chaos"]),
    ("LOOP LEVEL", C_LVL, ["envelope follower:", "mean |D| of both cores,", "last sample each block",
                            "(no row)"]),
    ("CE1", C_CE1, ["cycling rise / fall", "cycle cy1 · skew sk1", "depth dp1",
                     "free-running"]),
    ("CE2", C_CE2, ["cycling rise / fall", "cycle cy2 · skew sk2", "depth dp2 (bipolar)",
                     "free-running"]),
    ("KICK ENV", C_KICK, ["kE = velocity at a", "trigger, −60 dB in 80 ms", "amount kik:",
                           "burst (0–63), + swell"]),
    ("RUN GAIN", C_RUN, ["gr: run 127 = on;", "0–126 decay 0.25–60 s", "re-armed by a kick;",
                          "Stop → 60 s decay"]),
    ("DELAY DRIFT", C_DRIFT, ["random walk per core", "±1.5 % × wow,", "new target / 16 blocks",
                               "slewed"]),
    ("ROTATION LFO", C_ROT, ["phase ramp", "rot 0 hold; 0.01–4 Hz", "core A / B opposite",
                              "jittered by chaos"]),
    ("CHAOS", C_CHS, ["random draws (chs)", "per cycle / per turn", "and loop-coupled",
                       "rise (> 64)"]),
]
mx = {}
for i, (t, c, ls) in enumerate(mods):
    x = 40 + i * (MW + MG)
    module(x, MY, MW, 150, t, c, ls, 10)
    mx[t] = x + MW / 2
    if t not in ("LOOP LEVEL", "CHAOS"):
        jack(x + MW / 2, MY, c, "", out=True)

# lanes (y) and drops: (lane y, colour, source x, [(target x, target y)])
LANE = {C_LFO: 525, C_CE2: 545, C_KICK: 565, C_RUN: 585, C_DRIFT: 605, C_DTK: 625, C_CE1: 645,
        C_ROT: 665, C_LVL: 690}


def bus(col, src_x, src_y, targets, up=True):
    y = LANE[col]
    xs = [t[0] for t in targets] + [src_x]
    path([(min(xs), y), (max(xs), y)], col, 2.2, arrow=False)
    path([(src_x, src_y), (src_x, y)], col, 2.2, arrow=False)
    for tx, ty in targets:
        path([(tx, y), (tx, ty + (6 if up else -6))], col, 1.8)


bus(C_LFO, mx["PATCH LFO"], MY, [(105, 500), (170, 500)] +
    [(j[k] - 5, 500) for j in (JA, JB) for k in ("F1", "tube", "fb", "vca")])
bus(C_CE2, mx["CE2"], MY, [(JA["F1"] + 5, 500), (JB["F1"] + 5, 500), (OX + 40, 500)])
bus(C_KICK, mx["KICK ENV"], MY, [(j[k] + (5 if k == "fb" else 0), 500) for j in (JA, JB) for k in ("fb", "exc")])
bus(C_RUN, mx["RUN GAIN"], MY, [(JA["gr"], 500), (JB["gr"], 500)])
bus(C_DRIFT, mx["DELAY DRIFT"], MY, [(JA["T"] - 5, 500), (JB["T"] - 5, 500)])
# OSC B pitch -> delay T (dtk)
y = LANE[C_DTK]
path([(260, 506), (260, y), (JB["T"] + 5, y)], C_DTK, 2.2, arrow=False)
for tx in (JA["T"] + 5, JB["T"] + 5):
    path([(tx, y), (tx, 506)], C_DTK, 1.8)
text(270, y - 4, "dtk", 10, "bold", C_DTK)
bus(C_CE1, mx["CE1"], MY, [(OX + 130, 500)])
bus(C_ROT, mx["ROTATION LFO"], MY, [(OX + 215, 500)])
# loop level: from the cores' D jacks down to the follower
y = LANE[C_LVL]
path([(JA["D"], 506), (JA["D"], y), (JB["D"], y)], C_LVL, 2.2, arrow=False)
path([(JB["D"], 506), (JB["D"], y)], C_LVL, 2.2, arrow=False)
path([(JA["D"], y), (mx["LOOP LEVEL"], y), (mx["LOOP LEVEL"], MY - 6)], C_LVL, 2.2)
jack(mx["LOOP LEVEL"], MY, C_LVL, "")
# bottom lanes: loop level -> LFO rate, CE rise; chaos -> LFO, CE1, CE2, rotation; inputs on module bottoms
BY = MY + 150
for xx in (mx["PATCH LFO"] - 30, mx["CE1"] - 30, mx["CE2"] - 30):
    jack(xx, BY, C_LVL, "", above=False)
for xx in (mx["PATCH LFO"] + 30, mx["CE1"] + 30, mx["CE2"] + 30, mx["ROTATION LFO"]):
    jack(xx, BY, C_CHS, "", above=False)
jack(mx["LOOP LEVEL"], BY, C_LVL, "", above=False, out=True)
jack(mx["CHAOS"], BY, C_CHS, "", above=False, out=True)
y1 = BY + 22
path([(mx["LOOP LEVEL"], BY + 6), (mx["LOOP LEVEL"], y1), (mx["CE2"] - 30, y1)], C_LVL, 2, arrow=False)
path([(mx["PATCH LFO"] - 30, y1), (mx["LOOP LEVEL"], y1)], C_LVL, 2, arrow=False)
for xx in (mx["PATCH LFO"] - 30, mx["CE1"] - 30, mx["CE2"] - 30):
    path([(xx, y1), (xx, BY + 7)], C_LVL, 1.6)
text(mx["LOOP LEVEL"] + 6, y1 + 14, "level → LFO rate (×mod), CE rise (chs > 64)", 9.5, "bold", C_LVL)
y2 = BY + 52
path([(mx["CHAOS"], BY + 6), (mx["CHAOS"], y2), (mx["PATCH LFO"] + 30, y2)], C_CHS, 2, arrow=False)
for xx in (mx["PATCH LFO"] + 30, mx["CE1"] + 30, mx["CE2"] + 30, mx["ROTATION LFO"]):
    path([(xx, y2), (xx, BY + 7)], C_CHS, 1.6)
text(mx["CE2"] + 40, y2 + 14, "chaos → LFO rate jitter, CE1 / CE2 cycle + peak, rotation rate", 9.5, "bold", C_CHS)
# trigger / transport
TY = BY + 85
module(mx["KICK ENV"] - 84, TY, 168, 48, "TRIGGER", "#455a64", ["step · pad · MIDI · chromatic"], 9.5)
module(mx["RUN GAIN"] - 84, TY, 168, 48, "TRANSPORT", "#455a64", ["start re-arms · stop decays"], 9.5)
path([(mx["KICK ENV"], TY), (mx["KICK ENV"], BY + 6)], "#555555", 1.6)
path([(mx["KICK ENV"] + 40, TY), (mx["KICK ENV"] + 40, TY - 10), (mx["RUN GAIN"] - 40, TY - 10),
      (mx["RUN GAIN"] - 40, BY + 6)], "#555555", 1.6)
path([(mx["RUN GAIN"], TY), (mx["RUN GAIN"], BY + 6)], "#555555", 1.6)
text(mx["KICK ENV"] - 84, TY + 64, "the trigger also sets the note (OSC pitch)", 9.5, fill="#555")

# =============================================================== CABLE LIST
TX = 1760
text(TX, 92, "Cable list — internal modulation (amount = the code's law)", 14, "bold")
rows = [
    (C_LFO, "PATCH LFO", "Osc 2 pos", "+ cv·lfl·24 steps", "dpt *"),
    (C_LFO, "", "Osc 2 pitch", "+ cv·lfl·0.3 semitone", "dpt *"),
    (C_LFO, "", "F1 cutoff (A, B)", "+ cv·lfl·12 semitones", "dpt *"),
    (C_LFO, "", "TORCIDO tube (A, B)", "+ 0.15·cv·lfl", "dpt *"),
    (C_LFO, "", "feedback (A, B)", "+ 0.2·inv·lfl", "dpt *"),
    (C_LFO, "", "VCA A (AMP2) / B (AMP1)", "offset 0.42 + inv / 0.42 + cv", "dpt"),
    (C_LFO, "", "its own rate, shape", "× 2^(mod·inv), warp 0.45·mod·inv", "mod"),
    (C_CE1, "CE1", "output level", "1 − dp1 + dp1·CE1", "dp1"),
    (C_CE2, "slow env", "the band (A, B)", "± 48·d_s·(L−0.5) semitones", "chs"),
    (C_CE2, "", "F3 tilt", "± 24 semitones × dp2", "dp2"),
    (C_KICK, "KICK ENV", "exciter level (A, B)", "+ burst·kE·0.8", "kik"),
    (C_KICK, "", "noise mix (A, B)", "+ burst·kE·0.5", "kik"),
    (C_KICK, "", "feedback (A, B)", "+ swell·kE·0.3 (kik > 63)", "kik"),
    (C_RUN, "RUN GAIN", "loop write (A, B)", "× gr (0..1)", "run"),
    (C_DRIFT, "DELAY DRIFT", "delay time (A, B)", "× (1 + d), |d| ≤ 1.5 %·wow", "wow"),
    (C_DTK, "Osc 2 pitch", "delay time (A, B)", "× 2^(−dtk' · Osc 2 offset in oct)", "dtk"),
    (C_ROT, "ROTATION LFO", "stereo matrix", "pan A = −cos 2πφ, B opposite", "rot"),
    (C_LVL, "LOOP LEVEL", "patch LFO rate", "× 2^(mod' · 1.5 · level)", "mod"),
    (C_LVL, "", "CE1 / CE2 rise", "× (1 + 2(chs' − .5)·min(1, 4·level))", "chs > 64"),
    (C_CHS, "CHAOS", "CE1 / CE2 cycle, peak", "× 2^(±chs'), − chs'·|r|/2", "chs"),
    (C_CHS, "", "patch LFO rate", "± 0.1·chs' octave per cycle", "chs"),
    (C_CHS, "", "rotation rate", "± 0.5·chs' octave per turn", "chs"),
    (C_SYNC, "Osc 2 wrap", "Osc 1a, 1b sync", "+ 0.5 FS into the test", "(fixed)"),
    (C_SYNC, "core A / B D", "own copy 1a / 1b sync", "reset when D + wrap > snc·258", "snc"),
    (C_SYNC, "core A ↔ B", "the other core's write", "+ xfb·D, xfb = 0.6·(63−spr)/63", "spr < 63"),
    ("#555555", "TRIGGER", "kick env, run gain, note", "kE = velocity; gr re-armed", "—"),
    ("#555555", "TRANSPORT", "run gain", "start re-arms; stop: 60 s decay", "—"),
]
y = 118
text(TX + 22, y, "from", 10.5, "bold")
text(TX + 130, y, "to", 10.5, "bold")
text(TX + 290, y, "amount", 10.5, "bold")
text(TX + 548, y, "row", 10.5, "bold")
y += 20
for col, src, dst, amt, row in rows:
    add(f'<rect x="{TX}" y="{y - 10}" width="14" height="12" rx="3" fill="{col}"/>')
    text(TX + 22, y, src, 10.5, "bold", col)
    text(TX + 130, y, dst, 10.5)
    text(TX + 290, y, amt, 10.5)
    text(TX + 548, y, row, 10.5, "bold")
    y += 19
text(TX, y + 8, "* dpt (lfl) is applied twice on these five: cv = shape × lfl, then × lfl again (depth ∝ lfl²).",
     10.5, "bold", "#c62828")
text(TX, y + 26, "System modulation not drawn: the platform LFO (lfo_*, 2 destinations, any row) and", 10.5,
     fill="#555")
text(TX, y + 42, "velocity (dst / amt, one row; velocity also sets kE).", 10.5, fill="#555")

# how the cores relate (your question, 2026-11-25)
NY = y + 62
rect(TX, NY, 640, 150, "#fffde7", "#f9a825", 1.6, 6)
text(TX + 10, NY + 20, "How the two cores relate", 12.5, "bold", "#795548")
for i, ln in enumerate([
    "Direct audio coupling: only xfb (each core's D into the other's loop write), and only when",
    "spr < 63 (0.6 at spr 0). At spr ≥ 63 — init.kat is 63 — the cores do not exchange audio.",
    "Shared: Osc 2 (one ring modulator for both), every internal modulator, and the loop-level",
    "follower (averages both cores; drives the LFO rate and the CE rise): indirect, control-rate.",
    "Osc 1a / 1b: one oscillator's two copies (core A / core B), detuned by dtn; each is synced",
    "only by its own core. Code names: OSC A = Osc 1, OSC B = Osc 2 (KatranOsc.c, KatranVoice.c).",
]):
    text(TX + 10, NY + 40 + i * 17, ln, 10.5)

# legend
ly = H - 40
path([(TX, ly), (TX + 40, ly)], C_AUD, 1.8)
text(TX + 46, ly + 4, "audio", 10.5)
path([(TX + 100, ly), (TX + 140, ly)], C_B, 1.8)
text(TX + 146, ly + 4, "Osc 2 (ring)", 10.5)
path([(TX + 240, ly), (TX + 280, ly)], C_BUF, 1.8, "3,3")
text(TX + 286, ly + 4, "buffer", 10.5)
path([(TX + 340, ly), (TX + 380, ly)], C_SYNC, 1.4, "4,3")
text(TX + 386, ly + 4, "sync / coupling", 10.5)
jack(TX + 500, ly, "#333")
text(TX + 512, ly + 4, "input jack", 10.5)
jack(TX + 590, ly, "#333", out=True)
text(TX + 600, ly + 4, "output", 10.5)

add("</svg>")
with open(os.path.join(ROOT, "katran_block_diagram_p2.svg"), "w") as fh:
    fh.write("\n".join(o))
print("ok")
