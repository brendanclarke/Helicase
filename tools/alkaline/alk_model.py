#!/usr/bin/env python3
"""Alkaline (TB-303) DSP reference model (S092 P6).

What:       the DSP equations derived from the S092 P1-P4 circuit models, as
            a Python reference: the diode-ladder VCF with its input pair and
            Q21 output stage, closed through the linear network around it
            (FeedbackNet: C14, C15, C16, C17, C21, C22, C22F, C23 and their
            resistors); the control path (knobs -> expo current, MEG, the
            accent network); the VEG and the OTA; the pitch path (slide lag).
            Solvers: the golden RK4 at 8x oversampling (LadderRef), the TPT
            options A (linear stages) and B (nonlinear stages) with the
            network per sample, and the real-time forms of A and B
            (block_net=True: SplitNet, fast modes per sample, the slow
            network per block). Equations and every constant's source:
            S092_P6_DSP_EQUATIONS.md.
Why:        P0 phase P1/P2: host models for the filter options A and B and
            the constants the C implementation will use.
Inputs:     alk_constants.json (fitted from SPICE by fit_spice.py) for the
            MEG, VEG and square-wave tables; circuit values below.
Outputs:    functions/classes used by validate.py.
Accessors:  tools/alkaline/validate.py
"""
import json
import math
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

FS = 44108.0                 # Helicase sample rate
BLOCK = 32                   # control block
VT = 0.02585                 # thermal voltage at 27 C (the SPICE runs)

# ---------------------------------------------------------------- circuit values
# [SN] p5 unless stated. Units: ohm, farad, volt, ampere.
C_LADDER = (18e-9, 33e-9, 33e-9, 33e-9)        # C18 (bottom), C19, C24, C26
# Q21 output stage: small-signal gain 103.6 (S092 P2 §2.1, R110 22k, measured
# at 1 kHz incl. loading) = A / (2 Vt)
A_OUT = 103.6 * 2 * VT
# C27/C25 into Q21's input: Rin = R114 || (beta Vt / (I21/2)), beta 300, I21 0.51 mA
R_IN21 = 1.0 / (1.0 / 100e3 + (0.51e-3 / 2) / (300 * VT))
TAU_OUT = 0.1e-6 * R_IN21                       # ~2.33 ms (68 Hz)
# ... and that coupling draws current from the ladder's top capacitor C26:
# differential load current v_hp / (2 R_IN21) (S092 P6 §2.1, term L)
W_LOAD = 1.0 / (2 * R_IN21 * C_LADDER[3])      # rad/s, acting on v_hp / v3

# OTA pin 6 into (47k + 22k) || 220k (S092 P3)
R_OTA_LOAD = (47e3 + 22e3) * 220e3 / (47e3 + 22e3 + 220e3)

# Control network (S092 P4)
V12 = 12.0
V561 = 3.263                 # Q9's node at rest (P4 §3.1)
VBE11_CAL = 0.5279           # Q11 Vbe at 7.375 uA (P4 rest: nb11 - n561)
I_CAL = 7.375e-6
BETA11 = 240.0               # Q11 (2SC536) current gain in the P4 netlist
R47, R61, R63, R71, R72, R73, R46 = 10e3, 10e3, 220e3, 2.2e3, 100e3, 100e3, 47e3
R119, R120, C36 = 47e3, 22e3, 0.033e-6
TM3 = 152.74e3               # P4 calibration
VR = 50e3                    # VR3, VR4b, VR5, VR7
C13 = 1e-6
VD24 = 0.48                  # D24 (1N4148, IS 2.52 nA, N 1.752) at ~0.1 mA
R_4066 = 250.0

# Pitch
F_C2 = 65.406
TAU_SLIDE = 100e3 * 0.22e-6  # R-2R output resistance x C35 (P0 A9)

# Per-instance fitted constants
_CONST = None


def constants():
    global _CONST
    if _CONST is None:
        with open(os.path.join(HERE, "alk_constants.json")) as fh:
            _CONST = json.load(fh)
    return _CONST


def taper_a(r):
    """Audio taper (VR3, VR5, VR6): 20 % of the track at half rotation."""
    return (16.0 ** r - 1.0) / 15.0


# =============================================================== control path
# S093 P3: FAST_CONTROL selects the firmware's reduced control path
# (AlkalineControl.c, S093_P2 §3A Tier 1/Tier 2): expo by 2 fixed-point passes
# warm-started from the last block's current (callers pass i_prev); the MEG
# diode stack by one Newton step from the last block's current; the accent
# network solved once per block with the diode states the last block's
# solution implied (T2-3; the firmware default is the exact T1-1 since S093
# P3, which equals the reference rule, so FAST_ACCENT_LAG selects T2-3 here).
# The fast log2/exp2 and the rational prewarp are
# within 1e-5 of exact and are not modelled. validate.py --fast runs V3/V5 on it.
FAST_CONTROL = False
FAST_ACCENT_LAG = True       # T2-3 (ALK_ACC_SOLVES 1); False = T1-1 (= reference)
EXPO_PASSES_FAST = 2


def expo_current(cut, env, res, n53, v_w4, i_prev=None):
    """Ladder current (A) from the knobs, the MEG (N53) and the accent
    sweep's VR4b wiper voltage (S092 P6 §3.1). Static at control rate.
    FAST_CONTROL with i_prev: the firmware's warm-started passes."""
    r_lo3 = VR * taper_a(cut)
    v_th3 = V561 + (V12 - V561) * (R47 + r_lo3) / (R47 + VR)
    r_th3 = (VR - r_lo3) * (R47 + r_lo3) / (R47 + VR)
    r_lo5 = VR * taper_a(env)
    v_th5 = V561 + (n53 - V561) * (R61 + r_lo5) / (R61 + VR)
    r_th5 = (R61 + r_lo5) * (VR - r_lo5) / (R61 + VR)
    g5 = 1.0 / (R63 + r_th5)
    g4 = 1.0 / R72                       # v_w4 is the wiper voltage under load
    dv = ((v_th5 - V561) * g5 + (v_w4 - V561) * g4) / (1.0 / R71 + g5 + g4)
    e = math.exp(dv / VT)
    i = I_CAL
    passes = 3
    if FAST_CONTROL and i_prev is not None:
        i, passes = max(i_prev, 1e-12), EXPO_PASSES_FAST
    for _ in range(passes):              # Q11's Vbe follows its current (log)
        vbe = VBE11_CAL + VT * math.log(max(i, 1e-12) / I_CAL)
        i73 = max((v_th3 - (V561 + vbe)) / (r_th3 + TM3 + R73), 1e-12)
        # Q11's base current comes out of R73's current: I = (I73 - I/b) e
        i = i73 * e / (1.0 + e / BETA11)
    return i, dv


ND_VT = 1.752 * VT          # 1N4148 model (N 1.752)
IS_D = 2.52e-9
I_R117 = (5.333 - 1.02) / 22e3   # R117 bias into D27/D26 (~0.2 mA)


class Meg:
    """Filter envelope: C62 state, strike, discharge through R138 + VR6 into
    the diode stack (V_inf), Darlington output N53 (S092 P6 §3.2)."""

    def __init__(self):
        c = constants()["meg"]
        self.v_peak, self.v_inf, self.t_strike = c["v_peak"], c["v_inf"], c["t_strike"]
        self.drop, self.soft, self.v53_min = c["drop"], c["soft"], c["v53_min"]
        self.v = c["v_rest"]
        self.strike_left = 0.0
        self.i = 0.0                     # FAST_CONTROL: the stack current

    def trigger(self):
        self.strike_left = self.t_strike

    def step(self, dt, decay, accent_on):
        r = 68e3 + (R_4066 if accent_on else 1e6 * taper_a(decay))
        if self.strike_left > 0:
            self.v = self.v_peak
            self.strike_left -= dt
            self.i = (self.v_peak - 1.3) / r
        elif FAST_CONTROL:
            # one Newton step on r i + vy(i) = v from the last block's i;
            # vy at the new current by its tangent (AlkalineControl.c)
            i = max(self.i, 0.0)
            vy = ND_VT * (2 * math.log((I_R117 + i) / IS_D + 1.0) + math.log(i / IS_D + 1.0))
            dvy = ND_VT * (2 / (I_R117 + i + IS_D) + 1 / (i + IS_D))
            inew = max(i - (r * i + vy - self.v) / (r + dvy), 0.0)
            vyn = vy + dvy * (inew - i)
            self.i = inew
            self.v = vyn + (self.v - vyn) * math.exp(-dt / (r * 1e-6))
        else:
            # discharge into the D28 / (D27 + D26, biased by R117) stack: the
            # stack voltage follows its current (diode law); a few fixed-point
            # passes, then an exact exponential step towards it
            i = max((self.v - 1.3) / r, 1e-12)
            for _ in range(4):
                vj = 2 * ND_VT * math.log((I_R117 + i) / IS_D + 1.0)
                vy = vj + ND_VT * math.log(i / IS_D + 1.0)
                i = max((self.v - vy) / r, 0.0)
            self.v = vy + (self.v - vy) * math.exp(-dt / (r * 1e-6))
        return self.n53()

    def n53(self):
        x = (self.v - self.drop) / self.soft
        sp = x if x > 30 else math.log1p(math.exp(x))
        return self.v53_min + self.soft * sp


class AccentNet:
    """The accent network from VR7's wiper (S092 P6 §3.3): the wiper W7 is one
    node loaded by both branches, D24 -> R46 -> VR4b (P, W) with C13 and R72
    into the expo node (the sweep), and R119 -> C36 -> R120 -> D27 into the
    OTA's pin 1 (the VCA accent). One backward-Euler nodal solve per block
    over (W7, P, W, V13, V36) with D24 and D27 as ideal-switch + fixed-drop
    diodes. Returns VR4b's wiper voltage for expo_current() and the accent
    current into pin 1. vca_branch=False drops R119 (the P4 control netlist,
    which has no VCA side)."""

    def __init__(self, vca_branch=True):
        self.v13 = constants()["accent"]["v13_rest"]
        self.v36 = 0.0
        self.vd27 = constants()["vca_accent"]["v_drop"]
        self.vca_branch = vca_branch
        self.d24 = self.d27 = 0          # FAST_CONTROL: the lagged diode states

    def step(self, dt, accent_on, acc_knob, res, n53, v134):
        ra7 = max(VR * acc_knob, 1.0)                    # wiper -> ground
        rb7 = max(VR * (1 - acc_knob), 1.0) + (R_4066 if accent_on else 1e12)
        ra = max(VR * res, 1.0)                          # VR4b pin 1 (P) -> W
        rb = max(VR * (1 - res), 1.0)                    # W -> C13
        g13, g36 = C13 / dt, C36 / dt
        g119 = 1.0 / R119 if self.vca_branch else 0.0
        states = (((self.d24, self.d27),) if FAST_CONTROL and FAST_ACCENT_LAG
                  else ((1, 1), (1, 0), (0, 1), (0, 0)))
        for d24, d27 in states:
            g46 = d24 / R46
            g120 = d27 / R120
            # unknowns W7, P, W, V13, V36
            G = np.array([
                [1 / ra7 + 1 / rb7 + g46 + g119, -g46, 0.0, 0.0, -g119],
                [-g46, g46 + 1 / ra, -1 / ra, 0.0, 0.0],
                [0.0, -1 / ra, 1 / ra + 1 / rb + 1 / R72, -1 / rb, 0.0],
                [0.0, 0.0, -1 / rb, 1 / rb + g13, 0.0],
                [-g119, 0.0, 0.0, 0.0, g119 + g36 + g120]])
            rhs = np.array([n53 / rb7 + g46 * VD24, -g46 * VD24, v134 / R72,
                            g13 * self.v13, g36 * self.v36 + g120 * self.vd27])
            w7, vp, vw, v13, v36 = np.linalg.solve(G, rhs)
            ok24 = (w7 - VD24 - vp >= 0) == bool(d24)
            ok27 = (v36 - self.vd27 >= 0) == bool(d27)
            if FAST_CONTROL and FAST_ACCENT_LAG:   # the states it implies, next block
                self.d24, self.d27 = int(w7 - VD24 - vp >= 0), int(v36 - self.vd27 >= 0)
            if ok24 and ok27:
                break
        self.v13, self.v36 = v13, v36
        return vw, (max(0.0, (v36 - self.vd27) / R120) if self.vca_branch else 0.0)


class Veg:
    """VCA envelope: C42 state (recharge tau R123 C42 = 1.5 s, emptied by the
    trigger with the fitted attack), current through Q31 from the gate as a
    table fitted to SPICE (S092 P6 §3.4)."""

    def __init__(self):
        c = constants()["veg"]
        self.v = 12.0 - 1e-3
        self.tau_att = c["tau_att"]
        self.att_delay = c["att_delay"]
        self.v_tab = np.array(c["v42"])
        self.i_tab = np.array(c["i_uA"]) * 1e-6
        self.att_left = 0.0
        self.delay_left = 0.0
        self.tau_rec = c.get("tau_rec", 1.5)
        self.v_rec = c.get("v_rec", 12.0)

    def trigger(self):
        self.delay_left = self.att_delay
        self.att_left = 8 * self.tau_att

    def step(self, dt, gate_on):
        if self.delay_left > 0:
            self.delay_left -= dt
        elif self.att_left > 0:
            self.v *= math.exp(-dt / self.tau_att)
            self.att_left -= dt
        self.v = self.v_rec + (self.v - self.v_rec) * math.exp(-dt / self.tau_rec)
        return float(np.interp(self.v, self.v_tab, self.i_tab)) if gate_on else 0.0


# ======================================================= feedback/VCA network
class FeedbackNet:
    """The linear network around the ladder (S092 P6 §2.4): Q19's emitter
    (tp_pre) -> C14 -> TP6 -> VR4a (loaded wiper) -> C15 -> Q18 (bias R99 to
    C22F || R66) -> C23 -> R97/R98 -> Q12's right base, with the shared bias
    node NB (R68 || C16); the VCO input S1 -> C17 -> R62 -> R70 -> NB; and the
    VCA inputs TP6 -> C21 -> R121 and wiper -> C22 -> R122 into R124.
    Eight capacitor states z. Built per Resonance setting k as a continuous
    state-space model  dz/dt = A z + B w,  y = C z + D w,  with inputs
    w = (tp_pre, s) and outputs y = (u = v(inL) - v(inR), v_vca = OTA input,
    TP6)."""

    CAPS = (("E", "TP6", 1e-6), ("TP6", "A1", 0.01e-6), ("W", "B1", 1e-6),
            ("NB18", "0", 1e-6), ("W", "A2", 0.01e-6), ("Q", "F1", 1e-6),
            ("NB", "0", 10e-6), ("S", "G1", 1e-6))
    NAMES = ("C14", "C21", "C15", "C22F", "C22", "C23", "C16", "C17")

    def __init__(self, k):
        r_top = max(50e3 * (1 - k), 1e-3)
        r_bot = max(50e3 * k, 1e-3)
        rin18 = 300 * (10e3 * 12.2e3 / 22.2e3)
        self.res = (("TP6", "W", r_top), ("W", "0", r_bot), ("A1", "P3", 220e3),
                    ("P3", "0", 2.2e3), ("B1", "B18", 2.2e3), ("B18", "NB18", 100e3),
                    ("NB18", "0", 100e3), ("B18", "0", rin18), ("A2", "P3", 100e3),
                    ("F1", "INR", 10e3), ("INR", "NB", 2.2e3), ("NB", "0", 2.2e3),
                    ("G1", "INL", 220e3), ("INL", "NB", 2.2e3))
        nodes = sorted({n for a, b, _ in self.res for n in (a, b)} |
                       {n for a, b, _ in self.CAPS for n in (a, b)} | {"E", "S", "Q"} - {"0"})
        nodes = [n for n in nodes if n != "0"]
        self.idx = {n: i for i, n in enumerate(nodes)}
        nn = len(nodes)
        # MNA unknowns: node voltages, then branch currents of E, S, Q and the 8 caps
        self.n_unk = nn + 3 + len(self.CAPS)
        A = np.zeros((8, 8))
        B = np.zeros((8, 2))
        C = np.zeros((3, 8))
        D = np.zeros((3, 2))
        for col in range(10):                 # 8 cap voltages, then tp_pre, s
            z = np.zeros(8)
            w = np.zeros(2)
            if col < 8:
                z[col] = 1.0
            else:
                w[col - 8] = 1.0
            v, ic = self._solve(z, w, nn)
            dz = ic / np.array([c for _, _, c in self.CAPS])
            y = np.array([v[self.idx["INL"]] - v[self.idx["INR"]], v[self.idx["P3"]],
                          v[self.idx["TP6"]]])
            if col < 8:
                A[:, col], C[:, col] = dz, y
            else:
                B[:, col - 8], D[:, col - 8] = dz, y
        self.A, self.B, self.C, self.D = A, B, C, D

    def _solve(self, z, w, nn):
        n = self.n_unk
        G = np.zeros((n, n))
        rhs = np.zeros(n)
        def node(x):
            return None if x == "0" else self.idx[x]
        for a, b, r in self.res:
            ia, ib = node(a), node(b)
            g = 1.0 / r
            for i, j, sgn in ((ia, ia, 1), (ib, ib, 1), (ia, ib, -1), (ib, ia, -1)):
                if i is not None and j is not None:
                    G[i, j] += sgn * g
        # voltage-defined branches: E = tp_pre, S = s, Q = 0.99 V(B18), caps V(a)-V(b) = z
        branches = [("E", "0", ("v", w[0])), ("S", "0", ("v", w[1])), ("Q", "0", ("vcvs", 0.99))]
        branches += [(a, b, ("v", z[q])) for q, (a, b, _) in enumerate(self.CAPS)]
        for q, (a, b, kind) in enumerate(branches):
            row = nn + q
            ia, ib = node(a), node(b)
            if ia is not None:
                G[ia, row] += 1.0           # branch current leaves a
                G[row, ia] += 1.0
            if ib is not None:
                G[ib, row] -= 1.0
                G[row, ib] -= 1.0
            if kind[0] == "v":
                rhs[row] = kind[1]
            else:                           # V(Q) - gain V(B18) = 0
                G[row, self.idx["B18"]] -= kind[1]
        sol = np.linalg.solve(G, rhs)
        v = sol[:nn]
        # cap current (a -> b through the cap) = the branch current variable
        # (MNA convention: a branch current leaves node a)
        ic = sol[nn + 3:]
        return v, ic

    def discrete(self, fs):
        """Trapezoidal (bilinear) form: z+ = Ad z + Bd (w + w+), y = C z + D w."""
        h2 = 0.5 / fs
        M = np.eye(8) - h2 * self.A
        Mi = np.linalg.inv(M)
        return Mi @ (np.eye(8) + h2 * self.A), Mi @ (h2 * self.B)


def _lyap(A, Q):
    """Solves A X + X A^T + Q = 0 (Kronecker form; the network is 8 states)."""
    n = A.shape[0]
    K = np.kron(np.eye(n), A) + np.kron(A, np.eye(n))
    return np.linalg.solve(K, -Q.reshape(-1)).reshape(n, n)


def _balanced(A, B, C, r):
    """Square-root balanced truncation of (A, B, C) to r states."""
    Wc, Wo = _lyap(A, B @ B.T), _lyap(A.T, C.T @ C)
    def sqrt_psd(W):
        lam, V = np.linalg.eigh(0.5 * (W + W.T))
        return V * np.sqrt(np.clip(lam, 1e-30, None))
    Lc, Lo = sqrt_psd(Wc), sqrt_psd(Wo)
    U, sv, Vt = np.linalg.svd(Lo.T @ Lc)
    si = 1.0 / np.sqrt(sv[:r])
    T = Lc @ Vt[:r].T * si                   # n x r
    Ti = (si[:, None] * U[:, :r].T) @ Lo.T   # r x n
    return Ti @ A @ T, Ti @ B, C @ T, sv


class ReducedNet:
    """The real-time form of FeedbackNet (S092 P6 §4.2). The network splits
    by what it drives: the loop part (inputs tp_pre and s, output u into the
    ladder) and the VCA part (input tp_pre only, output v_vca; s never reaches
    the OTA). Each is balanced-truncated on its own output, to r_u and r_v
    states, and the two are stacked block-diagonally. The D terms are kept
    exactly, so the instantaneous resonance gain is the circuit's. Rows of
    C: u, v_vca, and TP6 (NaN: the real-time form does not carry it). Same
    interface as FeedbackNet (A, B, C, D, discrete())."""

    def __init__(self, k, r_u=4, r_v=2, full=None):
        f = full or FeedbackNet(k)
        Au, Bu, Cu, self.hsv_u = _balanced(f.A, f.B, f.C[:1], r_u)
        Av, Bv, Cv, self.hsv_v = _balanced(f.A, f.B[:, :1], f.C[1:2], r_v)
        n = r_u + r_v
        self.A = np.zeros((n, n))
        self.A[:r_u, :r_u], self.A[r_u:, r_u:] = Au, Av
        self.B = np.zeros((n, 2))
        self.B[:r_u], self.B[r_u:, 0] = Bu, Bv[:, 0]
        self.C = np.zeros((3, n))
        self.C[0, :r_u], self.C[1, r_u:] = Cu[0], Cv[0]
        self.C[2] = np.nan                    # TP6 is not carried in real time
        self.D = f.D.copy()
        self.r_u, self.r_v = r_u, r_v

    def discrete(self, fs):
        n = self.A.shape[0]
        h2 = 0.5 / fs
        Mi = np.linalg.inv(np.eye(n) - h2 * self.A)
        return Mi @ (np.eye(n) + h2 * self.A), Mi @ (h2 * self.B)


def _expm(A):
    """Matrix exponential (scaling and squaring, Taylor order 14)."""
    nrm = np.max(np.sum(np.abs(A), axis=1))
    sq = max(0, int(math.ceil(math.log2(max(nrm, 1e-30)))) + 1)
    X = A / 2 ** sq
    E, term = np.eye(A.shape[0]), np.eye(A.shape[0])
    for q in range(1, 15):
        term = term @ X / q
        E = E + term
    for _ in range(sq):
        E = E @ E
    return E


class SplitNet:
    """The real-time form of FeedbackNet (S092 P6 §4.3), split by mode speed.

    w = (tp_pre, s); outputs u (into the ladder) and v (the OTA input).
      D w        per sample: the instantaneous gains (resonance amount).
      fast modes per sample: the eigenmodes faster than F_SPLIT (C21/C22,
                 70-160 Hz), as one-poles  dy/dt = -p y + b.w, read out by
                 (c_u, c_v).
      q_u, q_v   per sample: the slow group's 1/s asymptote on each output,
                 one-poles at A_ASYM with input gains C_o B_slow, so the slow
                 group's high-frequency tail (which sets the resonance
                 damping) stays at audio rate.
      slow rest  per block: the slow group minus the two asymptote poles,
                 zero-order hold on the block means of w, output ramped per
                 sample from the previous block's value (one block latency).
    """
    F_SPLIT = 40.0
    A_ASYM = 2 * math.pi * 5.0
    A_ASYM_U = None        # S093 P1: separate asymptote poles per output
    A_ASYM_V = None        # (None = A_ASYM)

    def __init__(self, k, fs=FS, block=BLOCK, full=None):
        f = full or FeedbackNet(k)
        lam, V = np.linalg.eig(f.A)
        lam, V = lam.real, V.real
        Vi = np.linalg.inv(V)
        fast = np.abs(lam) > 2 * math.pi * self.F_SPLIT
        Pf = V[:, fast] @ Vi[fast]                       # projector on the fast modes
        # fast modes sorted by pole, each scaled to unit gain from tp_pre (the
        # oscillator does not reach them), so p, bf and cf are smooth in k
        idx = np.where(fast)[0][np.argsort(-lam[fast])]
        bf = (Vi @ f.B)[idx]
        cf = (f.C[:2] @ V)[:, idx]
        self.p = -lam[idx]                               # rad/s
        self.bf = bf / bf[:, :1]                         # n_f x 2: (1, ~0)
        self.cf = cf * bf[:, 0]                          # 2 x n_f (rows u, v)
        Bs = (np.eye(8) - Pf) @ f.B
        Cs = f.C[:2]
        self.ga = Cs @ Bs                                # 2 x 2: asymptote gains (rows u, v)
        a = self.A_ASYM
        au = self.A_ASYM_U or a
        av = self.A_ASYM_V or a
        self.a_uv = np.array([au, av])
        # block-rate system: [z (8, slow subspace), q'_u, q'_v]; outputs Cs z - q'
        Aa = np.zeros((10, 10))
        Aa[:8, :8] = f.A
        Aa[8, 8], Aa[9, 9] = -au, -av
        Ba = np.zeros((10, 2))
        Ba[:8], Ba[8], Ba[9] = Bs, self.ga[0], self.ga[1]
        Ca = np.zeros((2, 10))
        Ca[:, :8] = Cs
        Ca[0, 8] = Ca[1, 9] = -1.0
        T = block / fs
        self.Phi = _expm(Aa * T)
        self.Gam = np.linalg.solve(Aa, (self.Phi - np.eye(10)) @ Ba)
        self.Aa, self.Ba, self.Ca, self.Bs = Aa, Ba, Ca, Bs
        self.D = f.D[:2]
        h2 = 0.5 / fs
        # trapezoidal one-poles: y+ = al y + be (x + x+)
        self.al_f = (1 - self.p * h2) / (1 + self.p * h2)
        self.be_f = h2 / (1 + self.p * h2)
        self.al_a = (1 - self.a_uv * h2) / (1 + self.a_uv * h2)   # per output (u, v)
        self.be_a = h2 / (1 + self.a_uv * h2)

    def dc_state(self, s_dc):
        """Block-rate state at the DC operating point for a constant s."""
        return -np.linalg.solve(self.Aa, self.Ba @ np.array([0.0, s_dc]))


    def per_sample_ops(self):
        return dict(fast_modes=len(self.p))


def make_net(k, order=None):
    """FeedbackNet (order None) or its real-time form, order = (r_u, r_v)."""
    return FeedbackNet(k) if order is None else ReducedNet(k, *order)


# ------------------------------------------------------------- TPT prewarp
# The TPT (bilinear) ladder runs flat at high cutoffs (-7 % at 6.6 kHz). The
# ladder rate is scaled by P = tan(theta)/theta, theta = c(k) w1 / (2 fs), so
# that the response's -3 dB point (above the peak when there is one) lands
# where the continuous model puts it. c(k) = f_-3dB / (w1 / 2 pi), measured on
# the linearised model (S092 P6 §5.2). One tan per block in firmware.
PW_K = tuple(q / 10.0 for q in range(11))
PW_C = (0.256, 0.597, 0.911, 1.055, 1.153, 1.228, 1.287, 1.342, 1.387, 1.430, 1.470)
PW_THETA_MAX = 1.3


def prewarp(i_lad, k, fs=FS):
    """Factor on the ladder current for the TPT solvers."""
    c = float(np.interp(k, PW_K, PW_C))
    th = min(c * i_lad / (4 * VT * C_LADDER[1]) / (2 * fs), PW_THETA_MAX)
    return math.tan(th) / th if th > 1e-6 else 1.0


# ================================================================ the ladder
def _t(z):
    """tanh(z)/z, the secant gain of the linearised nonlinearities."""
    return math.tanh(z) / z if abs(z) > 1e-6 else 1.0 - z * z / 3.0


class LadderBase:
    """State: x[0..3] = ladder cap voltages / (2 Vt); HP capacitor voltages
    z_in (C17), z_out (C27/C25), z14, z15, z23."""

    def __init__(self, fs=FS):
        self.fs = fs
        self.x = [0.0] * 4
        self.z_out = 0.0
        self.znet = np.zeros(8)
        self.vca_in = None
        self.max_stage = 0.0
        self.max_out = 0.0


def settle(lad, s_dc, k):
    """Start a ladder at the DC operating point: network capacitors at their
    steady state for a constant input s_dc (as SPICE starts from .op)."""
    lad.s_dc = s_dc
    lad.zblk = None
    net = make_net(k, getattr(lad, "net_order", None))
    lad.znet = -np.linalg.solve(net.A, net.B @ np.array([0.0, s_dc]))
    lad.w_prev = np.array([0.0, s_dc])
    return lad


class LadderRef(LadderBase):
    """Golden: the continuous equations (S092 P6 §2.2) by RK4 at OS x fs."""

    def __init__(self, fs=FS, os_=8):
        super().__init__(fs)
        self.os = os_

    def deriv(self, st, s, i_lad, net):
        x0, x1, x2, x3, zo = st[:5]
        zn = np.array(st[5:])
        v3 = 2 * VT * x3
        vhp = v3 - zo
        tp_pre = -A_OUT * math.tanh(vhp / (2 * VT))
        w_in = np.array([tp_pre, s])
        y = net.C @ zn + net.D @ w_in
        u = y[0]
        xin = u / (2 * VT)
        w = [i_lad / (4 * VT * c) for c in C_LADDER]
        t01, t12, t23, t3 = (math.tanh(x0 - x1), math.tanh(x1 - x2),
                             math.tanh(x2 - x3), math.tanh(x3))
        dz = net.A @ zn + net.B @ w_in
        d = [w[0] * (-math.tanh(xin) - t01), w[1] * (t01 - t12), w[2] * (t12 - t23),
             w[3] * (t23 - t3) - W_LOAD * vhp / (2 * VT), vhp / TAU_OUT] + list(dz)
        return d, tp_pre, y, (abs(x0 - x1), abs(x1 - x2), abs(x2 - x3), abs(x3)), vhp

    def process(self, s, i_lad, k):
        net = FeedbackNet(k)
        n = len(s)
        out = np.empty(n)
        vca = np.empty(n)
        tpo = np.empty(n)
        st = list(self.x) + [self.z_out] + list(self.znet)
        ns = len(st)
        h = 1.0 / (self.fs * self.os)
        s_prev = s[0]
        for j in range(n):
            for m in range(self.os):
                a = s_prev + (s[j] - s_prev) * (m / self.os)
                b = s_prev + (s[j] - s_prev) * ((m + 0.5) / self.os)
                c = s_prev + (s[j] - s_prev) * ((m + 1) / self.os)
                k1 = self.deriv(st, a, i_lad[j], net)[0]
                k2 = self.deriv([st[q] + h / 2 * k1[q] for q in range(ns)], b, i_lad[j], net)[0]
                k3 = self.deriv([st[q] + h / 2 * k2[q] for q in range(ns)], b, i_lad[j], net)[0]
                k4 = self.deriv([st[q] + h * k3[q] for q in range(ns)], c, i_lad[j], net)[0]
                st = [st[q] + h / 6 * (k1[q] + 2 * k2[q] + 2 * k3[q] + k4[q]) for q in range(ns)]
            s_prev = s[j]
            _, tp_pre, y, sd, vhp = self.deriv(st, s[j], i_lad[j], net)
            self.max_stage = max(self.max_stage, max(sd))
            self.max_out = max(self.max_out, abs(vhp) / (2 * VT))
            zn = np.array(st[5:])
            out[j] = tp_pre - zn[0]           # TP6 = Q19 emitter - C14 voltage
            vca[j] = y[1]
            tpo[j] = tp_pre
        self.x = st[:4]
        self.z_out = st[4]
        self.znet = np.array(st[5:])
        self.vca_in = vca
        self.tp_pre = tpo
        return out


class LadderTPT(LadderBase):
    """Real-time candidates (S092 P6 §2.3): trapezoidal (TPT) at 1x, every
    nonlinearity replaced by its secant gain at the previous sample, the
    4x4 tridiagonal solved with one shared reciprocal, and the loop closed
    with one division. stage_nl=False is option A (linear ladder stages,
    nonlinear input and output); True is option B (all stages)."""

    def __init__(self, fs=FS, stage_nl=True, net_order=None, block_net=None, warp=True):
        super().__init__(fs)
        self.stage_nl = stage_nl
        self.warp = warp                     # prewarp the ladder rate (prewarp())
        self.net_order = net_order           # None: full 8-state network
        # block_net=True: the real-time form (SplitNet, S092 P6 §5.3): fast modes
        # and the slow group's asymptotes per sample, the slow remainder per block
        self.block_net = block_net
        self.t_st = [1.0, 1.0, 1.0, 1.0]     # secant gains of the four diode pairs
        self.t_in = 1.0
        self.t_out = 1.0
        self.xin_prev = 0.0
        self.vhp_prev = 0.0

    @staticmethod
    def _g(tau, fs):
        return math.tan(1.0 / (2.0 * fs * tau))   # TPT one-pole gain

    def process(self, s, i_lad, k):
        n = len(s)
        out = np.empty(n)
        vca = np.empty(n)
        fs = self.fs
        h2 = 0.5 / fs
        g_out = self._g(TAU_OUT, fs)
        if self.block_net:
            return self._process_block(s, i_lad, k)
        net = make_net(k, self.net_order)
        tpo = np.empty(n)
        Ad, Bd = net.discrete(fs)
        Cn, Dn = net.C, net.D
        # u+ = Cu (Ad z + Bd w) + (Cu Bd + Du) w+   (w = (tp_pre, s))
        CuAd, CuBd = Cn[0] @ Ad, Cn[0] @ Bd
        Ku = CuBd + Dn[0]                       # instantaneous gains of u on w+
        w_prev = self.w_prev if hasattr(self, "w_prev") else np.array([0.0, s[0]])
        z = self.znet
        for j in range(n):
            ie = i_lad[j] * (prewarp(i_lad[j], k, fs) if self.warp else 1.0)
            w = [h2 * ie / (4 * VT * c) for c in C_LADDER]   # (h/2) w_k
            x = self.x
            ts = self.t_st if self.stage_nl else [1.0, 1.0, 1.0, 1.0]
            tin, tout = self.t_in, self.t_out
            f_old = [-ts[0] * (x[0] - x[1]), ts[0] * (x[0] - x[1]) - ts[1] * (x[1] - x[2]),
                     ts[1] * (x[1] - x[2]) - ts[2] * (x[2] - x[3]), ts[2] * (x[2] - x[3]) - ts[3] * x[3]]
            r = [x[q] + w[q] * f_old[q] for q in range(4)]
            r[0] += w[0] * (-tin * self.xin_prev)
            wl = h2 * W_LOAD
            r[3] += -wl * self.vhp_prev / (2 * VT) + wl * self.z_out / (2 * VT * (1 + g_out))
            d0 = 1 + w[0] * ts[0]
            d1 = 1 + w[1] * (ts[0] + ts[1])
            d2 = 1 + w[2] * (ts[1] + ts[2])
            d3 = 1 + w[3] * (ts[2] + ts[3]) + wl / (1 + g_out)
            l1, l2, l3 = -w[1] * ts[0], -w[2] * ts[1], -w[3] * ts[2]
            u0, u1, u2 = -w[0] * ts[0], -w[1] * ts[1], -w[2] * ts[2]
            p0 = d0
            p1 = d1 - l1 * u0 / p0
            p2 = d2 - l2 * u1 / p1
            p3 = d3 - l3 * u2 / p2
            yr = [r[0]]
            yq = [-w[0] * tin]
            for q, (lq, pp) in enumerate(((l1, p0), (l2, p1), (l3, p2)), start=1):
                yr.append(r[q] - lq * yr[-1] / pp)
                yq.append(0.0 - lq * yq[-1] / pp)
            piv = (p0, p1, p2, p3)
            ups = (u0, u1, u2)
            pr = [0.0] * 4
            pq = [0.0] * 4
            pr[3], pq[3] = yr[3] / p3, yq[3] / p3
            for q in (2, 1, 0):
                pr[q] = (yr[q] - ups[q] * pr[q + 1]) / piv[q]
                pq[q] = (yq[q] - ups[q] * pq[q + 1]) / piv[q]
            # tp_pre+ = -A tout vhp+ / 2Vt,  vhp+ = (2Vt x3+ - z_out) / (1 + g_out)
            #   -> tp_pre+ = at x3+ + bt
            at = -A_OUT * tout / (1 + g_out)
            bt = A_OUT * tout * self.z_out / (2 * VT * (1 + g_out))
            # u+ = base + Ku[0] tp_pre+ ,  base = CuAd z + CuBd w_prev + Ku[1] s+
            base = CuAd @ z + CuBd @ w_prev + Ku[1] * s[j]
            # 2Vt xin = base + Ku0 (at (pr3 + pq3 xin) + bt)
            xin = (base + Ku[0] * (at * pr[3] + bt)) / (2 * VT - Ku[0] * at * pq[3])
            xn = [pr[q] + pq[q] * xin for q in range(4)]
            v3 = 2 * VT * xn[3]
            vhp = (v3 - self.z_out) / (1 + g_out)
            tp_pre = -A_OUT * math.tanh(vhp / (2 * VT))
            w_now = np.array([tp_pre, s[j]])
            z = Ad @ z + Bd @ (w_prev + w_now)
            y = Cn @ z + Dn @ w_now
            out[j] = y[2]                     # TP6
            vca[j] = y[1]
            tpo[j] = tp_pre
            self.z_out += 2 * g_out * vhp
            self.t_in = _t(xin)
            self.t_out = _t(vhp / (2 * VT))
            dif = (xn[0] - xn[1], xn[1] - xn[2], xn[2] - xn[3], xn[3])
            self.t_st = [_t(dz) for dz in dif]
            self.max_stage = max(self.max_stage, max(abs(dz) for dz in dif))
            self.max_out = max(self.max_out, abs(vhp) / (2 * VT))
            self.xin_prev = xin
            self.vhp_prev = vhp
            self.x = xn
            w_prev = w_now
        self.w_prev = w_prev
        self.znet = z
        self.vca_in = vca
        self.tp_pre = tpo
        return out


    def _ladder_step(self, j, s_j, i_j, base_fn, Ku0, g_out):
        """One sample of the ladder with the loop closed through
        u+ = base + Ku0 tp_pre+ (base from the network). Returns
        (tp_pre, xin, vhp, xn)."""
        h2 = 0.5 / self.fs
        w = [h2 * i_j / (4 * VT * c) for c in C_LADDER]   # i_j already prewarped
        x = self.x
        ts = self.t_st if self.stage_nl else [1.0, 1.0, 1.0, 1.0]
        tin, tout = self.t_in, self.t_out
        f_old = [-ts[0] * (x[0] - x[1]), ts[0] * (x[0] - x[1]) - ts[1] * (x[1] - x[2]),
                 ts[1] * (x[1] - x[2]) - ts[2] * (x[2] - x[3]), ts[2] * (x[2] - x[3]) - ts[3] * x[3]]
        r = [x[q] + w[q] * f_old[q] for q in range(4)]
        r[0] += w[0] * (-tin * self.xin_prev)
        wl = h2 * W_LOAD
        r[3] += -wl * self.vhp_prev / (2 * VT) + wl * self.z_out / (2 * VT * (1 + g_out))
        d0 = 1 + w[0] * ts[0]
        d1 = 1 + w[1] * (ts[0] + ts[1])
        d2 = 1 + w[2] * (ts[1] + ts[2])
        d3 = 1 + w[3] * (ts[2] + ts[3]) + wl / (1 + g_out)
        l1, l2, l3 = -w[1] * ts[0], -w[2] * ts[1], -w[3] * ts[2]
        u0, u1, u2 = -w[0] * ts[0], -w[1] * ts[1], -w[2] * ts[2]
        p0 = d0
        p1 = d1 - l1 * u0 / p0
        p2 = d2 - l2 * u1 / p1
        p3 = d3 - l3 * u2 / p2
        yr = [r[0]]
        yq = [-w[0] * tin]
        for q, (lq, pp) in enumerate(((l1, p0), (l2, p1), (l3, p2)), start=1):
            yr.append(r[q] - lq * yr[-1] / pp)
            yq.append(0.0 - lq * yq[-1] / pp)
        piv = (p0, p1, p2, p3)
        ups = (u0, u1, u2)
        pr = [0.0] * 4
        pq = [0.0] * 4
        pr[3], pq[3] = yr[3] / p3, yq[3] / p3
        for q in (2, 1, 0):
            pr[q] = (yr[q] - ups[q] * pr[q + 1]) / piv[q]
            pq[q] = (yq[q] - ups[q] * pq[q + 1]) / piv[q]
        at = -A_OUT * tout / (1 + g_out)
        bt = A_OUT * tout * self.z_out / (2 * VT * (1 + g_out))
        base = base_fn()
        xin = (base + Ku0 * (at * pr[3] + bt)) / (2 * VT - Ku0 * at * pq[3])
        xn = [pr[q] + pq[q] * xin for q in range(4)]
        vhp = (2 * VT * xn[3] - self.z_out) / (1 + g_out)
        tp_pre = -A_OUT * math.tanh(vhp / (2 * VT))
        self.z_out += 2 * g_out * vhp
        self.t_in = _t(xin)
        self.t_out = _t(vhp / (2 * VT))
        dif = (xn[0] - xn[1], xn[1] - xn[2], xn[2] - xn[3], xn[3])
        self.t_st = [_t(dz) for dz in dif]
        self.max_stage = max(self.max_stage, max(abs(dz) for dz in dif))
        self.max_out = max(self.max_out, abs(vhp) / (2 * VT))
        self.xin_prev = xin
        self.vhp_prev = vhp
        self.x = xn
        return tp_pre

    def _process_block(self, s, i_lad, k):
        """Real-time form (SplitNet): fast modes and the slow 1/s asymptotes
        per sample, the slow remainder per block."""
        n = len(s)
        out = np.full(n, np.nan)               # TP6 is not carried
        vca = np.empty(n)
        tpo = np.empty(n)
        g_out = self._g(TAU_OUT, self.fs)
        sn = SplitNet(k, self.fs, BLOCK)
        if getattr(self, "zblk", None) is None:
            self.zblk = sn.dc_state(self.s_dc)
            nf = len(sn.p)
            self.yf = (sn.bf @ np.array([0.0, self.s_dc])) / sn.p      # fast modes at DC
            self.qa = sn.ga @ np.array([0.0, self.s_dc]) / sn.a_uv     # asymptote poles at DC
            self.w_last = np.array([0.0, self.s_dc])
        z, yf, qa, w_last = self.zblk, self.yf, self.qa, self.w_last
        r_now = sn.Ca @ z
        r_prev = getattr(self, "r_prev", r_now)
        # instantaneous gains of u on tp_pre+ and s+ (D plus each one-pole's share)
        Ku = sn.D[0] + (sn.cf[0] * sn.be_f) @ sn.bf + sn.be_a[0] * sn.ga[0]
        Kv = sn.D[1] + (sn.cf[1] * sn.be_f) @ sn.bf + sn.be_a[1] * sn.ga[1]
        for b0 in range(0, n, BLOCK):
            b1 = min(n, b0 + BLOCK)
            acc = np.zeros(2)
            for j in range(b0, b1):
                fr = (j - b0 + 1) / BLOCK
                r = r_prev + (r_now - r_prev) * fr
                sj = s[j]
                # the parts of the one-poles known before tp_pre+
                yf_k = sn.al_f * yf + sn.be_f * (sn.bf @ w_last)
                qa_k = sn.al_a * qa + sn.be_a * (sn.ga @ w_last)
                base_u = r[0] + sn.cf[0] @ yf_k + qa_k[0] + Ku[1] * sj
                ie = i_lad[j] * (prewarp(i_lad[j], k, self.fs) if self.warp else 1.0)
                tp = self._ladder_step(j, sj, ie, lambda: base_u, Ku[0], g_out)
                w_now = np.array([tp, sj])
                yf = yf_k + sn.be_f * (sn.bf @ w_now)
                qa = qa_k + sn.be_a * (sn.ga @ w_now)
                vca[j] = r[1] + sn.cf[1] @ yf + qa[1] + sn.D[1] @ w_now
                tpo[j] = tp
                w_last = w_now
                acc += w_now
            z = sn.Phi @ z + sn.Gam @ (acc / (b1 - b0))
            r_prev, r_now = r_now, sn.Ca @ z
        self.zblk, self.yf, self.qa, self.w_last, self.r_prev = z, yf, qa, w_last, r_prev
        self.vca_in = vca
        self.tp_pre = tpo
        return out


# ============================================================== VCA audio path
def vca_out(v_in, i_abc):
    """OTA: I_out = I_abc tanh(v / 2Vt) into the output load (volts)."""
    return R_OTA_LOAD * i_abc * np.tanh(v_in / (2 * VT))
