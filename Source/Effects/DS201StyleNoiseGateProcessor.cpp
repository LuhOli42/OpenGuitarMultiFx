#include "DS201StyleNoiseGateProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    juce::AudioParameterFloatAttributes msText()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " s";
            if (v >= 10.0f)   return juce::String (juce::roundToInt (v)) + " ms";
            if (v >= 1.0f)    return juce::String (v, 1) + " ms";
            return juce::String (juce::roundToInt (v * 1000.0f)) + " us";
        });
    }

    juce::AudioParameterFloatAttributes hzText()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        });
    }

    juce::NormalisableRange<float> logRange (float lo, float hi, float def)
    {
        return juce::NormalisableRange<float> (lo, hi, 0.0f, std::log (0.5f) / std::log ((def - lo) / (hi - lo)));
    }
}

DS201StyleNoiseGateProcessor::DS201StyleNoiseGateProcessor()
{
    auto thr = std::make_unique<juce::AudioParameterFloat> ("ds201_threshold", "Threshold", juce::NormalisableRange<float> (-72.0f, -18.0f, 0.5f), -50.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
    auto rng = std::make_unique<juce::AudioParameterFloat> ("ds201_range", "Range", juce::NormalisableRange<float> (-80.0f, 0.0f, 0.5f), -80.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 0) + " dB"; }));
    auto atk = std::make_unique<juce::AudioParameterFloat> ("ds201_attack", "Attack", logRange (0.01f, 1000.0f, 1.0f), 1.0f, msText());
    auto hld = std::make_unique<juce::AudioParameterFloat> ("ds201_hold", "Hold", logRange (2.0f, 2000.0f, 60.0f), 60.0f, msText());
    auto dcy = std::make_unique<juce::AudioParameterFloat> ("ds201_decay", "Decay", logRange (2.0f, 4000.0f, 200.0f), 200.0f, msText());
    auto lf = std::make_unique<juce::AudioParameterFloat> ("ds201_lf", "L.F. Filter", logRange (25.0f, 4000.0f, 100.0f), 25.0f, hzText());
    auto hf = std::make_unique<juce::AudioParameterFloat> ("ds201_hf", "H.F. Filter", logRange (250.0f, 35000.0f, 3000.0f), 35000.0f, hzText());
    auto key = std::make_unique<juce::AudioParameterFloat> ("ds201_key", "Key Listen", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v < 0.5f ? "Off" : "On"); }));

    threshold = thr.get(); range = rng.get(); attack = atk.get(); hold = hld.get(); decay = dcy.get();
    lowCut = lf.get(); highCut = hf.get(); keyListen = key.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ds201", "DS201-Style Noise Gate", "|", std::move (thr), std::move (rng),
        std::move (atk), std::move (hld), std::move (dcy), std::move (lf), std::move (hf), std::move (key));
}

void DS201StyleNoiseGateProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    appliedLow = appliedHigh = -1.0f;
    updateFilters();
    reset();
}

void DS201StyleNoiseGateProcessor::reset()
{
    for (auto& f : hpf)
        f.reset();
    for (auto& f : lpf)
        f.reset();
    detector = 0.0;
    gainDb = 0.0;
    holdCounter = 0;
    open = false;
}

void DS201StyleNoiseGateProcessor::updateFilters() noexcept
{
    const float lo = lowCut->get(), hi = highCut->get();
    if (lo == appliedLow && hi == appliedHigh)
        return;
    appliedLow = lo;
    appliedHigh = hi;
    const double nyq = 0.45 * sampleRate;
    for (auto& f : hpf)
    {
        // preserve the state across a coefficient change
        const double z1 = f.z1, z2 = f.z2;
        f.makeHighPass (sampleRate, juce::jmin ((double) lo, nyq));
        f.z1 = z1; f.z2 = z2;
    }
    for (auto& f : lpf)
    {
        const double z1 = f.z1, z2 = f.z2;
        f.makeLowPass (sampleRate, juce::jmin ((double) hi, nyq));
        f.z1 = z1; f.z2 = z2;
    }
}

void DS201StyleNoiseGateProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    updateFilters();

    const double thresholdLin = std::pow (10.0, (double) threshold->get() / 20.0);
    const double rangeDb = -(double) range->get(); // positive
    const double attackSamples = juce::jmax (1.0, (double) attack->get() * 0.001 * sampleRate);
    const double decaySamples = juce::jmax (1.0, (double) decay->get() * 0.001 * sampleRate);
    const long long holdSamples = (long long) ((double) hold->get() * 0.001 * sampleRate);
    const bool listen = keyListen->get() >= 0.5f;

    // The key's detector: a peak follower that rises at once and falls in ~1 ms (a DS201's is fast enough for a 10 us attack setting).
    const double detRelease = std::exp (-1.0 / (0.001 * sampleRate));

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        double keyPeak = 0.0;
        std::array<double, 2> keySample { 0.0, 0.0 };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double k = lpf[(size_t) ch].process (hpf[(size_t) ch].process ((double) buffer.getSample (ch, i)));
            keySample[(size_t) ch] = k;
            keyPeak = juce::jmax (keyPeak, std::abs (k));
        }
        detector = keyPeak > detector ? keyPeak : detector * detRelease;

        if (detector >= thresholdLin)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
            --holdCounter;
        else
            open = false;

        if (open)
            gainDb = juce::jmin (0.0, gainDb + rangeDb / attackSamples);
        else
            gainDb = juce::jmax (-rangeDb, gainDb - rangeDb / decaySamples);

        const float g = (float) std::pow (10.0, gainDb / 20.0);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, listen ? (float) keySample[(size_t) ch] : buffer.getSample (ch, i) * g);
    }
}

void DS201StyleNoiseGateProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::gate_svg, IconData::gate_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
