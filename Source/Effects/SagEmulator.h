#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx
{

/**
    A behavioural power-supply sag for amplifiers that are not circuit-modelled (the neural captures). A real tube
    amp's rectifier and filter capacitors droop while the output stage draws current, so a loud passage squashes
    itself for a few tens of milliseconds and recovers afterwards, with a slow, lightly damped ring (the choke against
    the capacitors, ~13 Hz on the Bassman). This is the same phenomenon the Bassman Amplifier gets from its
    real supply model, reduced to what an audio-only model can honour: the output level is the "current draw", a
    2-pole low-pass at 13 Hz (Q 0.9) is the supply's response, and the gain follows the droop.

    It is an approximation: a neural capture cannot say how ITS amp's supply behaved, so the amount is a taste control,
    not a measurement. `amount` 0 leaves the signal bit-exact.
*/
class SagEmulator
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        attack = 1.0 - std::exp (-1.0 / (0.006 * sampleRate));   // the draw rises in ~6 ms
        release = 1.0 - std::exp (-1.0 / (0.080 * sampleRate));  // and falls in ~80 ms (the filter capacitors)
        const double w0 = 2.0 * 3.14159265358979323846 * 13.0 / sampleRate, q = 0.9;
        const double alpha = std::sin (w0) / (2.0 * q), cosw = std::cos (w0);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cosw) / 2.0 / a0;
        b1 = (1.0 - cosw) / a0;
        b2 = b0;
        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
        reset();
    }

    void reset() noexcept { draw = z1 = z2 = 0.0; }

    /** Scales `x` in place. `amount` in 0..1. */
    void process (float* x, int numSamples, float amount) noexcept
    {
        if (amount <= 0.0f)
        {
            // keep the supply's state following the signal so switching it on mid-note does not jump
            for (int i = 0; i < numSamples; ++i)
                step (std::abs ((double) x[i]));
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const double droop = step (std::abs ((double) x[i]));
            x[i] = (float) ((double) x[i] / (1.0 + depth * (double) amount * droop));
        }
    }

    /** Current droop (0 = none), for tests. */
    double droopNow() const noexcept { return std::max (0.0, z1); }

private:
    double step (double level) noexcept
    {
        draw += (level > draw ? attack : release) * (level - draw);
        const double out = b0 * draw + z1;
        z1 = b1 * draw - a1 * out + z2;
        z2 = b2 * draw - a2 * out;
        return std::max (0.0, out);
    }

    static constexpr double depth = 1.2;
    double sampleRate = 48000.0, attack = 0.0, release = 0.0;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double draw = 0.0, z1 = 0.0, z2 = 0.0;
};

} // namespace openguitarmultifx
