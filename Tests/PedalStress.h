#pragma once

#include "Effects/EffectProcessor.h"

#include <cmath>

namespace openguitarmultifx
{

/** Random knob jumps (a quarter of them to an end stop), plucked notes of random pitch and level, and hot bursts (x6, a
    pedal's output driving the next one) through a stereo processor, in 128-sample blocks. Returns the number of blocks
    whose output was not finite. The caller then checks the processor's own solver-failure rate: a circuit that fails to
    converge freezes and burns 300 Newton iterations per sample, which is the "sound dies and the CPU spikes" failure
    (the Guv'nor, 2026-09-21: two coupled clamp diodes in the op-amp macro-model). */
inline int runPedalStress (EffectProcessor& fx, double seconds, int seed, double sampleRate = 48000.0)
{
    juce::Random rng (seed);
    auto params = fx.getParameters()->getParameters (true);
    juce::AudioBuffer<float> buf (2, 128);
    double noteFreq = 110.0, noteAmp = 0.3, notePhase = 0.0;
    long long sinceNote = 0;
    int hotBurstLeft = 0, nonFinite = 0;

    for (long long b = 0; b < (long long) (seconds * sampleRate / 128.0); ++b)
    {
        if (rng.nextInt (40) == 0)
            for (auto* p : params)
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                {
                    const float v = rng.nextInt (4) == 0 ? (rng.nextBool() ? 0.0f : 1.0f) : rng.nextFloat();
                    *f = f->getNormalisableRange().convertFrom0to1 (v);
                }
        if (rng.nextInt (300) == 0)
            hotBurstLeft = 40;

        for (int i = 0; i < 128; ++i)
        {
            if (++sinceNote > (long long) (0.6 * sampleRate * (0.5 + rng.nextFloat())))
            {
                sinceNote = 0;
                noteFreq = 82.0 * std::pow (2.0, rng.nextInt (30) / 12.0);
                noteAmp = 0.05 + 0.6 * rng.nextFloat();
            }
            noteAmp *= 0.99998;
            notePhase += 2.0 * juce::MathConstants<double>::pi * noteFreq / sampleRate;
            double x = noteAmp * (std::sin (notePhase) + 0.4 * std::sin (2.0 * notePhase) + 0.2 * std::sin (3.0 * notePhase));
            if (hotBurstLeft > 0)
                x *= 6.0;
            buf.setSample (0, i, (float) x);
            buf.setSample (1, i, (float) x);
        }
        if (hotBurstLeft > 0)
            --hotBurstLeft;

        fx.process (buf);
        bool finite = true;
        for (int i = 0; i < 128; ++i)
            finite = finite && std::isfinite (buf.getSample (0, i));
        if (! finite)
            ++nonFinite;
    }
    return nonFinite;
}

} // namespace openguitarmultifx
