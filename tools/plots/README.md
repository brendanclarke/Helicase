# tools/plots — S084 P1 voice-expansion plots

Design and illustration plots for `S084_P1_VOICE_EXPANSIONS.md`. Nothing here
is part of the firmware build.

## Requirements

Python 3 with `numpy`, `matplotlib` and `pillow`. These are not project
requirements (`requirements.txt` stays stdlib-only), so use a throwaway venv:

```
python3 -m venv /tmp/lxrplots && /tmp/lxrplots/bin/pip install numpy matplotlib pillow
cd tools/plots && /tmp/lxrplots/bin/python plot_current_drive.py   # etc.
```

## Scripts and outputs

| Script | Output | What it shows |
|---|---|---|
| `plot_current_drive.py` | `current_drive_profiles.png` | Today's filter `drv` soft clip, the Voice/Mix `drv` distortion, the real ZDF LP/HP loops driven by a full-scale sine, and small-signal gain against the control value |
| `plot_stock_waves.py` | `stock_waves_303_analog.png`, `stock_wave_harmonics.csv` | The new stock waves `sq3 sw3 sqa tra swa`: reference trace, the level-0 table, how they band-limit at 1 kHz and 4 kHz, harmonic spectra; the CSV is the harmonic recipe (k = 1..511) the install-time generator would use |
| `plot_pwm.py` | `pwm_families.png` | The `pwm` type families `prc psw ptr pq3 pw3 swp` at four control values, as played at 220 Hz |
| `plot_wavetable_formats.py` | `wavetable_formats.png` (+ printed table; `--stock` prints the stock-wave layouts) | Wavetable storage formats played as the oscillator would play them: aliasing/images and brightness per note against flash size, on a folder of single cycles (default `SD_CARD/loops`) |
| `plot_waveshapers.py` | `waveshapers_static.png`, `waveshapers_stateful.png` | The proposed filter drive types (`dty`): memoryless shapers with transfer curves, and the stateful ones (zero-crossing, trackers) on a sine and a kick-like glide |
| `lxr_plotlib.py` | — | Shared code: Python ports of the firmware drive stages, band-limiting helpers, the reference-image digitisers, plot style |

## Reference images (`reference/`)

Supplied by the user on 2026-10-25:

- `303_square_saw.png` — TB-303 square (left) and saw (right) captures;
  digitised for `sq3` and `sw3`.
- `tra_dirty_triangle.png` — the dirty triangle wanted for `tra`.
- `waveshapers.png` — the waveshaper set (rectifying … lo square) the drive
  types follow.

The digitisers find the trace pixels per column, measure the period by
autocorrelation, average all full cycles, and keep the first 30–40
harmonics (what the screenshots resolve). The remaining harmonics up to 511
are extended with a fitted roll-off (`lxr_plotlib.extend_harmonics()`).

## Firmware formulas ported

| Python | Firmware |
|---|---|
| `filt_drive()` | `SVF_setDrive()` (`ResonantFilter.c`) |
| `filter_input_stage()`, `ZdfFilter` | `SVF_calcBlockZDF()` batched solver (`ResonantFilter.c`) |
| `dist_k()`, `dist_curve()` | `setDistortionShape()`, `distortion_curveSample16()` (`distortion.c/.h`) |
| `shaper_f2f()`, `fast_tan()` | `valueShaperF2F()`, `fastTan()`; filter cutoff/resonance writers in `InstrumentManager.c` |

If any of those firmware functions change, update the port here before
trusting the plots.
