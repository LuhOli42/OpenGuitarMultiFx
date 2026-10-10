# GEB7StyleEqualizer — Boss GEB-7 Bass Equalizer

## What the real unit is

The Boss GEB-7 is the bass variant of the GE-7 graphic equalizer: seven bands at
**50 Hz, 120 Hz, 400 Hz, 500 Hz, 800 Hz, 4.5 kHz and 10 kHz** plus a master Level slider,
each ±15 dB nominal (band centres from the GEB-7 owner's manual). Electrically it is the
same generation of Boss graphic EQ as the GE-7 — a non-inverting input stage, a Level
stage, an op-amp whose two inputs are joined through each band's slider to a series-LC
branch to the bias rail, and a buffered output. The LC branches are realised with op-amp
**gyrators** (a capacitor-multiplied inductor), exactly as documented for the GE-7 in
[GE7StyleEqualizer.md](GE7StyleEqualizer.md).

## What is modelled

The whole signal path of the GE-7 model, retuned to the GEB-7's band centres:

- **Input stage** — the same non-inverting stage with the 2.3 kHz pre-emphasis zero
  (470 Ω + 15 nF in the feedback leg).
- **Level stage** — the same slider arrangement (+15 dB boost / −15 dB cut around unity).
- **Equaliser** — one op-amp, 3.3K series/feedback resistors, 10K linear sliders; each of
  the six lower bands is a gyrator branch (modelled as the equivalent inductor
  `L = C_gyr R_gyr R_loss` in series with `R_loss`, in parallel with `R_gyr`), and the top
  band is a plain series C+R branch — the same split as the GE-7.
- **Output** — coupling cap, the effect-mode JFET switch (~200 Ω), the de-emphasis
  network that cancels the input pre-emphasis, the emitter follower and the 100K
  output pulldown.

## Band components

The real GEB-7 BOM is not published, so the branches are tuned to the published centres
on the same topology (±0.5%):

| Band | C_series | C_gyr | R_gyr | R_loss |
|------|----------|-------|-------|--------|
| 50 Hz | 3.3 µF | 0.1 µF | 100K | 300 |
| 120 Hz | 0.68 µF | 0.082 µF | 100K | 315 |
| 400 Hz | 0.33 µF | 0.015 µF | 100K | 330 |
| 500 Hz | 0.22 µF | 0.015 µF | 100K | 310 |
| 800 Hz | 0.15 µF | 0.0082 µF | 100K | 330 |
| 4.5 kHz | 0.022 µF | 0.0022 µF | 82K | 315 |
| 10 kHz | 0.027 µF + 820 Ω series RC (no gyrator; a shelf that keeps rising, like the GE-7's top band) |

## Deliberate simplifications

- Component values are tuned to the *documented band frequencies*; the GEB-7's exact BOM
  is unpublished (the frequencies themselves are spec).
- The input/output stages keep the GE-7's network (same-family hardware); if the GEB-7's
  own values differ, the audible result is identical because the pre/de-emphasis pair
  cancels below the top band either way.
- Slider taper taken as linear, same assumption as the GE-7 model.
