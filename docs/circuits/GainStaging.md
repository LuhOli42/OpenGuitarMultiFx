# Gain staging: what a sample means, and why everything sounded saturated

## The convention
Every circuit model in this project reads an audio sample **as volts at the input jack** -- `setSource(srcIn, data[i])`,
with no scaling, in every pedal and in the amp. That is deliberate: the schematics these models come from are in volts, and
the diode knees, tube grid limits and op-amp rails are all volt quantities. A sample of 1.0 is one volt.

**A guitar is not one volt.** A pickup gives roughly 0.05 V (light picking, single coils) to 0.3 V (hard strum,
humbuckers), peaking maybe 0.5 V. So the level an interface hands the app decides how hard every model is driven, and an
interface whose own preamp lands the guitar near full scale drives them **3-10x harder than the real pedal or amp would
ever see**.

## What that sounds like, measured
The user reported (2026-09-21) that playing softer did not clean anything up, and that the pedals seemed to be always
saturating. `Tests/AmpDynamicsBench.cpp` (`AMP_DYNAMICS=1`) sweeps the input level and prints output level and harmonic
content. The Bassman with its Volume at noon:

| input (V) | THD at 0 dB input gain | THD at -12 dB | output at 0 dB |
|---|---|---|---|
| 0.02 | 1.4% | 0.0% | 0.204 |
| 0.05 | 7.3% | 0.0% | 0.418 |
| 0.10 | 26.3% | 2.6% | **0.451** |
| 0.20 | 41.5% | 7.4% | **0.454** |
| 0.35 | 50.6% | 22.8% | **0.451** |
| 0.50 | 54.7% | 31.9% | **0.449** |
| 1.00 | 54.8% | 45.5% | **0.458** |

At 0 dB the output is **pinned from 0.1 V upward**: twenty decibels of playing dynamics produce no change in level at all,
which is exactly "it does not clean up when I play softer". At -12 dB the gain tracks the input over the light range
(7.86 -> 8.28 dB from 0.02 to 0.1 V) and only then compresses -- an amp that answers the pick.

**The Bassman's input limiter is not the cause.** Measured with it on and off (`BM_NO_LIMIT=1`): identical up to 0.35 V,
within 1% above. It only acts above 0.4 V and was added for solver stability (`Bassman5F6A.md`).

## The control
`AudioEngine::setInputGainDb()`, in the IN tile's menu ("Input gain", -24 .. +6 dB in 3 dB steps), remembered across
launches in `inputgain.txt` (`Source/Engine/InputGainSettings.h`) for the same reason the tuning is: it describes the
interface plugged in, not a sound. The IN meter reads **after** it, so it shows what the chain is actually being fed.

## The pot taper (fixed 2026-09-21)
Every logarithmic pot used to be approximated as `knob^2`, documented as a stand-in in each processor. It is a poor one:
`knob^2` passes **25%** of the track at half rotation, while the audio-taper ("A") pots actually fitted to guitar pedals
are a **15% taper**. Every Drive, Gain and Volume knob was therefore passing ~1.7x (+4.4 dB) more than the real part at
noon -- the knob's whole useful range was squeezed into its bottom third, which is the "it is always saturated" feel as
much as the level is.

`Source/Effects/PotTaper.h` replaces it with Ben Holmes' published curve fit for logarithmic potentiometer laws
(benholmes.co.uk, 2017), pinned to the one number a pot is specified by:

    y = a (b^x - 1),   b = (1/ym - 1)^2,   a = 1 / (b - 1)

with `ym` = the fraction at half rotation: **0.15** for audio/log ("A"), 0.85 for anti-log ("C"), 0.5 linear.
`pots::audio()` / `pots::reverseAudio()`. Applied to every log pot in the project (Tube Screamers, OD-1, Centaur, BD-2,
RAT, Distortion+/DOD 250, Guv'nor, Blues Breaker, the Bassman's Volume/Bass/Power Drive); the pots the schematics mark
linear (Guv'nor and Blues Breaker Drive, the tone stacks, Treble/Middle/Presence) were already linear and are untouched.

**Honest limit:** a real A pot is not exponential at all. It is built from two resistive track widths, so its law is two
straight segments with a kink near the middle; the smooth fit agrees at the midpoint and the ends, and between them real
parts vary by manufacturer. No datasheet gives the breakpoints, so inventing them would be a worse kind of guess.

**Every unity trim was re-measured** after this (`UnityLevel.md`): the pedals lost 2.8-6.1 dB at noon, which is the size of
the error that was there before.
