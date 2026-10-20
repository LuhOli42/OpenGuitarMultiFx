#include "SubOctaveSynthProcessor.h"

namespace openguitarmultifx
{

SubOctaveSynthProcessor::SubOctaveSynthProcessor() : SynthEffect ("Sub-Octave Synth", "octaver", "subOctaveSynth")
{
    sub1 = addFloat ("sub1", "Oct -1", 0, 100, 70, 1.0f, "%", 0);
    sub2 = addFloat ("sub2", "Oct -2", 0, 100, 30, 1.0f, "%", 0);
    dryLevel = addFloat ("dry", "Guitar", 0, 100, 60, 1.0f, "%", 0);
    filterCut = addFloat ("cutoff", "Filter", 100, 8000, 900, 0.35f, "Hz", 0);
    resonance = addFloat ("resonance", "Resonance", 0, 90, 30, 1.0f, "%", 0);
    sweep = addFloat ("sweep", "Env Sweep", 0, 100, 40, 1.0f, "%", 0);
    dynamics = addFloat ("dynamics", "Dynamics", 0, 100, 80, 1.0f, "%", 0);
    ringAmount = addFloat ("ring", "Ring Mod", 0, 100, 0, 1.0f, "%", 0);
    ringFreq = addFloat ("ringFreq", "Ring Freq", 20, 2000, 220, 0.4f, "Hz", 0);
    drive = addFloat ("drive", "Drive", 0, 100, 0, 1.0f, "%", 0);
    addMixLevel (100);
}

void SubOctaveSynthProcessor::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    trackLp[0].lowpass (700.0, fs, 0.54);
    trackLp[1].lowpass (700.0, fs, 1.31);
    mixLevel.prepare (fs);
    reset();
}

void SubOctaveSynthProcessor::reset()
{
    trackLp[0].reset(); trackLp[1].reset();
    env.reset(); filter.reset();
    prev = peak = ringPhase = amp = 0.0;
    armed = ff1 = ff2 = false;
    controlCounter = 0;
}

void SubOctaveSynthProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    updateMix();
    const double s1 = sub1->get() / 100.0, s2 = sub2->get() / 100.0, guitar = dryLevel->get() / 100.0;
    const double ring = ringAmount->get() / 100.0, ringInc = ringFreq->get() / fs;
    const double baseCut = filterCut->get(), res = resonance->get() / 100.0, sw = sweep->get() / 100.0, dyn = dynamics->get() / 100.0;
    const double drv = 1.0 + drive->get() * 0.08;
    env.set (1.0, 70.0, fs);
    const double ampCoef = std::exp (-1.0 / (0.003 * fs));
    const double peakDecay = std::exp (-1.0 / (0.05 * fs));

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : inL;
        const double mono = 0.5 * (inL + inR);
        const double y = trackLp[1].process (trackLp[0].process (mono));
        const double a = std::abs (y);
        peak = a > peak ? a : peak * peakDecay;
        const double h = 0.15 * peak;
        if (y < -h) armed = true;
        if (armed && prev < 0.0 && y >= 0.0 && peak > 4e-4)
        {
            armed = false;
            ff1 = ! ff1;
            if (ff1) ff2 = ! ff2; // ÷4 toggles on every second ÷2 edge
        }
        prev = y;

        const double e = env.process (mono);
        const double ampTarget = e > 6e-4 ? (1.0 - dyn) * 0.5 + dyn * juce::jmin (1.0, e * 5.0) : 0.0;
        amp = ampTarget + (amp - ampTarget) * ampCoef;
        if (controlCounter == 0)
            filter.set (juce::jmin (14000.0, baseCut * std::exp2 (sw * juce::jmin (1.0, e * 6.0) * 3.0)), res, fs);
        controlCounter = (controlCounter + 1) & 15;

        const double square = (ff1 ? 1.0 : -1.0) * s1 + (ff2 ? 1.0 : -1.0) * s2;
        const double subs = filter.lp (square * 0.5) * amp;
        ringPhase += ringInc;
        ringPhase -= std::floor (ringPhase);
        const double carrier = std::sin (2.0 * mt::kPi * ringPhase);

        const auto gn = nextGains();
        auto chain = [&] (double x) {
            const double dry = gs::softClip (x * drv) / (1.0 + 0.2 * (drv - 1.0));
            return subs + guitar * dry + ring * x * carrier;
        };
        L[i] = (float) gn.apply (inL, chain (inL));
        if (R != nullptr) R[i] = (float) gn.apply (inR, chain (inR));
    }
}

} // namespace openguitarmultifx
