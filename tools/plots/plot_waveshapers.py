#!/usr/bin/env python3
"""Interpretation of the proposed filter drive types (`dty`), S084 P1.

Outputs:
  waveshapers_static.png    memoryless shapers: transfer curve + sine response
  waveshapers_stateful.png  shapers with state (zero-crossing logic, trackers):
                            steady sine and a kick-like decaying pitch glide

Each type replaces the filter's input soft clip g(u), u = in * drive,
drive = 0.4 + 6*(drv/127)^2 (today's mapping, unchanged). The ZDF filter then
runs on the shaped signal. Types (user list, numbered as in
reference/waveshapers.png, then four additional folders):

  sft  0  today's Pade soft clip ~ 2*tanh(u/2)                      (reference)
  rct  1  Rectifying: |u| (octave up), soft-limited, DC-blocked
  art  2  Alternate rectifying: positive half-cycles of every other cycle
          (toggle on each rising zero crossing) -> octave down, DC-blocked
  thf  3  Threshold fold: triangle fold above a threshold (0.6), repeating
  fdc  4  Fifth-down crunch: one rectified half-cycle out of every three (zero-crossing
          counter mod 3) -> fifth down, then a hard-clip crunch, DC-blocked
  bsw  5  Swap: 8-bit quantise, swap the low and high nibbles
  tsw  6  Tracking saw: a falling ramp reset at each rising zero crossing,
          slope from the last measured period, amplitude latched from the
          previous cycle's peak
  lsw  7  Lo saw: the same, reset every second crossing (octave down)
  lsq  8  Lo square: flip-flop toggled at each rising crossing (octave down)
  sfd  9  sine fold sin(pi/2*u): smooth multi-fold
  afd  9  accelerating fold sin(pi/2*(u + 0.05u^3)): the folds crowd
          together as the level rises
  asf  9  asymmetric fold: triangle fold of (u + 0.35), offset removed ->
          even harmonics (a fixed "symmetry" setting)
  chb  9  Chebyshev: 3x - 4x^3 on a clipped x: drive morphs the fundamental
          into its 3rd harmonic
  rsb  9  ring-sub: input multiplied by the lo-square tracker -> a ring tone
          with a sub-octave component

Zero-crossing logic uses a small hysteresis (0.03 of full scale). Shapes
that create DC (rct, art, fdc) run a one-pole DC blocker at about 10 Hz. The
trackers compute 1/period at control rate (once per 32-sample block), so
there is no per-sample division. All outputs are level-trimmed so a sine at
drv 64 has the same RMS as `sft`, capped so the drv-127 peak stays within sft's
ceiling of 2.0 (the trim is printed and shown in titles).
"""
import math
import argparse
import json

import numpy as np

import lxr_plotlib as L

BLOCK = 32
HYST = 0.03
ENV_REL = math.exp(-1.0 / (0.02 * L.FS))   # 20 ms release (before the first crossing)
DC_R = 0.9986                              # DC blocker pole (~10 Hz)


def soft(u):
    return L.filter_input_stage(u) * 0.5   # ~tanh(u/2), peak 1


def tri_fold(u, t):
    """Periodic triangle fold: identity for |u| <= t, mirrored beyond."""
    return t - 4 * t * np.abs(((u + t) / (4 * t)) % 1.0 - 0.5)


class Shaper:
    """Per-sample model of one drive type with its state."""

    def __init__(self, kind):
        self.kind = kind
        self.reset()

    def reset(self):
        self.prev = 0.0
        self.armed = True          # hysteresis: ready to detect a rising crossing
        self.neg_armed = True
        self.toggle = False
        self.count = 0
        self.ramp = 1.0
        self.env = 0.0
        self.cur_max = 0.0
        self.latched = False
        self.period = 200.0
        self.since = 0
        self.inc_block = 1.0 / 200.0
        self.dc_x = self.dc_y = 0.0
        self.n = 0

    def _crossings(self, u):
        rise = fall = False
        if self.armed and u > HYST and self.prev <= HYST:
            rise, self.armed = True, False
        if u < -HYST:
            self.armed = True
        if self.neg_armed and u < -HYST and self.prev >= -HYST:
            fall, self.neg_armed = True, False
        if u > HYST:
            self.neg_armed = True
        self.prev = u
        return rise, fall

    def _dc(self, x):
        y = x - self.dc_x + DC_R * self.dc_y
        self.dc_x, self.dc_y = x, y
        return y

    def process(self, u):
        k = self.kind
        if self.n % BLOCK == 0:                     # control-rate reciprocal
            self.inc_block = 1.0 / max(self.period, 4.0)
        self.n += 1
        if k == "sft":
            return float(L.filter_input_stage(u))
        if k == "rct":
            return self._dc(2 * float(soft(abs(u))))
        if k == "thf":
            return float(tri_fold(u, 0.6)) / 0.6
        if k == "bsw":
            q = int(round((float(soft(u)) + 1) * 127.5))
            q = max(0, min(255, q))
            r = ((q & 0x0F) << 4) | (q >> 4)
            return r / 127.5 - 1.0
        if k == "sfd":
            return math.sin(math.pi / 2 * u)
        if k == "afd":
            return math.sin(math.pi / 2 * (u + 0.05 * u ** 3))
        if k == "asf":
            return float(tri_fold(u + 0.35, 1.0) - tri_fold(0.35, 1.0))
        if k == "chb":
            x = float(soft(u))
            return 3 * x - 4 * x ** 3

        # Stateful types.
        s = float(soft(u))
        rise, fall = self._crossings(u)
        self.since += 1
        self.cur_max = max(self.cur_max, abs(s))
        if rise:
            self.period = self.since
            self.since = 0
            # Amplitude latch: the trackers take the previous cycle's peak, so
            # their level is flat within a cycle and follows decays per cycle.
            self.env = self.cur_max
            self.cur_max = 0.0
            self.latched = True
        if not self.latched:
            self.env = max(abs(s), self.env * ENV_REL)
        if k == "art":
            if rise:
                self.toggle = not self.toggle
            return self._dc(2 * s if (s > 0 and self.toggle) else 0.0)
        if k == "fdc":
            if rise or fall:
                self.count = (self.count + 1) % 3
            y = 2 * abs(s) if self.count == 0 else 0.0
            return self._dc(min(y, 1.3) / 1.3 * 2)
        if k in ("tsw", "lsw"):
            if rise:
                self.toggle = not self.toggle
                if k == "tsw" or self.toggle:
                    self.ramp = 0.0
            step = self.inc_block if k == "tsw" else 0.5 * self.inc_block
            self.ramp = min(1.0, self.ramp + step)
            return 2 * self.env * (1 - 2 * self.ramp)
        if k in ("lsq", "rsb"):
            if rise:
                self.toggle = not self.toggle
            sq = 1.0 if self.toggle else -1.0
            return 2 * self.env * sq if k == "lsq" else 2 * s * sq
        raise ValueError(k)

    def run(self, u):
        self.reset()
        return np.array([self.process(float(v)) for v in u])


STATIC = [("sft", "0 · sft — today's soft clip (reference)"),
          ("rct", "1 · rct — Rectifying (octave up)"),
          ("thf", "3 · thf — threshold fold"),
          ("bsw", "5 · bsw — Swap (nibble swap)"),
          ("sfd", "9 · sfd — sine fold"),
          ("afd", "9 · afd — accelerating fold"),
          ("asf", "9 · asf — asymmetric fold"),
          ("chb", "9 · chb — Chebyshev 3rd")]
STATEFUL = [("art", "2 · art — Alternate rectifying (octave down)"),
            ("fdc", "4 · fdc — fifth-down crunch"),
            ("tsw", "6 · tsw — Tracking saw"),
            ("lsw", "7 · lsw — Lo saw (octave down)"),
            ("lsq", "8 · lsq — Lo square (octave down)"),
            ("rsb", "9 · rsb — ring-sub (input × lo square)")]

DRVS = [40, 80, 127]


def sine(freq, cycles, amp=1.0, settle_cycles=0):
    n = int(round(L.FS / freq * (cycles + settle_cycles)))
    t = np.arange(n) / L.FS
    return t, amp * np.sin(2 * np.pi * freq * t)


def kick(n_ms=180):
    n = int(L.FS * n_ms / 1000)
    t = np.arange(n) / L.FS
    f = 55 + 95 * np.exp(-t / 0.04)
    ph = 2 * np.pi * np.cumsum(f) / L.FS
    return t, np.exp(-t / 0.09) * np.sin(ph)


def level_trims():
    """Gain per type: match sft's RMS on a sine at drv 64, but never let the
    peak at drv 127 exceed sft's ceiling (2.0) — sparse shapes (art, fdc)
    would otherwise be trimmed far too hot."""
    _, x = sine(220.0, 40, settle_cycles=20)
    ref = None
    trims = {}
    for k, _ in [("sft", "")] + STATIC[1:] + STATEFUL:
        y = Shaper(k).run(x * L.filt_drive(64))[len(x) // 2:]
        y127 = Shaper(k).run(x * L.filt_drive(127))[len(x) // 2:]
        rms = math.sqrt(float(np.mean(y ** 2)))
        if k == "sft":
            ref = rms
        t_rms = ref / rms if rms > 1e-9 else 1.0
        t_peak = 2.0 / max(float(np.max(np.abs(y127))), 1e-9)
        trims[k] = min(t_rms, t_peak)
    return trims


def export_ref(dirpath):
    """Write (u, s, expected out) float32 cases for test_shaper.c."""
    import os

    os.makedirs(dirpath, exist_ok=True)
    trims = level_trims()
    _, xs = sine(220.0, 4, settle_cycles=20)
    _, xk = kick()
    cases = [("sine40", xs, 40), ("sine80", xs, 80),
             ("sine127", xs, 127), ("kick110", xk, 110)]
    kinds = ["sft", "rct", "art", "thf", "fdc", "bsw", "tsw",
             "lsw", "lsq", "sfd", "afd", "asf", "chb", "rsb"]
    index = []

    for signal, x, drive in cases:
        u = np.asarray(x, dtype=np.float64) * L.filt_drive(drive)
        s = np.asarray([L.filter_input_stage(float(v)) for v in u])
        for kind in kinds:
            sh = Shaper(kind)
            y = sh.run(u)
            out = y * trims[kind]
            base = "%s_%s" % (signal, kind)
            for suffix, arr in (("u", u), ("s", s), ("out", out)):
                arr.astype("<f4").tofile(os.path.join(dirpath,
                                                      "%s_%s.bin" % (base, suffix)))
            index.append({"signal": signal, "type": kind,
                          "count": int(len(u)), "base": base})
    with open(os.path.join(dirpath, "index.json"), "w") as fh:
        json.dump(index, fh, indent=1)
    print("wrote shaper reference to", dirpath)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit-c", action="store_true",
                    help="print the driveShaper_trim initializer")
    ap.add_argument("--export-ref", metavar="DIR",
                    help="write float32 (u, s, out) cases for test_shaper")
    args = ap.parse_args()
    if args.emit_c:
        trims = level_trims()
        kinds = ["sft", "rct", "art", "thf", "fdc", "bsw", "tsw",
                 "lsw", "lsq", "sfd", "afd", "asf", "chb", "rsb"]
        print("const float driveShaper_trim[DRIVE_TYPE_COUNT] = {")
        for kind in kinds:
            print("    %.6ff, /* %s %+.2f dB */" %
                  (trims[kind], kind, 20 * math.log10(trims[kind])))
        print("};")
        return
    if args.export_ref:
        export_ref(args.export_ref)
        return

    plt = L.setup_matplotlib()
    trims = level_trims()
    print("level trims (dB) vs sft at drv 64:")
    for k, v in trims.items():
        print(f"  {k}: {20 * math.log10(v):+.1f}")
    cols = [L.PALETTE[2], L.PALETTE[3], L.PALETTE[4]]

    # ---- static figure
    fig, ax = plt.subplots(len(STATIC), 2, figsize=(13, 2.7 * len(STATIC)),
                           gridspec_kw={"width_ratios": [1, 2]})
    xin = np.linspace(-1, 1, 801)
    _, xs = sine(220.0, 2, settle_cycles=20)
    keep = int(round(L.FS / 220.0 * 2))
    for r, (k, title) in enumerate(STATIC):
        tr = trims[k]
        a = ax[r, 0]
        for d_byte, c in zip(DRVS, cols):
            d = L.filt_drive(d_byte)
            sh = Shaper(k)
            y = np.array([sh.process(float(v)) for v in xin * d]) if k != "rct" else \
                2 * soft(np.abs(xin * d))
            a.plot(xin, y * tr, color=c, label=f"drv {d_byte}")
        a.set_title(f"{title}", loc="left")
        a.set_xlim(-1, 1)
        a.set_ylim(-2.6, 2.6)
        a.set_xlabel("input sample")
        a.set_ylabel("to filter")
        a.legend(loc="lower right", ncol=3)
        a = ax[r, 1]
        a.plot(np.arange(keep) / keep * 2, xs[-keep:], color="#c3cad2", lw=1.2, label="input")
        for d_byte, c in zip(DRVS, cols):
            y = Shaper(k).run(xs * L.filt_drive(d_byte))[-keep:]
            a.plot(np.arange(keep) / keep * 2, y * tr, color=c, lw=1.3, label=f"drv {d_byte}")
        a.set_title(f"sine response (level trim {20 * math.log10(tr):+.1f} dB)", loc="left", fontsize=9.5)
        a.set_xlim(0, 2)
        a.set_ylim(-2.6, 2.6)
        a.set_xlabel("two cycles of a 220 Hz full-scale sine")
        a.legend(loc="upper right", ncol=4)
    fig.suptitle("Filter drive types — memoryless shapers (input stage, before the filter)", fontsize=14,
                 fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.985))
    fig.savefig(L.out_path("waveshapers_static.png"), dpi=105)

    # ---- stateful figure
    fig, ax = plt.subplots(len(STATEFUL), 2, figsize=(13, 2.7 * len(STATEFUL)),
                           gridspec_kw={"width_ratios": [1.4, 1]})
    _, xs = sine(220.0, 6, settle_cycles=20)
    keep = int(round(L.FS / 220.0 * 6))
    tk, xk = kick()
    for r, (k, title) in enumerate(STATEFUL):
        tr = trims[k]
        a = ax[r, 0]
        a.plot(np.arange(keep) / keep * 6, xs[-keep:], color="#c3cad2", lw=1.2, label="input")
        for d_byte, c in zip([80, 127], cols[1:]):
            y = Shaper(k).run(xs * L.filt_drive(d_byte))[-keep:]
            a.plot(np.arange(keep) / keep * 6, y * tr, color=c, lw=1.3, label=f"drv {d_byte}")
        a.set_title(f"{title}  (trim {20 * math.log10(tr):+.1f} dB)", loc="left")
        a.set_xlim(0, 6)
        a.set_ylim(-2.4, 2.4)
        a.set_xlabel("six cycles of a 220 Hz sine")
        a.legend(loc="upper right", ncol=3)
        a = ax[r, 1]
        a.plot(tk * 1000, xk, color="#c3cad2", lw=1.0, label="kick-like input (150→55 Hz glide)")
        y = Shaper(k).run(xk * L.filt_drive(110))
        a.plot(tk * 1000, y * tr, color=L.PALETTE[1], lw=1.1, label="drv 110")
        a.set_xlim(0, tk[-1] * 1000)
        a.set_ylim(-2.4, 2.4)
        a.set_xlabel("ms")
        a.set_title("tracking a decaying pitch glide", loc="left", fontsize=9.5)
        a.legend(loc="upper right")
    fig.suptitle("Filter drive types — shapers with state (zero-crossing logic and trackers)", fontsize=14,
                 fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.985))
    fig.savefig(L.out_path("waveshapers_stateful.png"), dpi=105)
    print("wrote waveshapers_static.png and waveshapers_stateful.png")


if __name__ == "__main__":
    main()
