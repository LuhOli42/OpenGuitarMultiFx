#include "TrackedSynthProcessor.h"

namespace openguitarmultifx
{
namespace
{
    constexpr int kControlEvery = 16;
}

TrackedSynthProcessor::TrackedSynthProcessor() : SynthEffect ("Tracked Synth", "pitch_shift", "trackedSynth")
{
    wave = addChoice ("wave", "Wave", { "SAW", "SQUARE", "TRIANGLE", "SINE" }, 0);
    octave = addFloat ("octave", "Octave", -2, 1, 0, 1.0f, "oct", 0);
    sub = addFloat ("sub", "Sub Osc", 0, 100, 40, 1.0f, "%", 0);
    detune = addFloat ("detune", "Detune", 0, 40, 8, 1.0f, "ct", 0);
    glide = addFloat ("glide", "Glide", 0, 500, 20, 0.5f, "ms", 0);
    cutoff = addFloat ("cutoff", "Cutoff", 80, 12000, 1800, 0.35f, "Hz", 0);
    resonance = addFloat ("resonance", "Resonance", 0, 90, 25, 1.0f, "%", 0);
    envAmount = addFloat ("envAmount", "Filter Env", 0, 100, 50, 1.0f, "%", 0);
    release = addFloat ("release", "Release", 20, 800, 160, 0.5f, "ms", 0);
    dynamics = addFloat ("dynamics", "Dynamics", 0, 100, 70, 1.0f, "%", 0);
    sensitivity = addFloat ("sensitivity", "Sensitivity", -60, -20, -46, 1.0f, "dB", 0);
    dryGuitar = addFloat ("dry", "Guitar", 0, 100, 0, 1.0f, "%", 0);
    addMixLevel (100);
}

void TrackedSynthProcessor::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    tracker.prepare (fs);
    mixLevel.prepare (fs);
    reset();
}

void TrackedSynthProcessor::reset()
{
    tracker.reset();
    env.reset();
    osc = {}; osc2 = {}; subOsc = {};
    filter.reset();
    curHz = 0.0; amp = 0.0; controlCounter = 0;
}

void TrackedSynthProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    updateMix();
    const auto w = (gs::Wave) mt::asIndex (wave);
    const double octMul = std::exp2 ((double) juce::roundToInt (octave->get()));
    const double detMul = std::exp2 (detune->get() / 1200.0);
    const double subLevel = sub->get() / 100.0;
    const double glideCoef = glide->get() < 1.0f ? 0.0 : std::exp (-1.0 / (glide->get() * 1e-3 * fs));
    const double baseCut = cutoff->get(), res = resonance->get() / 100.0, envAmt = envAmount->get() / 100.0;
    const double dyn = dynamics->get() / 100.0, guitar = dryGuitar->get() / 100.0;
    const double thr = juce::Decibels::decibelsToGain ((double) sensitivity->get());
    env.set (1.5, release->get(), fs);
    const double ampCoef = std::exp (-1.0 / (0.004 * fs));

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : inL;
        const float mono = (float) (0.5 * (inL + inR));
        tracker.push (mono);
        const double e = env.process (mono);
        const bool open = tracker.voiced && e > thr;

        if (open)
        {
            const double target = tracker.hz * octMul;
            curHz = curHz <= 0.0 ? target : target + (curHz - target) * glideCoef;
        }
        const double ampTarget = open ? (1.0 - dyn) * 0.6 + dyn * juce::jmin (1.0, e * 5.0) : 0.0;
        amp = ampTarget + (amp - ampTarget) * ampCoef;

        double y = 0.0;
        if (curHz > 0.0 && amp > 1e-5)
        {
            const double inc = curHz / fs;
            y = osc.next (w, inc) + osc2.next (w, inc * detMul) * 0.8;
            y += subLevel * subOsc.next (gs::Wave::square, 0.5 * inc) * 0.9;
            y *= 0.35;
        }

        if (controlCounter == 0)
        {
            const double sweep = envAmt * juce::jmin (1.0, e * 6.0) * 4.0;
            filter.set (juce::jmin (16000.0, baseCut * std::exp2 (sweep)), res, fs);
        }
        controlCounter = (controlCounter + 1) % kControlEvery;
        const double wet = gs::softClip (filter.lp (y)) * amp;

        const auto gn = nextGains(); // the dry guitar rides inside the wet path so Mix = 0 stays a clean bypass
        L[i] = (float) gn.apply (inL, wet + guitar * inL);
        if (R != nullptr) R[i] = (float) gn.apply (inR, wet + guitar * inR);
    }
}

} // namespace openguitarmultifx
