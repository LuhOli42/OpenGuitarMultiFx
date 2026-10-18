#include "SpectralMorphProcessor.h"

namespace openguitarmultifx
{
namespace
{
    struct Formant { double hz, db, widthOct; };
    struct Shape { const char* name; std::vector<Formant> formants; double tiltDbPerOct; };

    const std::vector<Shape>& shapes()
    {
        static const std::vector<Shape> s = {
            { "CHOIR AH",   { { 700, 0, 0.35 }, { 1220, -4, 0.35 }, { 2600, -9, 0.4 } }, -2.0 },
            { "CHOIR EE",   { { 270, 0, 0.3 }, { 2290, -4, 0.35 }, { 3010, -8, 0.4 } }, -1.0 },
            { "CHOIR OO",   { { 300, 0, 0.3 }, { 870, -5, 0.35 }, { 2240, -14, 0.4 } }, -3.0 },
            { "CHOIR EH",   { { 530, 0, 0.3 }, { 1840, -3, 0.35 }, { 2480, -8, 0.4 } }, -2.0 },
            { "BRASS",      { { 480, 0, 0.5 }, { 1250, 2, 0.5 }, { 2700, -3, 0.6 } }, -3.0 },
            { "STRINGS",    { { 320, 0, 0.9 }, { 1500, -2, 0.9 }, { 4200, -8, 0.8 } }, -5.0 },
            { "REED",       { { 600, 0, 0.35 }, { 1800, -2, 0.4 }, { 3300, -7, 0.4 } }, -2.5 },
            { "NASAL",      { { 900, 0, 0.3 }, { 2100, 2, 0.3 }, { 3400, -1, 0.35 } }, -1.5 },
            { "HOLLOW",     { { 250, 0, 0.45 }, { 3800, -6, 0.6 } }, -4.0 },
            { "BRIGHT",     { { 2500, 0, 0.9 }, { 5200, 0, 0.9 } }, 0.0 },
        };
        return s;
    }
}

juce::StringArray SpectralMorphProcessor::shapeNames()
{
    juce::StringArray n;
    for (const auto& s : shapes()) n.add (s.name);
    return n;
}

SpectralMorphProcessor::SpectralMorphProcessor() : SynthEffect ("Spectral Morph", "ring_mod", "spectralMorph")
{
    shapeA = addChoice ("shapeA", "Shape A", shapeNames(), 0);
    shapeB = addChoice ("shapeB", "Shape B", shapeNames(), 4);
    morph = addFloat ("morph", "Morph", 0, 100, 0, 1.0f, "%", 0);
    lfoRate = addFloat ("lfoRate", "LFO Rate", 0.05f, 8, 0.5f, 0.5f, "Hz", 2);
    lfoDepth = addFloat ("lfoDepth", "LFO Depth", 0, 100, 0, 1.0f, "%", 0);
    flatten = addFloat ("flatten", "Flatten", 0, 100, 70, 1.0f, "%", 0);
    response = addFloat ("response", "Response", 5, 200, 40, 0.5f, "ms", 0);
    dynamics = addFloat ("dynamics", "Dynamics", 0, 100, 80, 1.0f, "%", 0);
    addMixLevel (100);
}

void SpectralMorphProcessor::computeShape (int idx, std::array<double, kBands>& out) const
{
    const auto& s = shapes()[(size_t) juce::jlimit (0, (int) shapes().size() - 1, idx)];
    double sumSq = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        double lin = 0.0;
        for (const auto& f : s.formants)
        {
            const double d = std::log2 (centres[(size_t) b] / f.hz) / f.widthOct;
            lin += juce::Decibels::decibelsToGain (f.db) * std::exp (-0.5 * d * d);
        }
        lin += 0.04;
        lin *= juce::Decibels::decibelsToGain (s.tiltDbPerOct * std::log2 (centres[(size_t) b] / 1000.0));
        out[(size_t) b] = lin;
        sumSq += lin * lin;
    }
    const double norm = 1.0 / std::sqrt (sumSq / kBands);
    for (auto& g : out) g *= norm;
}

void SpectralMorphProcessor::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    const double lo = 90.0, hi = juce::jmin (7000.0, 0.42 * fs);
    for (int b = 0; b < kBands; ++b)
    {
        centres[(size_t) b] = lo * std::pow (hi / lo, (double) b / (kBands - 1));
        bands[(size_t) b].bandpass (centres[(size_t) b], fs, 4.5);
    }
    cachedA = cachedB = -1;
    mixLevel.prepare (fs);
    reset();
}

void SpectralMorphProcessor::reset()
{
    for (auto& b : bands) b.reset();
    for (auto& f : followers) f.reset();
    inputEnv.reset();
    lfoPhase = 0.0;
    current.fill (1.0);
}

void SpectralMorphProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    updateMix();
    const int a = mt::asIndex (shapeA), bIdx = mt::asIndex (shapeB);
    if (a != cachedA) { computeShape (a, shapeAGains); cachedA = a; }
    if (bIdx != cachedB) { computeShape (bIdx, shapeBGains); cachedB = bIdx; }

    const double flat = flatten->get() / 100.0, dyn = dynamics->get() / 100.0;
    const double baseMorph = morph->get() / 100.0, depth = lfoDepth->get() / 100.0;
    const double lfoInc = lfoRate->get() / fs;
    const double ref = 0.08; // band level the flattened bands are pulled toward
    for (auto& f : followers) f.set (3.0, response->get(), fs);
    inputEnv.set (2.0, 60.0, fs);

    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R != nullptr ? R[i] : inL;
        const double mono = 0.5 * (inL + inR);

        lfoPhase += lfoInc;
        lfoPhase -= std::floor (lfoPhase);
        const double lfo = 0.5 + 0.5 * std::sin (2.0 * mt::kPi * lfoPhase);
        const double m = juce::jlimit (0.0, 1.0, baseMorph * (1.0 - depth) + lfo * depth);

        double sum = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const double x = bands[(size_t) b].process (mono);
            const double e = followers[(size_t) b].process (x);
            const double target = shapeAGains[(size_t) b] + (shapeBGains[(size_t) b] - shapeAGains[(size_t) b]) * m;
            const double g = (1.0 - flat) + flat * ref / (e + 0.002);
            sum += x * g * target;
        }
        const double ie = inputEnv.process (mono);
        const double gate = (1.0 - dyn) * juce::jmin (1.0, ie * 40.0) + dyn * juce::jmin (1.0, ie * 6.0);
        const double wet = gs::softClip (sum * 0.6) * ((1.0 - flat) + flat * gate * 1.4);

        const auto gn = nextGains();
        L[i] = (float) gn.apply (inL, wet);
        if (R != nullptr) R[i] = (float) gn.apply (inR, wet);
    }
}

} // namespace openguitarmultifx
