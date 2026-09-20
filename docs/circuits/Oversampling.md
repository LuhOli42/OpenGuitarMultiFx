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

## Cost and where to revisit it
Oversampling multiplies the processor's cost by the factor. On the desktop that is
tolerable (one pedal at 4x is ~40% of one core); on the Phase 6 target it will not be.
The options then, in order: oversample only the blocks that contain the clipper
(the linear blocks before/after gain nothing), drop the factors by one, or make the
factor a per-device quality setting. Do not "fix" CPU by removing the oversampling
from a pedal without checking this table.
