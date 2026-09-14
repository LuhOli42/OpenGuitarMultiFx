#include "PolyphonicPitchDetector.h"

#include <cmath>

namespace openguitarmultifx
{

PolyphonicPitchDetector::PolyphonicPitchDetector()
{
    startTimer (100); // sweeps bandSetSlot -- see DeferredReclaimer
}

PolyphonicPitchDetector::~PolyphonicPitchDetector() = default;

void PolyphonicPitchDetector::prepare (double sampleRateToUse, int maxBlockSize)
{
    sampleRate = sampleRateToUse;
    preparedBlockSize = maxBlockSize;
    bandScratch.assign ((size_t) maxBlockSize, 0.0f);

    if (! pendingTuning.empty())
        setTuning (pendingTuning);
}

void PolyphonicPitchDetector::setTuning (const TuningProfile& tuning)
{
    pendingTuning = tuning;

    if (sampleRate <= 0.0)
        return; // prepare() will apply pendingTuning once it knows the sample rate

    auto newSet = std::make_unique<BandSet>();

    for (auto& tuned : tuning)
    {
        auto band = std::make_unique<Band>();
        band->targetFrequencyHz = tuned.frequencyHz;
        band->noteName = tuned.noteName;

        // Q=3: wide enough to still pass a string that's a good way out of
        // tune (this IS a tuner -- it has to work before you're in tune),
        // narrow enough that neighbouring strings (guitar strings are
        // roughly a 4th/5th apart) don't dominate YIN's fundamental guess.
        const auto centreHz = juce::jlimit (20.0f, (float) (sampleRate * 0.45), tuned.frequencyHz);
        *band->filter.coefficients = *juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centreHz, 3.0f);

        // A shared, one-size-fits-all PitchDetector range/threshold
        // doesn't work here: (1) the default 70-1200Hz range can't even
        // find several extended-range/bass strings (B0/E1/F#1/A1/B1 all
        // sit under 70Hz), so every band needs its OWN range sized around
        // its actual target; (2) a bandpass-filtered signal's RMS is well
        // below what the shared 0.01 silence threshold (tuned for a
        // full-band signal) expects, so bands were reading "silence" on a
        // string that was clearly being played -- a lower threshold here
        // is the fix. Range: roughly -6/+7 semitones around the target,
        // generous enough to still get a reading on a string that's
        // badly out of tune (the whole point of a tuner) while staying
        // clear of neighbouring strings.
        band->pitchDetector.prepare (sampleRate, tuned.frequencyHz * 0.7f, tuned.frequencyHz * 1.5f, 0.001f);

        newSet->bands.push_back (std::move (band));
    }

    bandSetSlot.publish (std::move (newSet));
}

void PolyphonicPitchDetector::pushSamples (const float* data, int numSamples) noexcept
{
    auto* bandSet = bandSetSlot.currentRaw();
    if (bandSet == nullptr || numSamples > (int) bandScratch.size())
        return;

    for (auto& band : bandSet->bands)
    {
        for (int i = 0; i < numSamples; ++i)
            bandScratch[(size_t) i] = band->filter.processSample (data[i]);

        band->pitchDetector.pushSamples (bandScratch.data(), numSamples);

        const float hz = band->pitchDetector.getDetectedFrequencyHz();
        const bool isActive = hz > 0.0f;
        band->active.store (isActive, std::memory_order_relaxed);
        band->frequencyHz.store (hz, std::memory_order_relaxed);

        if (isActive && band->targetFrequencyHz > 0.0f)
        {
            const float cents = 1200.0f * std::log2 (hz / band->targetFrequencyHz);
            band->cents.store (juce::jlimit (-50.0f, 50.0f, cents), std::memory_order_relaxed);
        }
    }
}

int PolyphonicPitchDetector::getNumStrings() const noexcept
{
    auto* bandSet = bandSetSlot.currentRaw();
    return bandSet != nullptr ? (int) bandSet->bands.size() : 0;
}

PolyphonicPitchDetector::StringReading PolyphonicPitchDetector::getReading (int stringIndex) const noexcept
{
    auto* bandSet = bandSetSlot.currentRaw();
    if (bandSet == nullptr || stringIndex < 0 || stringIndex >= (int) bandSet->bands.size())
        return {};

    auto& band = *bandSet->bands[(size_t) stringIndex];
    return { band.active.load (std::memory_order_relaxed),
             band.frequencyHz.load (std::memory_order_relaxed),
             band.cents.load (std::memory_order_relaxed),
             band.noteName };
}

} // namespace openguitarmultifx
