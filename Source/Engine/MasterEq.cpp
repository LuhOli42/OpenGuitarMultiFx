#include "MasterEq.h"

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    double clampFreq (double freqHz, double sampleRate) noexcept
    {
        return juce::jlimit (10.0, 0.49 * sampleRate, freqHz);
    }

    /** Butterworth stage Qs for an Nth-order cascade (N/2 biquads), for the
        cut types' slopeDbOct -> order mapping. */
    double butterworthQ (int order, int stage) noexcept
    {
        // order 4: 0.5412, 1.3065 | order 6: 0.5177, 0.7071, 1.9319 | order 8: 0.5098, 0.6013, 0.8999, 2.5628
        static constexpr double q4[] = { 0.54119610, 1.30656296 };
        static constexpr double q6[] = { 0.51763809, 0.70710678, 1.93185165 };
        static constexpr double q8[] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        if (order <= 2)  return 0.70710678;
        if (order <= 4)  return q4[stage];
        if (order <= 6)  return q6[stage];
        return q8[stage];
    }

    int orderForSlope (int slopeDbOct) noexcept
    {
        return juce::jlimit (2, 8, juce::roundToInt (slopeDbOct / 12.0f) * 2);
    }
}

const char* MasterEq::displayName (MasterEqBand::Type type) noexcept
{
    switch (type)
    {
        case MasterEqBand::Type::lowCut:    return "Low Cut";
        case MasterEqBand::Type::lowShelf:  return "Low Shelf";
        case MasterEqBand::Type::peak:      return "Peak";
        case MasterEqBand::Type::notch:     return "Notch";
        case MasterEqBand::Type::highShelf: return "High Shelf";
        case MasterEqBand::Type::highCut:   return "High Cut";
    }
    return "Peak";
}

void MasterEq::computeSections (const std::vector<MasterEqBand>& bandList, double sr, CoeffSet& out) noexcept
{
    out.count = 0;
    if (sr <= 0.0)
        return;

    Biquad section;
    auto commit = [&]
    {
        if (out.count < (int) out.sections.size())
        {
            auto& s = out.sections[(size_t) out.count++];
            s.b0 = section.b0; s.b1 = section.b1; s.b2 = section.b2;
            s.a1 = section.a1; s.a2 = section.a2;
        }
    };

    for (int bi = 0; bi < (int) bandList.size() && bi < maxBands; ++bi)
    {
        const auto& band = bandList[(size_t) bi];
        if (! band.enabled)
            continue;

        const double freq = clampFreq (band.freqHz, sr);

        switch (band.type)
        {
            case MasterEqBand::Type::lowCut:
            case MasterEqBand::Type::highCut:
            {
                const int order = orderForSlope (band.slopeDbOct);
                const double resonance = juce::jlimit (0.5f, 4.0f, band.q);
                for (int stage = 0; stage < order / 2; ++stage)
                {
                    const double q = butterworthQ (order, stage) * resonance;
                    if (band.type == MasterEqBand::Type::lowCut)
                        section.makeHighPass (sr, freq, q);
                    else
                        section.makeLowPass (sr, freq, q);
                    commit();
                }
                break;
            }
            case MasterEqBand::Type::lowShelf:
                section.makeLowShelf (sr, freq, band.gainDb, juce::jlimit (0.2f, 2.0f, band.q));
                commit();
                break;
            case MasterEqBand::Type::highShelf:
                section.makeHighShelf (sr, freq, band.gainDb, juce::jlimit (0.2f, 2.0f, band.q));
                commit();
                break;
            case MasterEqBand::Type::peak:
                section.makePeak (sr, freq, juce::jlimit (0.3f, 18.0f, band.q), band.gainDb);
                commit();
                break;
            case MasterEqBand::Type::notch:
                section.makeNotch (sr, freq, juce::jlimit (0.3f, 18.0f, band.q));
                commit();
                break;
        }
    }
}

void MasterEq::prepare (double newSampleRate, int numChannels) noexcept
{
    sampleRate = newSampleRate;
    numLiveChannels = juce::jlimit (0, 2, numChannels);

    // Recompute + republish for the new rate: the sections' bilinear
    // coefficients are sample-rate-dependent.
    auto coeffs = std::make_unique<CoeffSet>();
    computeSections (bands, newSampleRate, *coeffs);
    coeffSlot.publish (std::move (coeffs));

    reset();
}

void MasterEq::reset() noexcept
{
    for (auto& channelSections : live)
        for (auto& biquad : channelSections)
            biquad.reset();
    applied = nullptr; // next block re-copies coefficients (z stays cleared -- this is a real reset)
    liveSectionCount = 0;
}

void MasterEq::setBands (const std::vector<MasterEqBand>& newBands)
{
    bands = newBands;
    if (bands.size() > maxBands)
        bands.resize (maxBands);

    if (sampleRate > 0.0)
    {
        auto coeffs = std::make_unique<CoeffSet>();
        computeSections (bands, sampleRate, *coeffs);
        coeffSlot.publish (std::move (coeffs));
    }
}

void MasterEq::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (auto* coeffs = coeffSlot.currentRaw(); coeffs != applied)
    {
        applied = coeffs;
        liveSectionCount = coeffs != nullptr ? coeffs->count : 0;

        if (coeffs != nullptr)
            for (int ch = 0; ch < (int) live.size(); ++ch)
                for (int i = 0; i < liveSectionCount; ++i)
                {
                    const auto& s = coeffs->sections[(size_t) i];
                    auto& b = live[(size_t) ch][(size_t) i];
                    b.b0 = s.b0; b.b1 = s.b1; b.b2 = s.b2; b.a1 = s.a1; b.a2 = s.a2;
                    // z1/z2 kept on purpose: a mid-drag recompute continues the
                    // filter's state instead of restarting it (no click).
                }
    }

    if (liveSectionCount == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), numLiveChannels);
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto* sections = live[(size_t) ch].data();
        for (int i = 0; i < numSamples; ++i)
        {
            double x = data[i];
            for (int s = 0; s < liveSectionCount; ++s)
                x = sections[s].process (x);
            data[i] = (float) x;
        }
    }
}

double MasterEq::magnitudeAt (const std::vector<MasterEqBand>& bandList, double freqHz, double sr) noexcept
{
    CoeffSet coeffs;
    computeSections (bandList, sr, coeffs);

    double magnitude = 1.0;
    Biquad probe;
    for (int i = 0; i < coeffs.count; ++i)
    {
        const auto& s = coeffs.sections[(size_t) i];
        probe.b0 = s.b0; probe.b1 = s.b1; probe.b2 = s.b2; probe.a1 = s.a1; probe.a2 = s.a2;
        magnitude *= probe.magnitude (freqHz, sr);
    }
    return magnitude;
}

} // namespace openguitarmultifx
