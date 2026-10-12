#pragma once

#include <cmath>

namespace openguitarmultifx::pots
{

/**
    How a real potentiometer's resistance follows its knob.

    Every log pot in this project used to be approximated as `knob^2`, which was documented as a stand-in in each
    processor. It is a poor one: `knob^2` passes **25%** of the track at half rotation, while the audio-taper ("A") pots
    actually fitted to guitar pedals are **15% taper** -- at 50% rotation the divider ratio is 15%. So at noon every Drive,
    Gain and Volume knob in the project was passing about 1.7x (+4.4 dB) more than the real part, which is a large part of
    why the models sat in saturation whatever the player did (see docs/circuits/GainStaging.md).

    The law here is Ben Holmes' curve fit for logarithmic potentiometer laws (benholmes.co.uk, 2017), which is the
    published modelling technique for exactly this:

        y = a (b^x - 1),   b = (1/ym - 1)^2,   a = 1 / (b - 1)

    `ym` is the fraction at half rotation, so the curve is pinned to the one number a pot is actually specified by:
    y(0) = 0, y(0.5) = ym, y(1) = 1. ym = 0.15 is an audio/log ("A") pot, 0.85 an anti-log/reverse ("C") pot, 0.5 linear.

    **What this is not:** a real A pot is not exponential at all -- it is built from two resistive track widths, so its law
    is two straight segments meeting near the middle, and it has a kink there that this smooth curve does not. The two
    agree on the midpoint and the ends; between them a real part varies by manufacturer anyway. The smooth fit is the
    accepted model and avoids inventing segment breakpoints no datasheet gives.
*/
inline double law (double knob, double midpointFraction) noexcept
{
    const double x = knob < 0.0 ? 0.0 : (knob > 1.0 ? 1.0 : knob);
    if (std::abs (midpointFraction - 0.5) < 1.0e-9) // linear ("B"): the exponential form degenerates to 0/0 there
        return x;
    const double r = 1.0 / midpointFraction - 1.0;
    const double b = r * r;
    return (std::pow (b, x) - 1.0) / (b - 1.0);
}

/** Audio / logarithmic ("A") taper: 15% of the track at half rotation, the value real pedal pots are made to. */
inline double audio (double knob) noexcept { return law (knob, 0.15); }

/** Anti-log / reverse-audio ("C") taper: 85% at half rotation -- the mirror of audio(). */
inline double reverseAudio (double knob) noexcept { return 1.0 - audio (1.0 - knob); }

/** Inverse of audio(): the knob rotation that puts the wiper at a given fraction of the track.
    Used for parameter defaults -- "this knob should sit at half gain by default" means the
    rotation audioInverse(0.5) (~0.81), not 0.5, on a real audio-taper pot. */
inline double audioInverse (double wiper) noexcept
{
    const double y = wiper < 0.0 ? 0.0 : (wiper > 1.0 ? 1.0 : wiper);
    const double r = 1.0 / 0.15 - 1.0;
    const double b = r * r;
    return std::log (1.0 + y * (b - 1.0)) / std::log (b);
}

} // namespace openguitarmultifx::pots
