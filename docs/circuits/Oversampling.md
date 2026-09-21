# Oversampling the clipping pedals

`Source/Effects/OversampledEffect.h`, applied in `Source/EffectRegistry.cpp`.

## Why
A diode or transistor clipper makes harmonics far above 24 kHz. A real circuit
has no Nyquist frequency, so they simply die out; in a sampled model they fold
back into the audible band as **inharmonic** components (an input at 1.6 kHz
gives energy at frequencies that are not multiples of 1.6 kHz). That is the
harsh, "digital" top end and the extra fizz/hiss of an unoversampled distortion.
The fix is to run the circuit at 2x or 4x the host rate between a
half-band up-sampler and down-sampler (`juce::dsp::Oversampling`, polyphase-IIR
half-band, maximum quality).

## How it is built
A decorator: `OversampledEffect(inner, order)` (order 1 = 2x, 2 = 4x). The inner
processor is simply prepared at `fs * factor` and sees blocks `factor` times
longer, so no processor knows it is oversampled. Parameters, state, name, icon and
colour forward to the inner processor. `prepare()` with the same arguments is a
no-op (the engine re-prepares every processor whenever any block is dragged,
mid-signal; rebuilding the oversampler would tick). Latency: the IIR half-band is
minimum-phase, a few samples at the base rate, and the engine has no latency
compensation today.

## Measurements
Method: an input sine exactly on FFT bin 535 of 16384, 4 s of warmup, bins below 40
excluded; energy at bins that are **not** harmonics of the input, over the energy at
the harmonics (n = 1..15). Lower is cleaner. (`Tests/OversampledEffectTests.cpp`
keeps a version of this on a hard clipper; the pedal table was measured with a
temporary probe.)

| Pedal | 1x | 2x | 4x | CPU at 1x / 2x / 4x (% of a core, stereo dual-mono input) | Registry |
|---|---|---|---|---|---|
| DS-1 | -17.8 dB | -28.0 | -54.7 | 11 / 21 / 41 | **4x** |
| OD-1 | -37.9 | -55.2 | -75.3 | 5 / 11 / 21 | 2x |
| TS808 (TS9/TS10 same) | -41.2 | -62.4 | -94.0 | 6 / 13 / 25 | 2x |
| BD-2 | -36.6 | -51.6 | -63.5 | 17 / 29 / 57 | 2x |
| HM-2 | -45.1 | -62.7 | -71.7 | 16 / 28 / 53 | 2x |
| Centaur | -81.0 | -88.4 | -94.5 | 5 / 9 / 19 | none |
| Booster | < -100 | | | 3 | none |
| Overdrive (old) | < -130 | | | ~0 | none |

Two things these numbers say beyond the choice of factors:

- The **BD-2's 1x figure used to be +36 dB** (more junk than signal). That was not
  aliasing, it was the solver failing to converge; see `NodalCircuitSolver.md`.
  Aliasing and solver noise look alike in a spectrum, which is why the
  *non-periodic* metric (output minus itself one input period later) exists: an
  alias is periodic and cancels, noise does not. After the solver fixes every pedal
  is at **-150 dB or better** on that metric at every rate.
- The DS-1 is the one that needs 4x: it is the hardest clipper (a heavily
  overdriven op-amp into hard diodes with a bright tone stage after it), and 2x still
  leaves it at -28 dB.

(The "Registry" column above is what the registry used before the quality tiers; the tiers below replace it.)

## Quality tiers -- chosen in the app: "..." -> Settings -> Rendering quality (Eco / Normal / High)
**Eco is the default** (the user's choice). The screen calls `EffectRegistry::setOversamplingQuality`,
saves the choice to `<app data>/OpenGuitarMultiFx/render_quality.txt` and rebuilds the distortion
pedals already in the chain in place (same position and grid cell, knob values and bypass kept;
their MIDI Learn bindings are cleared, as for any removed block). "Normal" is the `balanced`
column below. `OGMFX_QUALITY=eco|normal|high` overrides the saved value (development).
Chosen per pedal from the table above; CPU is % of one core on the dev PC (48 kHz, mono
guitar in a stereo buffer, noon knobs):

| Pedal | eco (default) | balanced = Normal | high |
|---|---|---|---|
| DS-1 | 1x 2.6% | 2x 4.6% | 4x 8.5% |
| BD-2 | 1x 6.9% | 2x 12.5% | 4x 24% |
| HM-2 | 1x 6.0% | 2x 9.9% | 4x 19% |
| TS808/9/10 | 1x 3.1% | 1x 3.0% | 2x 6.5% |
| OD-1 | 1x 2.6% | 1x 2.5% | 2x 5.4% |
| Klon | 1.9% (never oversampled) | | |

Alias energy that survives below 5 kHz (a 1.6 kHz tone, worst case; dB re the harmonics):
DS-1 -26 / -37 / -55 (1x / 2x / 4x), BD-2 -39 / -53 / -63, HM-2 -33 / -48 / -62,
TS808 -42 / -63 / -95, OD-1 -49 / -70 / -88.

## Where the cost is, and what is left
Since the state-space core a linear block costs ~150 cycles and a nonlinear one 1000-2100
(Newton dominated, per-block table in NodalCircuitSolver.md); 5% of a core at 2x is ~1500
cycles per oversampled sample for the WHOLE pedal, about one nonlinear block, which is why
the BD-2 (two big ones) and HM-2 (three) do not fit at 2x. Ideas not done: merge the linear blocks that only
sit behind an ideal source (BD-2 A/E, HM-2's Color section) into their neighbours or run
them at the base rate around the oversampled core; replace the BD-2's two discrete gain
stages (each ~5 ports) by an op-amp macro-model with finite gain and a dominant pole --
needs the open-loop response fitted and the asymmetric class-A saturation checked.
Do not "fix" CPU by removing oversampling from a pedal without checking the table.
