#!/usr/bin/env python3
"""Fit the Alkaline MEG decay range (S093 P6).

What:       finds the two resistances of the decay law
              r = r_min + r_span x taper(decay)
            (R138 68 k + VR6 1 M on the 303) so that the MEG's time from the
            peak to 10 % is 0.80 x the stock time at decay 0 (-20 %, user) and
            1.50 x the stock time at decay 1 (+50 %, user). The stock times
            come from the same model with r_min = 68 k and r_span = 1 M; each
            target is solved by bisection on r with the reference MEG (the
            diode stack makes the time not proportional to r). The firmware's
            one-Newton-step MEG (FAST_CONTROL) gives the same r to within one
            block of t10 (measured, S093 P6).
Why:        S093_P6_PARAMETER_TWEAKS.md §3.2: dec 0 173 ms -> ~138 ms,
            dec 1 2.54 s -> ~3.81 s.
Inputs:     tools/alkaline/alk_constants.json (meg section); --check.
Outputs:    meg.r_min, meg.r_span written into alk_constants.json (other keys
            unchanged); a report. --check exits 1 when the stored values
            differ from a fresh fit by more than 0.1 %.
Accessors:  python3 tools/alkaline/fit_meg_range.py [--check]; then
            gen_alk_tables.py emits ALK_MEG_R_MIN / ALK_MEG_R_SPAN.
Affiliates: alk_model.Meg (reads the two values); validate.py V3b.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv, _argv = sys.argv[:1], sys.argv
sys.path.insert(0, HERE)
import alk_model as M   # noqa: E402

PATH = os.path.join(HERE, "alk_constants.json")
SHORT, LONG = 0.80, 1.50          # the user's -20 % / +50 %


def t10_at_r(r):
    """MEG time from the peak to 10 % (s) with the discharge resistance r."""
    mg = M.Meg()
    mg.r_min, mg.r_span = r, 0.0          # decay knob irrelevant: r fixed
    rest = mg.n53()
    mg.trigger()
    dt = M.BLOCK / M.FS
    t, peak, tp = 0.0, None, 0.0
    while t < 8.0:
        v = mg.step(dt, 0.0, False)
        t += dt
        if peak is None or v > peak:
            peak, tp = v, t
        elif v < rest + 0.1 * (peak - rest):
            return t - tp
    return float("inf")


def solve(target):
    """r with t10_at_r(r) = target (t10 rises with r): bisection to 0.01 %."""
    lo, hi = 1e3, 2e7
    for _ in range(60):
        mid = (lo * hi) ** 0.5
        if t10_at_r(mid) < target:
            lo = mid
        else:
            hi = mid
    return (lo * hi) ** 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(_argv[1:])
    stock_min, stock_max = t10_at_r(68e3), t10_at_r(68e3 + 1e6)
    r_min = solve(SHORT * stock_min)
    r_max = solve(LONG * stock_max)
    print(f"stock t10 {stock_min * 1e3:.1f} ms / {stock_max:.3f} s -> targets "
          f"{SHORT * stock_min * 1e3:.1f} ms / {LONG * stock_max:.3f} s: "
          f"r_min {r_min:.0f}, r_span {r_max - r_min:.0f}")
    c = json.load(open(PATH))
    if a.check:
        old = c["meg"]
        bad = (abs(old["r_min"] / r_min - 1) > 1e-3 or
               abs(old["r_span"] / (r_max - r_min) - 1) > 1e-3)
        sys.exit(1 if bad else 0)
    c["meg"]["r_min"], c["meg"]["r_span"] = r_min, r_max - r_min
    with open(PATH, "w") as fh:
        json.dump(c, fh, indent=1)


if __name__ == "__main__":
    main()
