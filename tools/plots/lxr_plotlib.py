"""Shared helpers for the tools/plots scripts (S084 P1 voice-expansion plots).

What lives here:
  - Python ports of the firmware drive stages as they are in the S084 tree
    (ResonantFilter.c, distortion.h, the IM_SPECIAL_FILTER_* writers);
  - band-limiting helpers (harmonic truncation, as the octave tables do);
  - digitisers for the reference images in reference/ (the 303 and dirty
    triangle screenshots supplied by the user);
  - a common matplotlib style.

These are design/illustration tools, not part of the firmware build. They
need numpy, matplotlib and pillow (see README.md).
"""
import math
import os

import numpy as np

FS = 44108.0                      # LXR-02 sample rate (PLLI2S N=271, R=2)
HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "reference")

# Wavetable octave edges used by freqToTableIndex() (Oscillator.c).
OCTAVE_EDGES_HZ = [16.3515987, 32.7031975, 65.406395, 130.81279, 261.62558,
                   523.25116, 1046.50232, 2093.00464, 4186.00928, 8372.01855]


# --------------------------------------------------------------------------
# Firmware ports
# --------------------------------------------------------------------------
def filt_drive(v):
    """SVF_setDrive(): drive = 0.4 + 6*(v/127)^2."""
    return 0.4 + (v / 127.0) ** 2 * 6.0


def pade_num(a):
    return (a / 945.0 + 105.0 / 945.0) * a + 1.0


def pade_den(a):
    return (a * 15.0 / 945.0 + 420.0 / 945.0) * a + 1.0


def filter_input_stage(u):
    """Batched ZDF Stage A input clip: x = u*N(u^2/4)/D(u^2/4) ~ 2*tanh(u/2)."""
    a = 0.25 * u * u
    return u * pade_num(a) / pade_den(a)


def fast_tanh(v):
    v = np.asarray(v, dtype=float)
    return np.where(v < -1.95, -1.0, np.where(v > 1.95, 1.0, 4.15 * v / (4.29 + v * v)))


def fast_tan(x):
    return (-15 * x + x ** 3) / (3 * (-5 + 2 * x * x))


def shaper_f2f(x, s):
    """valueShaperF2F(): (1+k)x/(1+k|x|), k = 2s/(1.0001-s)."""
    k = 2 * s / (1.0001 - s)
    return (1 + k) * x / (1 + k * abs(x))


def dist_k(d):
    """setDistortionShape(): k = 2(d/128)/(1-d/128)."""
    return 2 * (d / 128.0) / (1 - d / 128.0)


def dist_curve(x, d):
    """distortion_curveSample16() in normalised units."""
    k = dist_k(d)
    return (1 + k) * x / (1 + k * np.abs(x))


def ssat16(v):
    return max(-32768, min(32767, int(v)))


class ZdfFilter:
    """Float64 port of SVF_calcBlockZDF() (batched two-division solver)."""

    def __init__(self, freq_byte, reso_byte, drive_byte):
        self.f = shaper_f2f(freq_byte / 127.0, -0.9) * 0.45
        self.g = fast_tan(math.pi * self.f)
        q = 1 - reso_byte / 127.0
        self.q = 0.02 if q < 0.1 else q
        self.drive = filt_drive(drive_byte)
        self.s1 = self.s2 = self.zi = 0.0

    def run(self, ftype, samples):
        f, ff = self.g, self.g * self.g
        R = 1.0 if self.f >= 0.4499 else self.q
        s1, s2, zi = self.s1, self.s2, self.zi
        out = np.empty(len(samples))
        for i, b in enumerate(samples):
            u = (b / 32767.0) * self.drive
            ax, a1 = 0.25 * u * u, 0.25 * s1 * s1
            Nx, Dx, N1, D1 = pade_num(ax), pade_den(ax), pade_num(a1), pade_den(a1)
            invA = 1.0 / (Dx * D1)
            x = u * Nx * D1 * invA
            t1 = N1 * Dx * invA
            ih = 0.5 * (x + zi)
            zi = x
            v0 = 0.5 * (ih - 2 * R * s1 - s2)
            a0 = v0 * v0
            N0, D0 = pade_num(a0), pade_den(a0)
            E = D0 + f * 2 * R * N0
            P = ff * N0 * t1
            F = P + E
            Q = P * x + s2 * E + f * D0 * t1 * s1
            invB = 1.0 / (D0 * E * F)
            t0, g0, y1 = N0 * E * F * invB, D0 * D0 * F * invB, Q * D0 * E * invB
            s1t1 = s1 * t1
            xx = t0 * (x - y1)
            y0 = (s1t1 + f * xx) * g0
            s1 = s1t1 + 2 * f * (xx - t0 * 2 * R * y0)
            s2 = s2 + 2 * f * t1 * y0
            if ftype == "LP":
                o = ssat16(float(fast_tanh(y1)) * 0x7FFF)
            elif ftype == "HP":
                o = ssat16((x - 2 * R * y0 - y1) * 0x70FF)
            else:  # BP
                o = ssat16(y0 * 0x70FF)
            out[i] = o / 32767.0
        self.s1, self.s2, self.zi = s1, s2, zi
        return out


def filter_one_cycle(ftype, freq_byte, reso_byte, drive_byte, tone_hz):
    """Steady-state single cycle of a full-scale sine through the ZDF filter."""
    flt = ZdfFilter(freq_byte, reso_byte, drive_byte)
    n_settle = int(FS * 0.2)
    n_cycle = int(round(FS / tone_hz))
    n = np.arange(n_settle + n_cycle)
    x = np.clip(np.round(32767 * np.sin(2 * np.pi * tone_hz * n / FS)), -32768, 32767)
    y = flt.run(ftype, x)
    return np.arange(n_cycle) / n_cycle, y[n_settle:]


# --------------------------------------------------------------------------
# Band-limiting
# --------------------------------------------------------------------------
def harmonics_of(cycle):
    """Complex harmonic coefficients c[k], k = 0..N/2, of one cycle."""
    return np.fft.rfft(cycle) / len(cycle)


def from_harmonics(c, n, hmax=None):
    """Rebuild n samples from coefficients, keeping harmonics 1..hmax (no DC)."""
    c = np.array(c, dtype=complex)
    full = np.zeros(n // 2 + 1, dtype=complex)
    m = min(len(c), len(full))
    full[:m] = c[:m]
    full[0] = 0.0
    if hmax is not None:
        full[hmax + 1:] = 0.0
    return np.fft.irfft(full * n, n)


def bandlimit(cycle, hmax, n=None):
    n = n or len(cycle)
    return from_harmonics(harmonics_of(cycle), n, hmax)


def harmonics_for_note(f_hz, compression=1.0):
    """Highest harmonic that stays below Nyquist for a note (with warp)."""
    return max(1, int((FS / 2) / (f_hz * compression)))


def extend_harmonics(c, h_meas, h_max):
    """Extend measured harmonics 1..h_meas up to h_max with a fitted roll-off.

    Magnitudes follow a power law fitted over the upper half of the measured
    band; phases continue the fitted linear phase trend. Used because the
    reference screenshots resolve only the first few dozen harmonics.
    """
    c = np.array(c[: h_meas + 1], dtype=complex)
    k = np.arange(max(2, h_meas // 2), h_meas + 1)
    mag = np.abs(c[k]) + 1e-12
    slope, icpt = np.polyfit(np.log(k), np.log(mag), 1)
    slope = min(slope, -1.0)                       # never brighter than a saw
    ph = np.unwrap(np.angle(c[k]))
    pslope, picpt = np.polyfit(k, ph, 1)
    out = np.zeros(h_max + 1, dtype=complex)
    out[: h_meas + 1] = c
    kk = np.arange(h_meas + 1, h_max + 1)
    out[h_meas + 1:] = np.exp(icpt + slope * np.log(kk)) * np.exp(1j * (picpt + pslope * kk))
    return out


def normalise(cycle):
    cycle = np.asarray(cycle, dtype=float) - np.mean(cycle)
    return cycle / np.max(np.abs(cycle))


def rotate_to(cycle, index):
    return np.roll(cycle, -int(index))


# --------------------------------------------------------------------------
# Digitisers for the supplied screenshots
# --------------------------------------------------------------------------
def _load_rgb(name):
    from PIL import Image
    return np.asarray(Image.open(os.path.join(REF, name)).convert("RGB")).astype(float)


def _fold(trace, x0, x1, period, n_out):
    """Average every full cycle of trace[x0:x1] onto n_out phase bins."""
    xs = np.arange(x0, x1, dtype=float)
    ys = trace[x0:x1]
    ok = ~np.isnan(ys)
    xs, ys = xs[ok], ys[ok]
    phase = ((xs - x0) / period) % 1.0
    bins = np.linspace(0, 1, n_out, endpoint=False)
    order = np.argsort(phase)
    ph, yy = phase[order], ys[order]
    ph = np.concatenate([ph - 1, ph, ph + 1])
    yy = np.concatenate([yy, yy, yy])
    # Running mean over a narrow phase window, then sample at the bins.
    out = np.empty(n_out)
    w = 0.6 / period                               # ~0.6 px in phase units
    for i, b in enumerate(bins):
        sel = np.abs(ph - b) <= max(w, 0.5 / n_out)
        out[i] = np.mean(yy[sel]) if np.any(sel) else np.nan
    if np.any(np.isnan(out)):
        good = ~np.isnan(out)
        out = np.interp(bins, bins[good], out[good], period=1.0)
    return out


def _period(trace, x0, x1, min_lag=60):
    y = trace[x0:x1].copy()
    y = np.nan_to_num(y - np.nanmean(y))
    ac = np.correlate(y, y, mode="full")[len(y) - 1:]
    lag = min_lag + int(np.argmax(ac[min_lag: len(y) // 2]))
    # Parabolic refinement of the autocorrelation peak.
    a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
    return lag + 0.5 * (a - c) / (a - 2 * b + c)


def digitise_303(which, n_out=2048):
    """One cycle of the 303 square ('sq3') or saw ('sw3') screenshot.

    The screenshot has two grey panels with a blue trace and a dark zero
    line: square on the left (x 42..692, zero row 157) and saw on the right
    (x 756..1389, zero row 163). Returns (cycle, raw_trace_x, raw_trace_y,
    period_px) with the cycle normalised to peak 1 and DC removed.
    """
    a = _load_rgb("303_square_saw.png")
    blue = (a[:, :, 2] > 150) & (a[:, :, 0] < 140) & (a[:, :, 1] < 140)
    if which == "sq3":
        x0, x1, zero = 44, 690, 157
    else:
        x0, x1, zero = 800, 1388, 163   # skip the flat lead-in at the left
    trace = np.full(a.shape[1], np.nan)
    for x in range(x0, x1):
        ys = np.nonzero(blue[47:294, x])[0]
        if len(ys):
            trace[x] = zero - (np.mean(ys) + 47)
    period = _period(trace, x0, x1)
    cyc = _fold(trace, x0, x1, period, n_out)
    return cyc, np.arange(x0, x1), trace[x0:x1], period


def digitise_dirty_triangle(n_out=2048):
    """One cycle of the dirty-triangle scope screenshot ('tra').

    Grey trace on black with a grey graticule (rows/cols listed below).
    Graticule pixels are masked and the trace centroid is taken per column.
    """
    a = _load_rgb("tra_dirty_triangle.png").mean(axis=2)
    grid_cols = [40, 80, 119, 159, 197, 237, 277, 317, 355, 395, 435]
    grid_rows = [34, 73, 112, 151, 190, 230, 269, 308, 350]
    mask = np.ones_like(a, dtype=bool)
    for c in grid_cols:
        mask[:, c - 1: c + 2] = False
    for r in grid_rows:
        mask[r - 1: r + 2, :] = False
    x0, x1, y0, y1 = 42, 433, 100, 285
    zero = 190.0
    trace = np.full(a.shape[1], np.nan)
    for x in range(x0, x1):
        col = a[y0:y1, x]
        m = mask[y0:y1, x] & (col > 72)
        if np.any(m):
            w = (col - 60.0) * m
            trace[x] = zero - (np.sum(w * np.arange(y0, y1)) / np.sum(w))
    period = _period(trace, x0, x1, min_lag=50)
    cyc = _fold(trace, x0, x1, period, n_out)
    return cyc, np.arange(x0, x1), trace[x0:x1], period


# --------------------------------------------------------------------------
# Plot style
# --------------------------------------------------------------------------
PALETTE = ["#8a96a3", "#2f6fb0", "#1f9e89", "#d99a00", "#d1495b", "#7b3fa0", "#3a3a3a"]


def setup_matplotlib():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "axes.grid": True,
        "grid.color": "#e4e8ec",
        "grid.linewidth": 0.8,
        "axes.edgecolor": "#9aa3ad",
        "axes.titlesize": 11,
        "axes.titleweight": "bold",
        "axes.labelsize": 9,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "legend.fontsize": 7.5,
        "legend.frameon": False,
        "font.family": "DejaVu Sans",
    })
    return plt


def out_path(name):
    return os.path.join(HERE, name)
