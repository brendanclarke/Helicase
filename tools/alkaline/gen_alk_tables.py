#!/usr/bin/env python3
"""Generate the Alkaline C tables from the S092 P6 model (S093 P0, P3).

What:       writes, from tools/alkaline/alk_model.py and alk_constants.json:
              Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c
                - the Resonance table of the real-time network in form N1
                  (S093 P1 §5; S093 P3 made it the firmware's): every network
                  state at block rate, one asymptote one-pole per output per
                  sample (u at A_U_HZ, v at A_V_HZ), on ALK_RES_POINTS points:
                  slow block system (Phi 8x8, Gamma 10x2, slow outputs 2x8),
                  asymptote gains and instantaneous gains per point; the N1
                  per-sample and per-block asymptote constants;
                - the TPT prewarp table c(k) (S092 P6 §5.2);
                - the VEG current table and the fitted MEG/VEG/accent
                  constants (S092 P6 §4);
              Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c
                - alkSqrTable[11][1024], the Q8 square per octave level from
                  the P1 SPICE run (S092 P0 D8), band-limited per level like
                  the stock tables.
Why:        the firmware takes these from the circuit model, not from
            hand-copied numbers; --check proves the committed files are the
            generator's output.
Inputs:     alk_model.py, alk_constants.json (fit_spice.py).
Outputs:    the three C files above.
Accessors:  python3 tools/alkaline/gen_alk_tables.py [--check]
Affiliates: S093_P0_ALKALINE_C_FUNCTIONS_CODE.md §3; DiodeLadder.h
            (DiodeLadderNet layout); tools/dsp_test modules/alkaline.c.
"""
import argparse
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import alk_model as M  # noqa: E402

OUT_DIR = os.path.join(ROOT, "Core", "DSP", "Instruments", "Alkaline")
RES_POINTS = 9                      # S092 P6 §5.3: 9 points, LSD +0.016 dB
NET_BLOCK = 64                      # samples per slow-network update (T2-2, S093 P3)
A_U_HZ, A_V_HZ = 50.0, 120.0        # N1 asymptote poles (S093 P1 §5)
NET_STATES = 8                      # block-system states (S093 P3 experiment: --states)
SQ_SCALE_V = 5.4                    # square table: int16 32767 = +5.4 V around
SQ_MID_V = 4.0                      # a 4.0 V mid level (the network is AC)


def f32(x):
    """C float literal, round-trip exact for float32."""
    v = float(np.float32(x))
    if v == 0.0:
        return "0.0f"
    s = "%.9g" % v
    if "e" not in s and "." not in s:
        s += ".0"
    return s + "f"


def n1_net(k):
    """SplitNet in form N1 (S093 P1 §5): no per-sample fast modes."""
    saved = (M.SplitNet.F_SPLIT, M.SplitNet.A_ASYM_U, M.SplitNet.A_ASYM_V)
    M.SplitNet.F_SPLIT = 1e9
    M.SplitNet.A_ASYM_U = 2 * math.pi * A_U_HZ
    M.SplitNet.A_ASYM_V = 2 * math.pi * A_V_HZ
    try:
        return M.SplitNet(k, block=NET_BLOCK)
    finally:
        M.SplitNet.F_SPLIT, M.SplitNet.A_ASYM_U, M.SplitNet.A_ASYM_V = saved


def asym(hz):
    """Per-sample trapezoidal one-pole (al, be) and per-block decay at hz."""
    a, h2 = 2 * math.pi * hz, 0.5 / M.FS
    return (1 - a * h2) / (1 + a * h2), h2 / (1 + a * h2), math.exp(-a * NET_BLOCK / M.FS)


def _lyap(A, Q):
    """A X + X A^T + Q = 0 (small dense, by the Kronecker form)."""
    n = A.shape[0]
    K = np.kron(np.eye(n), A) + np.kron(A, np.eye(n))
    return np.linalg.solve(K, -Q.reshape(-1, order="F")).reshape(n, n, order="F")


def _sqrt_psd(Mx):
    w, V = np.linalg.eigh((Mx + Mx.T) / 2)
    return V @ np.diag(np.sqrt(np.clip(w, 0, None)))


_BASIS = {}


def reduced_basis(r):
    """S093 P3 experiment (--states): one balanced basis for every Res point
    (the trace-normalised Gramians summed over 33 points), so the reduced
    tables stay in a common basis and interpolate between points as the
    physical basis does. Returns (T 8 x r, Ti r x 8)."""
    if r not in _BASIS:
        P = Q = 0
        for k in np.linspace(0, 1, 33):
            sn = n1_net(k)
            A, B, C = sn.Aa[:8, :8], sn.Bs, sn.Ca[:, :8]
            p, q = _lyap(A, B @ B.T), _lyap(A.T, C.T @ C)
            P, Q = P + p / np.trace(p), Q + q / np.trace(q)
        L, Mq = _sqrt_psd(P), _sqrt_psd(Q)
        U, S, Vt = np.linalg.svd(Mq.T @ L)
        _BASIS[r] = (L @ Vt.T[:, :r] @ np.diag(S[:r] ** -0.5),
                     np.diag(S[:r] ** -0.5) @ U[:, :r].T @ Mq.T)
    return _BASIS[r]


def reduced_point(k, r):
    """The block system of res_point() on the reduced common basis."""
    sn = n1_net(k)
    T, Ti = reduced_basis(r)
    Ar, Br, Cr = Ti @ sn.Aa[:8, :8] @ T, Ti @ sn.Bs, sn.Ca[:, :8] @ T
    au, av = 2 * math.pi * A_U_HZ, 2 * math.pi * A_V_HZ
    Aa = np.zeros((r + 2, r + 2))
    Aa[:r, :r], Aa[r, r], Aa[r + 1, r + 1] = Ar, -au, -av
    Ba = np.zeros((r + 2, 2))
    Ba[:r], Ba[r], Ba[r + 1] = Br, sn.ga[0], sn.ga[1]
    Phi = M._expm(Aa * NET_BLOCK / M.FS)
    Gam = np.linalg.solve(Aa, (Phi - np.eye(r + 2)) @ Ba)
    return dict(phi=Phi[:r, :r], gam=Gam, cs=Cr, ga=sn.ga, d=sn.D)


def res_point(k):
    if NET_STATES != 8:
        return reduced_point(k, NET_STATES)
    sn = n1_net(k)
    assert len(sn.p) == 0, "N1 has no per-sample fast modes"
    assert np.allclose(sn.Phi[:8, 8:], 0) and np.allclose(sn.Phi[8:, :8], 0)
    # the kernel drops these (DiodeLadder.c): s never reaches the OTA input
    assert abs(sn.ga[1, 1]) < 1e-9 and abs(sn.D[1, 1]) < 1e-12
    # the emitted per-block decays (ALK_NET_DECAY_U/V) must be this network's
    for row, hz in ((8, A_U_HZ), (9, A_V_HZ)):
        assert np.float32(sn.Phi[row, row]) == np.float32(asym(hz)[2]), (row, sn.Phi[row, row])
    return dict(phi=sn.Phi[:8, :8], gam=sn.Gam, cs=sn.Ca[:, :8], ga=sn.ga, d=sn.D)


def square_table():
    lv = M.constants()["square_levels"]
    out = np.zeros((11, 1024))
    for n in range(11):
        cyc = np.array(lv[min(n, len(lv) - 1)]["sq"], dtype=float)
        c = np.fft.rfft(cyc)
        top = 16.3516 * 2 ** (n + 1)
        H = min(511, int((M.FS / 2) / top))
        c[H + 1:] = 0
        out[n] = np.fft.irfft(c, 1024)
    q = np.round((out - SQ_MID_V) / SQ_SCALE_V * 32767.0)
    assert q.max() <= 32767 and q.min() >= -32767, "square table overflows"
    return q.astype(int)


def emit_header():
    c = M.constants()
    meg, veg = c["meg"], c["veg"]
    v42 = np.array(veg["v42"])
    lines = [
        "/*",
        " * Alkaline tables (S093 P0, P3) - GENERATED by tools/alkaline/gen_alk_tables.py,",
        " * do not edit. Source model: S092_P6_DSP_EQUATIONS.md; network form N1:",
        " * S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md §5.",
        " */",
        "#ifndef ALKALINE_TABLES_H_",
        "#define ALKALINE_TABLES_H_",
        "",
        "#include <stdint.h>",
        "#include \"DiodeLadder.h\"",
        "",
        "/* Resonance table of the real-time network, form N1 (S093 P1 §5). */",
        f"#define ALK_RES_POINTS        {RES_POINTS}u",
        f"#define ALK_NET_BLOCK         {NET_BLOCK}u    /* samples per slow-network update */",
        f"#if ALK_NET_STATES != {NET_STATES}u",
        f"#error \"AlkalineTables: generated for {NET_STATES} network states (ALK_NET_STATES)\"",
        "#endif",
        "extern const DiodeLadderNet alkResTable[ALK_RES_POINTS];",
        "/* N1 asymptote one-poles: per sample y+ = AL y + BE (x + x+); per update",
        " * z+ = DECAY z + ... (gen_alk_tables.py asserts DECAY against the table). */",
        f"#define ALK_NET_AL_U          {f32(asym(A_U_HZ)[0])}  /* u (loop), {A_U_HZ:g} Hz */",
        f"#define ALK_NET_BE_U          {f32(asym(A_U_HZ)[1])}",
        f"#define ALK_NET_DECAY_U       {f32(asym(A_U_HZ)[2])}",
        f"#define ALK_NET_AL_V          {f32(asym(A_V_HZ)[0])}  /* v (VCA input), {A_V_HZ:g} Hz */",
        f"#define ALK_NET_BE_V          {f32(asym(A_V_HZ)[1])}",
        f"#define ALK_NET_DECAY_V       {f32(asym(A_V_HZ)[2])}",
        "",
        "/* TPT prewarp c(k) at k = 0, 0.1 .. 1 (S092 P6 §5.2). */",
        "#define ALK_PREWARP_POINTS    11u",
        "extern const float alkPrewarpC[ALK_PREWARP_POINTS];",
        "",
        "/* VEG: Q31 current against C42 voltage, uniform grid (S092 P6 §4.4). */",
        f"#define ALK_VEG_POINTS        {len(v42)}u",
        f"#define ALK_VEG_V0            {f32(v42[0])}",
        f"#define ALK_VEG_DV_INV        {f32(1.0 / (v42[1] - v42[0]))}",
        "extern const float alkVegCurrent[ALK_VEG_POINTS];   /* A */",
        f"#define ALK_VEG_ATT_DELAY_S   {f32(veg['att_delay'])}",
        f"#define ALK_VEG_TAU_ATT_S     {f32(veg['tau_att'])}",
        f"#define ALK_VEG_V_REC         {f32(veg['v_rec'])}",
        f"#define ALK_VEG_TAU_REC_S     {f32(veg['tau_rec'])}",
        "",
        "/* MEG (S092 P6 §4.2). */",
        f"#define ALK_MEG_V_PEAK        {f32(meg['v_peak'])}",
        f"#define ALK_MEG_V_REST        {f32(meg['v_rest'])}",
        f"#define ALK_MEG_T_STRIKE_S    {f32(meg['t_strike'])}",
        f"#define ALK_MEG_DROP          {f32(meg['drop'])}",
        f"#define ALK_MEG_SOFT          {f32(meg['soft'])}",
        f"#define ALK_MEG_V53_MIN       {f32(meg['v53_min'])}",
        "",
        "/* Accent network (S092 P6 §4.3). */",
        f"#define ALK_ACC_V13_REST      {f32(c['accent']['v13_rest'])}",
        f"#define ALK_ACC_VD27          {f32(c['vca_accent']['v_drop'])}",
        "",
        "/* D8 square, per octave level, from the P1 SPICE run. */",
        f"#define ALK_SQR_SCALE_V       {f32(SQ_SCALE_V / 32767.0)}  /* volts per LSB */",
        "extern const int16_t alkSqrTable[11][1024];",
        "",
        "#endif /* ALKALINE_TABLES_H_ */",
        "",
    ]
    return "\n".join(lines)


def mat(a, ind):
    a = np.atleast_2d(a)
    rows = [", ".join(f32(x) for x in r) for r in a]
    return ("{ " if len(rows) == 1 else "{\n" + ind + "    ") + \
        (",\n" + ind + "    ").join("{ " + r + " }" for r in rows) + \
        (" }" if len(rows) == 1 else "\n" + ind + "}")


def vec(a):
    return "{ " + ", ".join(f32(x) for x in a) + " }"


def emit_source():
    c = M.constants()
    lines = [
        "/*",
        " * Alkaline tables (S093 P0, P3) - GENERATED by tools/alkaline/gen_alk_tables.py,",
        " * do not edit.",
        " *",
        " * What:       the Resonance table of the real-time feedback network (form",
        " *             N1, S093 P1 §5), the",
        " *             TPT prewarp table and the VEG current table.",
        " * Why:        S092 P6 §4.4, §5.2, §5.3: every coefficient depends on",
        " *             Resonance only and varies smoothly with it, so one table",
        " *             interpolated per block replaces an on-device eigen/expm solve.",
        " * Inputs:     tools/alkaline/alk_model.py, alk_constants.json.",
        " * Outputs:    .rodata (program flash).",
        " * Accessors:  AlkalineVoice.c (per block), DiodeLadder.c (per block).",
        " * Affiliates: DiodeLadderNet (DiodeLadder.h); gen_alk_tables.py --check.",
        " */",
        "#include \"AlkalineTables.h\"",
        "",
        "const DiodeLadderNet alkResTable[ALK_RES_POINTS] = {",
    ]
    for n in range(RES_POINTS):
        k = n / (RES_POINTS - 1)
        r = res_point(k)
        ind = "        "
        lines += [
            f"    {{   /* Res k = {k:.3f} */",
            f"        .phi = {mat(r['phi'], ind)},",
            f"        .gam = {mat(r['gam'], ind)},",
            f"        .cs = {mat(r['cs'], ind)},",
            f"        .ga = {mat(r['ga'], ind)},",
            f"        .d = {mat(r['d'], ind)},",
            "    },",
        ]
    lines += ["};", "",
              "const float alkPrewarpC[ALK_PREWARP_POINTS] = " + vec(M.PW_C) + ";", "",
              "const float alkVegCurrent[ALK_VEG_POINTS] = {"]
    i = np.array(c["veg"]["i_uA"]) * 1e-6
    for s in range(0, len(i), 6):
        lines.append("    " + ", ".join(f32(x) for x in i[s:s + 6]) + ",")
    lines += ["};", ""]
    return "\n".join(lines)


def emit_square():
    q = square_table()
    lines = [
        "/*",
        " * Alkaline square table (S093 P0) - GENERATED by tools/alkaline/gen_alk_tables.py,",
        " * do not edit.",
        " *",
        " * What:       the TB-303 square (Q8 collector into S1) as one cycle per",
        " *             octave level, in the stock-table layout (level n = the octave",
        " *             below 16.3516 * 2^(n+1) Hz, 1024 points). Levels 0-6 are the",
        " *             P1 SPICE cycles at each level's centre frequency (duty 57.7 %",
        " *             at level 0 to 20.5 % at level 6, level 3.6 V to 8.0 V);",
        " *             levels 7-10 repeat level 6. Each level keeps the harmonics",
        " *             below Nyquist at the top of its octave.",
        f" *             int16 = (v - {SQ_MID_V} V) / {SQ_SCALE_V} V * 32767.",
        " * Why:        S092 P0 D8: the square's pitch-dependent duty and level are",
        " *             encoded per level (0 runtime cost). Flash: 22,528 B (D3).",
        " * Inputs:     alk_constants.json square_levels (fit_spice.py, P1 netlist).",
        " * Outputs:    .rodata (program flash).",
        " * Accessors:  AlkalineVoice.c via calcWavetableOscBlock().",
        " * Affiliates: ALK_SQR_SCALE_V (AlkalineTables.h).",
        " */",
        "#include \"AlkalineTables.h\"",
        "",
        "const int16_t alkSqrTable[11][1024] = {",
    ]
    for n in range(11):
        lines.append(f"    {{   /* level {n} */")
        for s in range(0, 1024, 16):
            lines.append("        " + ", ".join(str(v) for v in q[n, s:s + 16]) + ",")
        lines.append("    },")
    lines += ["};", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="compare with the committed files; exit 1 on a difference")
    ap.add_argument("--points", type=int, default=None,
                    help="experiments only: Res points (the firmware uses RES_POINTS)")
    ap.add_argument("--out", default=None, help="experiments only: output directory")
    ap.add_argument("--states", type=int, default=None,
                    help="experiments only: block-system states (a common reduced basis)")
    ap.add_argument("--block", type=int, default=None,
                    help="experiments only: samples per network update (the firmware uses NET_BLOCK)")
    a = ap.parse_args()
    global RES_POINTS, OUT_DIR, NET_BLOCK, NET_STATES
    if a.block:
        NET_BLOCK = a.block
    if a.states:
        NET_STATES = a.states
    if a.points:
        RES_POINTS = a.points
    if a.out:
        OUT_DIR = a.out
    files = {
        "AlkalineTables.h": emit_header(),
        "AlkalineTables.c": emit_source(),
        "AlkalineSquareTable.c": emit_square(),
    }
    bad = 0
    for name, text in files.items():
        path = os.path.join(OUT_DIR, name)
        if a.check:
            old = open(path).read() if os.path.exists(path) else None
            if old != text:
                print(f"{name}: DIFFERS from the generator")
                bad += 1
            else:
                print(f"{name}: matches")
        else:
            os.makedirs(OUT_DIR, exist_ok=True)
            with open(path, "w") as fh:
                fh.write(text)
            print(f"wrote {path}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
