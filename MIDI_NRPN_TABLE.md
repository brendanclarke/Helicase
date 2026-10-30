# MIDI NRPN Table — Voice Parameters

Generated 2026-10-30 from the descriptor tables in `Core/DSP/Instruments/*/*Parameters.c` (branch `dev-ph7-voice_expansion`, S088 state). Rules are in `knowledge_files/specification_reference/MIDI_SPEC.md` §5.4–§5.6.

## How the numbers work

- **NRPN number = `slot × 64 + descriptor index`** (slot 0..5 = V1..V6). Send CC99 = number >> 7 (MSB), CC98 = number & 127 (LSB), then CC6 = value (0–127). CC38 is ignored; CC96/97 step the value ±1.
- **Instrument-agnostic:** an NRPN number addresses whatever descriptor sits at that index for the instrument type currently loaded in that slot. The same number means a different parameter on a different type (see the side-by-side columns below).
- **Channels:** every ID works on the global channel G. On a voice channel only that voice's own block (and its per-voice Scene targets) works; numbering stays absolute.
- **Values** are absolute 7-bit, clamped to the parameter's own range. A MIDI write is a menu edit: it writes the Normal endpoint and suppresses that parameter's automation until the voice's next trigger, a transport change or a Scene change (R12).
- **Any type can sit in any slot** (at most two Cymbal/HiHat per kit). Default kit: V1–V3 Drum, V4 Snare, V5 Cymbal, V6 HiHat. T6 and T7 both play V6.

Descriptor counts: Drum 42 (0–41), Snare 41 (0–40), Cymbal 42 (0–41), HiHat 42 (0–41). Indices above those, up to 63, are unused in every block: they are ignored and forwarded.

### Voice block ranges

| Voice | NRPN range | CC99 (MSB) | CC98 (LSB) for index *i* |
|---|---|---|---|
| V1 | 0–63 | 0 | *i* |
| V2 | 64–127 | 0 | 64 + *i* |
| V3 | 128–191 | 1 | *i* |
| V4 | 192–255 | 1 | 64 + *i* |
| V5 | 256–319 | 2 | *i* |
| V6 | 320–383 | 2 | 64 + *i* |

## Voice parameter table

Each NRPN cell is `number (MSB/LSB)`. Type cells are `file key` category, long label (`menu label`).

| Idx | V1 | V2 | V3 | V4 | V5 | V6 | Drum | Snare | Cymbal | HiHat |
|---:|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 (0/0) | 64 (0/64) | 128 (1/0) | 192 (1/64) | 256 (2/0) | 320 (2/64) | `osc1_wave` Oscilltr Waveform (`wav`) | `osc1_wave` Oscilltr Waveform (`wav`) | `osc1_wave` Oscilltr Waveform (`wav`) | `osc1_wave` Oscilltr Waveform (`wav`) |
| 1 | 1 (0/1) | 65 (0/65) | 129 (1/1) | 193 (1/65) | 257 (2/1) | 321 (2/65) | `osc1_pitch_coarse` Oscilltr Coarse (`coa`) | `osc1_pitch_coarse` Oscilltr Coarse (`coa`) | `osc1_pitch_coarse` Oscilltr Coarse (`coa`) | `osc1_pitch_coarse` Oscilltr Coarse (`coa`) |
| 2 | 2 (0/2) | 66 (0/66) | 130 (1/2) | 194 (1/66) | 258 (2/2) | 322 (2/66) | `osc1_pitch_fine` Oscilltr Fine (`fin`) | `osc1_pitch_fine` Oscilltr Fine (`fin`) | `osc1_pitch_fine` Oscilltr Fine (`fin`) | `osc1_pitch_fine` Oscilltr Fine (`fin`) |
| 3 | 3 (0/3) | 67 (0/67) | 131 (1/3) | 195 (1/67) | 259 (2/3) | 323 (2/67) | `osc2_wave` FM Waveform (`wav`) | `noise_freq` Noise Frequncy (`noi`) | `osc2_wave` Mod Osc Waveform (`wav`) | `osc2_wave` Mod Osc Waveform (`wav`) |
| 4 | 4 (0/4) | 68 (0/68) | 132 (1/4) | 196 (1/68) | 260 (2/4) | 324 (2/68) | `osc2_pitch_coarse` FM Frequncy (`frq`) | `osc1_noise_mix` Nois/Osc Mix (`mix`) | `osc2_pitch_coarse` Mod Osc Freqcy 1 (`f1`) | `osc2_pitch_coarse` Mod Osc Freqcy 1 (`f1`) |
| 5 | 5 (0/5) | 69 (0/69) | 133 (1/5) | 197 (1/69) | 261 (2/5) | 325 (2/69) | `osc2_mod_amount` FM Amount (`amt`) | `filter_freq` Filter Frequncy (`frq`) | `osc2_mod_amount` Mod Osc Gain 1 (`g1`) | `osc2_mod_amount` Mod Osc Gain 1 (`g1`) |
| 6 | 6 (0/6) | 70 (0/70) | 134 (1/6) | 198 (1/70) | 262 (2/6) | 326 (2/70) | `osc2_mod_type` FM Mode (`mod`) | `filter_reso` Filter Resnance (`res`) | `osc3_wave` Mod Osc Waveform (`wav`) | `osc3_wave` Mod Osc Waveform (`wav`) |
| 7 | 7 (0/7) | 71 (0/71) | 135 (1/7) | 199 (1/71) | 263 (2/7) | 327 (2/71) | `filter_freq` Filter Frequncy (`frq`) | `filter_drive` Filter Overdriv (`drv`) | `osc3_pitch_coarse` Mod Osc Freqcy 2 (`f2`) | `osc3_pitch_coarse` Mod Osc Freqcy 2 (`f2`) |
| 8 | 8 (0/8) | 72 (0/72) | 136 (1/8) | 200 (1/72) | 264 (2/8) | 328 (2/72) | `filter_reso` Filter Resnance (`res`) | `filter_type` Filter Type (`typ`) | `osc3_mod_amount` Mod Osc Gain 2 (`g2`) | `osc3_mod_amount` Mod Osc Gain 2 (`g2`) |
| 9 | 9 (0/9) | 73 (0/73) | 137 (1/9) | 201 (1/73) | 265 (2/9) | 329 (2/73) | `filter_drive` Filter Overdriv (`drv`) | `amp_envelope_attack` Veloc EG Attack (`atk`) | `filter_freq` Filter Frequncy (`frq`) | `filter_freq` Filter Frequncy (`frq`) |
| 10 | 10 (0/10) | 74 (0/74) | 138 (1/10) | 202 (1/74) | 266 (2/10) | 330 (2/74) | `filter_type` Filter Type (`typ`) | `amp_envelope_decay` Veloc EG Decay (`dec`) | `filter_reso` Filter Resnance (`res`) | `filter_reso` Filter Resnance (`res`) |
| 11 | 11 (0/11) | 75 (0/75) | 139 (1/11) | 203 (1/75) | 267 (2/11) | 331 (2/75) | `amp_envelope_attack` Veloc EG Attack (`atk`) | `amp_envelope_slope` Veloc EG Slope (`slp`) | `filter_drive` Filter Overdriv (`drv`) | `filter_drive` Filter Overdriv (`drv`) |
| 12 | 12 (0/12) | 76 (0/76) | 140 (1/12) | 204 (1/76) | 268 (2/12) | 332 (2/76) | `amp_envelope_decay` Veloc EG Decay (`dec`) | `amp_attack_repeat` Veloc EG RepeatCt (`rpt`) | `filter_type` Filter Type (`typ`) | `filter_type` Filter Type (`typ`) |
| 13 | 13 (0/13) | 77 (0/77) | 141 (1/13) | 205 (1/77) | 269 (2/13) | 333 (2/77) | `amp_envelope_slope` Veloc EG Slope (`slp`) | `pitch_envelope_decay` PitchMod Decay (`dec`) | `amp_envelope_attack` Veloc EG Attack (`atk`) | `amp_envelope_attack` Veloc EG Attack (`atk`) |
| 14 | 14 (0/14) | 78 (0/78) | 142 (1/14) | 206 (1/78) | 270 (2/14) | 334 (2/78) | `pitch_envelope_decay` PitchMod Decay (`dec`) | `pitch_envelope_amount` PitchMod Amount (`amt`) | `amp_envelope_decay` Veloc EG Decay (`dec`) | `amp_envelope_decay` Veloc EG Dcy Clsd (`d1`) |
| 15 | 15 (0/15) | 79 (0/79) | 143 (1/15) | 207 (1/79) | 271 (2/15) | 335 (2/79) | `pitch_envelope_amount` PitchMod Amount (`amt`) | `pitch_envelope_slope` PitchMod Slope (`slp`) | `amp_envelope_slope` Veloc EG Slope (`slp`) | `amp_envelope_decay_choke` Veloc EG Dcy Open (`d2`) |
| 16 | 16 (0/16) | 80 (0/80) | 144 (1/16) | 208 (1/80) | 272 (2/16) | 336 (2/80) | `pitch_envelope_slope` PitchMod Slope (`slp`) | `instrument_vol` Voice Volume (`vol`) | `amp_attack_repeat` Veloc EG RepeatCt (`rpt`) | `amp_envelope_slope` Veloc EG Slope (`slp`) |
| 17 | 17 (0/17) | 81 (0/81) | 145 (1/17) | 209 (1/81) | 273 (2/17) | 337 (2/81) | `instrument_vol` Voice Volume (`vol`) | `instrument_pan` Voice Panning (`pan`) | `instrument_vol` Voice Volume (`vol`) | `instrument_vol` Voice Volume (`vol`) |
| 18 | 18 (0/18) | 82 (0/82) | 146 (1/18) | 210 (1/82) | 274 (2/18) | 338 (2/82) | `instrument_pan` Voice Panning (`pan`) | `instrument_drive` Voice Overdriv (`drv`) | `instrument_pan` Voice Panning (`pan`) | `instrument_pan` Voice Panning (`pan`) |
| 19 | 19 (0/19) | 83 (0/83) | 147 (1/19) | 211 (1/83) | 275 (2/19) | 339 (2/83) | `instrument_drive` Voice Overdriv (`drv`) | `instrument_decimation` Voice SampleRt (`srt`) | `instrument_drive` Voice Overdriv (`drv`) | `instrument_drive` Voice Overdriv (`drv`) |
| 20 | 20 (0/20) | 84 (0/84) | 148 (1/20) | 212 (1/84) | 276 (2/20) | 340 (2/84) | `instrument_decimation` Voice SampleRt (`srt`) | `lfo_rate` LFO Frequncy (`frq`) | `instrument_decimation` Voice SampleRt (`srt`) | `instrument_decimation` Voice SampleRt (`srt`) |
| 21 | 21 (0/21) | 85 (0/85) | 149 (1/21) | 213 (1/85) | 277 (2/21) | 341 (2/85) | `lfo_rate` LFO Frequncy (`frq`) | `lfo_amount` LFO Amount (`am1`) | `lfo_rate` LFO Frequncy (`frq`) | `lfo_rate` LFO Frequncy (`frq`) |
| 22 | 22 (0/22) | 86 (0/86) | 150 (1/22) | 214 (1/86) | 278 (2/22) | 342 (2/86) | `lfo_amount` LFO Amount (`am1`) | `lfo_amount_2` LFO Amount 2 (`am2`) | `lfo_amount` LFO Amount (`am1`) | `lfo_amount` LFO Amount (`am1`) |
| 23 | 23 (0/23) | 87 (0/87) | 151 (1/23) | 215 (1/87) | 279 (2/23) | 343 (2/87) | `lfo_amount_2` LFO Amount 2 (`am2`) | `lfo_wave` LFO Waveform (`wav`) | `lfo_amount_2` LFO Amount 2 (`am2`) | `lfo_amount_2` LFO Amount 2 (`am2`) |
| 24 | 24 (0/24) | 88 (0/88) | 152 (1/24) | 216 (1/88) | 280 (2/24) | 344 (2/88) | `lfo_wave` LFO Waveform (`wav`) | `lfo_retrigger_voice` LFO Retriggr (`rtg`) | `lfo_wave` LFO Waveform (`wav`) | `lfo_wave` LFO Waveform (`wav`) |
| 25 | 25 (0/25) | 89 (0/89) | 153 (1/25) | 217 (1/89) | 281 (2/25) | 345 (2/89) | `lfo_retrigger_voice` LFO Retriggr (`rtg`) | `lfo_polarity` LFO Polarity (`pol`) | `lfo_retrigger_voice` LFO Retriggr (`rtg`) | `lfo_retrigger_voice` LFO Retriggr (`rtg`) |
| 26 | 26 (0/26) | 90 (0/90) | 154 (1/26) | 218 (1/90) | 282 (2/26) | 346 (2/90) | `lfo_polarity` LFO Polarity (`pol`) | `lfo_sync` LFO ClockSnc (`snc`) | `lfo_polarity` LFO Polarity (`pol`) | `lfo_polarity` LFO Polarity (`pol`) |
| 27 | 27 (0/27) | 91 (0/91) | 155 (1/27) | 219 (1/91) | 283 (2/27) | 347 (2/91) | `lfo_sync` LFO ClockSnc (`snc`) | `lfo_offset` LFO Offset (`ofs`) | `lfo_sync` LFO ClockSnc (`snc`) | `lfo_sync` LFO ClockSnc (`snc`) |
| 28 | 28 (0/28) | 92 (0/92) | 156 (1/28) | 220 (1/92) | 284 (2/28) | 348 (2/92) | `lfo_offset` LFO Offset (`ofs`) | `velo_vol_on_off` Velocity Vol mod (`vel`) | `lfo_offset` LFO Offset (`ofs`) | `lfo_offset` LFO Offset (`ofs`) |
| 29 | 29 (0/29) | 93 (0/93) | 157 (1/29) | 221 (1/93) | 285 (2/29) | 349 (2/93) | `velo_vol_on_off` Velocity Vol mod (`vel`) | `velo_mod_amount` Velocity Amount (`amt`) | `velo_vol_on_off` Velocity Vol mod (`vel`) | `velo_vol_on_off` Velocity Vol mod (`vel`) |
| 30 | 30 (0/30) | 94 (0/94) | 158 (1/30) | 222 (1/94) | 286 (2/30) | 350 (2/94) | `velo_mod_amount` Velocity Amount (`amt`) | `velo_mod_dest` Velocity DstParam (`dst`) | `velo_mod_amount` Velocity Amount (`amt`) | `velo_mod_amount` Velocity Amount (`amt`) |
| 31 | 31 (0/31) | 95 (0/95) | 159 (1/31) | 223 (1/95) | 287 (2/31) | 351 (2/95) | `velo_mod_dest` Velocity DstParam (`dst`) | `lfo_target_voice` LFO DstVoice (`vo1`) | `velo_mod_dest` Velocity DstParam (`dst`) | `velo_mod_dest` Velocity DstParam (`dst`) |
| 32 | 32 (0/32) | 96 (0/96) | 160 (1/32) | 224 (1/96) | 288 (2/32) | 352 (2/96) | `lfo_target_voice` LFO DstVoice (`vo1`) | `lfo_target_param` LFO DstParam (`ds1`) | `lfo_target_voice` LFO DstVoice (`vo1`) | `lfo_target_voice` LFO DstVoice (`vo1`) |
| 33 | 33 (0/33) | 97 (0/97) | 161 (1/33) | 225 (1/97) | 289 (2/33) | 353 (2/97) | `lfo_target_param` LFO DstParam (`ds1`) | `lfo_target_voice_2` LFO DstVoice2 (`vo2`) | `lfo_target_param` LFO DstParam (`ds1`) | `lfo_target_param` LFO DstParam (`ds1`) |
| 34 | 34 (0/34) | 98 (0/98) | 162 (1/34) | 226 (1/98) | 290 (2/34) | 354 (2/98) | `lfo_target_voice_2` LFO DstVoice2 (`vo2`) | `lfo_target_param_2` LFO DstParam2 (`ds2`) | `lfo_target_voice_2` LFO DstVoice2 (`vo2`) | `lfo_target_voice_2` LFO DstVoice2 (`vo2`) |
| 35 | 35 (0/35) | 99 (0/99) | 163 (1/35) | 227 (1/99) | 291 (2/35) | 355 (2/99) | `lfo_target_param_2` LFO DstParam2 (`ds2`) | `transient_wave` Transnt Waveform (`wav`) | `lfo_target_param_2` LFO DstParam2 (`ds2`) | `lfo_target_param_2` LFO DstParam2 (`ds2`) |
| 36 | 36 (0/36) | 100 (0/100) | 164 (1/36) | 228 (1/100) | 292 (2/36) | 356 (2/100) | `transient_wave` Transnt Waveform (`wav`) | `transient_vol` Transnt Volume (`vol`) | `transient_wave` Transnt Waveform (`wav`) | `transient_wave` Transnt Waveform (`wav`) |
| 37 | 37 (0/37) | 101 (0/101) | 165 (1/37) | 229 (1/101) | 293 (2/37) | 357 (2/101) | `transient_vol` Transnt Volume (`vol`) | `transient_freq` Transnt Frequncy (`frq`) | `transient_vol` Transnt Volume (`vol`) | `transient_vol` Transnt Volume (`vol`) |
| 38 | 38 (0/38) | 102 (0/102) | 166 (1/38) | 230 (1/102) | 294 (2/38) | 358 (2/102) | `transient_freq` Transnt Frequncy (`frq`) | `osc1_position` Oscilltr Position (`pos`) | `transient_freq` Transnt Frequncy (`frq`) | `transient_freq` Transnt Frequncy (`frq`) |
| 39 | 39 (0/39) | 103 (0/103) | 167 (1/39) | 231 (1/103) | 295 (2/39) | 359 (2/103) | `osc1_position` Oscilltr Position (`pos`) | `osc1_type` Oscilltr Type (`typ`) | `osc1_position` Oscilltr Position (`pos`) | `osc1_position` Oscilltr Position (`pos`) |
| 40 | 40 (0/40) | 104 (0/104) | 168 (1/40) | 232 (1/104) | 296 (2/40) | 360 (2/104) | `osc1_type` Oscilltr Type (`typ`) | `osc1_run` Oscilltr Run Mode (`run`) | `osc1_type` Oscilltr Type (`typ`) | `osc1_type` Oscilltr Type (`typ`) |
| 41 | 41 (0/41) | 105 (0/105) | 169 (1/41) | 233 (1/105) | 297 (2/41) | 361 (2/105) | `osc1_run` Oscilltr Run Mode (`run`) | — | `osc1_run` Oscilltr Run Mode (`run`) | `osc1_run` Oscilltr Run Mode (`run`) |
| 42–63 | 42–63 | 106–127 | 170–191 | 234–255 | 298–319 | 362–383 | unused | unused | unused | unused |

### Where the four types diverge

Indices 0–2 (osc1 wave, coarse, fine) are the same on all four types. From `instrument_vol` to the end the four types carry the same keys in the same order, but Snare's run sits one index lower: Drum, Cymbal and HiHat 17–41, Snare 16–40. Between those, the blocks move:

| Block | Drum | Snare | Cymbal | HiHat |
|---|---|---|---|---|
| Osc 2 / noise | 3–6 FM osc (`osc2_wave`, `osc2_pitch_coarse`, `osc2_mod_amount`, `osc2_mod_type`) | 3–4 noise (`noise_freq`, `osc1_noise_mix`) | 3–8 two mod oscs (`osc2_*`, `osc3_*`) | 3–8 two mod oscs (`osc2_*`, `osc3_*`) |
| Filter (freq, reso, drive, type) | 7–10 | 5–8 | 9–12 | 9–12 |
| Amp envelope | 11–13 atk, dec, slp | 9–12 atk, dec, slp, `amp_attack_repeat` | 13–16 atk, dec, slp, `amp_attack_repeat` | 13–16 atk, closed dec, open dec (`amp_envelope_decay_choke`), slp |
| Pitch envelope (dec, amt, slp) | 14–16 | 13–15 | — | — |

### Value notes

Rows not listed here are continuous 0–127 (identity).

| Key | Values |
|---|---|
| `osc1_wave` | 0–7 `Sin Tri Saw Rec Noi Cym Re3 Sw3`; 8+ = installed user samples (clamps to the last) |
| `osc2_wave` | as `osc1_wave` |
| `osc3_wave` | as `osc1_wave` |
| `osc2_mod_type` | 0 `fm`, 1 `mix`, 2 `snc`, 3–127 `rng` (Drum only) |
| `filter_type` | 0 `LP`, 1 `HP`, 2 `BP`, 3 `UBP`, 4 `Nch`, 5 `Pek`, 6 `LP2`, 7 `off` |
| `lfo_wave` | 0 `sin`, 1 `tri`, 2 `sup`, 3 `sdn`, 4 `sqr`, 5 `rnd`, 6 `xup`, 7 `xdn` |
| `lfo_retrigger_voice` | 0 `off`, 1–6 `v1`–`v6`, 7 `scn` |
| `lfo_polarity` | 0 `neg`, 1 `pos`, 2 `bi` |
| `lfo_sync` | 0 `off`, 1 `4/1`, 2 `2/1`, 3 `1/1`, 4 `1/2`, 5 `1/3`, 6 `1/4`, 7 `1/6`, 8 `1/8`, 9 `12`, 10 `16`, 11 `32` |
| `velo_vol_on_off` | 0 off, 1 on |
| `transient_wave` | 0–13 `Snp Ofs Clk Ck2 Tik Kik Rim Drp Hat Clp Kk2 Snr Tom Sp2` |
| `osc1_type` | 0 `wav`, 1 `smp`, 2 `slp`, 3 `wtb` (selector, never morphed) |
| `osc1_run` | 0 `rst`, 1 `fre`, 2 `itp`; 3–127 clamp to `itp` (selector) |
| `instrument_pan` | 0–127, shown −63..+64, 63 = centre |
| `osc1_pitch_fine` | 0–127, shown −63..+64 |
| `velo_mod_dest` | modulation target token (clamped to the token list) |
| `lfo_target_param` | modulation target token (clamped to the token list) |
| `lfo_target_param_2` | modulation target token (clamped to the token list) |
| `lfo_target_voice` | LFO destination voice selector |
| `lfo_target_voice_2` | LFO destination voice selector |

## Scene targets (384–425)

Per-voice Scene targets also work on that voice's own channel; `fxm` works on G only.

| NRPN | MSB/LSB | Target | Meaning |
|---:|---|---|---|
| 384 | 3/0 | `1vm` | V1 Morph amount (0–126 → ×2, 127 → 255) |
| 385 | 3/1 | `2vm` | V2 Morph amount (0–126 → ×2, 127 → 255) |
| 386 | 3/2 | `3vm` | V3 Morph amount (0–126 → ×2, 127 → 255) |
| 387 | 3/3 | `4vm` | V4 Morph amount (0–126 → ×2, 127 → 255) |
| 388 | 3/4 | `5vm` | V5 Morph amount (0–126 → ×2, 127 → 255) |
| 389 | 3/5 | `6vm` | V6 Morph amount (0–126 → ×2, 127 → 255) |
| 390 | 3/6 | `—` | unused (retired) |
| 391 | 3/7 | `7dc` | T7 (open hat) amp decay, slot 6 |
| 392 | 3/8 | `1ou` | V1 audio output |
| 393 | 3/9 | `2ou` | V2 audio output |
| 394 | 3/10 | `3ou` | V3 audio output |
| 395 | 3/11 | `4ou` | V4 audio output |
| 396 | 3/12 | `5ou` | V5 audio output |
| 397 | 3/13 | `6ou` | V6 audio output |
| 398 | 3/14 | `1fx` | V1 FX send |
| 399 | 3/15 | `2fx` | V2 FX send |
| 400 | 3/16 | `3fx` | V3 FX send |
| 401 | 3/17 | `4fx` | V4 FX send |
| 402 | 3/18 | `5fx` | V5 FX send |
| 403 | 3/19 | `6fx` | V6 FX send |
| 404 | 3/20 | `fxm` | Effect Morph (G only) |
| 405 | 3/21 | `1ln` | T1 track length (1–128) |
| 406 | 3/22 | `2ln` | T2 track length (1–128) |
| 407 | 3/23 | `3ln` | T3 track length (1–128) |
| 408 | 3/24 | `4ln` | T4 track length (1–128) |
| 409 | 3/25 | `5ln` | T5 track length (1–128) |
| 410 | 3/26 | `6ln` | T6 track length (1–128) |
| 411 | 3/27 | `7ln` | T7 track length (1–128) |
| 412 | 3/28 | `1sc` | T1 track scale |
| 413 | 3/29 | `2sc` | T2 track scale |
| 414 | 3/30 | `3sc` | T3 track scale |
| 415 | 3/31 | `4sc` | T4 track scale |
| 416 | 3/32 | `5sc` | T5 track scale |
| 417 | 3/33 | `6sc` | T6 track scale |
| 418 | 3/34 | `7sc` | T7 track scale |
| 419 | 3/35 | `1sh` | T1 shuffle |
| 420 | 3/36 | `2sh` | T2 shuffle |
| 421 | 3/37 | `3sh` | T3 shuffle |
| 422 | 3/38 | `4sh` | T4 shuffle |
| 423 | 3/39 | `5sh` | T5 shuffle |
| 424 | 3/40 | `6sh` | T6 shuffle |
| 425 | 3/41 | `7sh` | T7 shuffle |
| 426–447 | | | unused |

Effect parameters are 448–510 (Effect-local index 0–62, G only); they depend on the current Effect type and are not charted here. 511 = off.

## MIDI Learn CC map (`SD_CARD/midilrn.cfg`)

A ready-made Learn table: each voice on its own channel (V1 = ch 2 … V6 = ch 7), the same 20 CC numbers on every channel. CCs with a General MIDI meaning use it (7 volume, 10 pan, 71 resonance, 72–75 envelope/cutoff, 76/77 LFO); the rest sit in the undefined block 20–30. 120 of the 128 Learn rows are used.

**Setup:**
1. Copy `SD_CARD/midilrn.cfg` to the card root (it replaces any Learn table already there) and boot.
2. MIDI page: `rxf` must include C (`all` for normal use); set `ch` (G) to a channel outside 2–7, e.g. 1 or 10. A channel equal to G is not a voice channel, and `v` rows never answer on G.
3. In **every Scene**, STEP track settings `ch`: T1 = 2, T2 = 3, T3 = 4, T4 = 5, T5 = 6, T6 = 7, T7 = 7 (T6 and T7 both play V6). The file stores the channel *kind* `v`, not a number, so the rows follow whatever channel each track has.
4. The rows are type-matched: a row works only while its slot holds the type below. Loading a different type into a slot silences that slot's rows until you Learn new ones (re-learning a parameter replaces its row).

With `txf` including C, panel edits of these parameters are sent out as these CCs on the voice channels instead of NRPN.

Caveat: many DAWs send CC7 and CC10 on a channel when a track is armed or a project opens, which sets voice volume and pan.

| CC | Function | Drum (V1–V3) | Snare (V4) | Cymbal (V5) | HiHat (V6) |
|---:|---|---|---|---|---|
| 7 | Volume | `instrument_vol` (17) | `instrument_vol` (16) | `instrument_vol` (17) | `instrument_vol` (17) |
| 10 | Pan | `instrument_pan` (18) | `instrument_pan` (17) | `instrument_pan` (18) | `instrument_pan` (18) |
| 20 | Osc pitch coarse | `osc1_pitch_coarse` (1) | `osc1_pitch_coarse` (1) | `osc1_pitch_coarse` (1) | `osc1_pitch_coarse` (1) |
| 21 | Osc waveform | `osc1_wave` (0) | `osc1_wave` (0) | `osc1_wave` (0) | `osc1_wave` (0) |
| 22 | Filter drive | `filter_drive` (9) | `filter_drive` (7) | `filter_drive` (11) | `filter_drive` (11) |
| 23 | Voice drive | `instrument_drive` (19) | `instrument_drive` (18) | `instrument_drive` (19) | `instrument_drive` (19) |
| 24 | Sample rate | `instrument_decimation` (20) | `instrument_decimation` (19) | `instrument_decimation` (20) | `instrument_decimation` (20) |
| 25 | Transient volume | `transient_vol` (37) | `transient_vol` (36) | `transient_vol` (37) | `transient_vol` (37) |
| 26 | Character 1 | `osc2_pitch_coarse` (4) | `noise_freq` (3) | `osc2_pitch_coarse` (4) | `osc2_pitch_coarse` (4) |
| 27 | Character 2 | `osc2_mod_amount` (5) | `osc1_noise_mix` (4) | `osc2_mod_amount` (5) | `osc2_mod_amount` (5) |
| 28 | Character 3 | `pitch_envelope_decay` (14) | `pitch_envelope_decay` (13) | `osc3_pitch_coarse` (7) | `osc3_pitch_coarse` (7) |
| 29 | Character 4 | `pitch_envelope_amount` (15) | `pitch_envelope_amount` (14) | `osc3_mod_amount` (8) | `osc3_mod_amount` (8) |
| 30 | Character 5 | `pitch_envelope_slope` (16) | `amp_attack_repeat` (12) | `amp_attack_repeat` (16) | `amp_envelope_decay_choke` (15) |
| 71 | Filter resonance | `filter_reso` (8) | `filter_reso` (6) | `filter_reso` (10) | `filter_reso` (10) |
| 72 | Amp slope | `amp_envelope_slope` (13) | `amp_envelope_slope` (11) | `amp_envelope_slope` (15) | `amp_envelope_slope` (16) |
| 73 | Amp attack | `amp_envelope_attack` (11) | `amp_envelope_attack` (9) | `amp_envelope_attack` (13) | `amp_envelope_attack` (13) |
| 74 | Filter cutoff | `filter_freq` (7) | `filter_freq` (5) | `filter_freq` (9) | `filter_freq` (9) |
| 75 | Amp decay | `amp_envelope_decay` (12) | `amp_envelope_decay` (10) | `amp_envelope_decay` (14) | `amp_envelope_decay` (14) |
| 76 | LFO rate | `lfo_rate` (21) | `lfo_rate` (20) | `lfo_rate` (21) | `lfo_rate` (21) |
| 77 | LFO amount | `lfo_amount` (22) | `lfo_amount` (21) | `lfo_amount` (22) | `lfo_amount` (22) |

Numbers in brackets are descriptor indices; the NRPN for the same parameter is `slot × 64 + index`.
