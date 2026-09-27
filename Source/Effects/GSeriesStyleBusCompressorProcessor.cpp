#include "GSeriesStyleBusCompressorProcessor.h"
#include "CompressorCommon.h"
#include "IconKit.h"

#include <IconData.h>

#include <functional>

namespace openguitarmultifx
{

namespace
{
    juce::AudioParameterFloatAttributes selectorText (std::function<juce::String (int)> name)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([name] (float v, int) { return name (juce::roundToInt (v)); });
    }
}

GSeriesStyleBusCompressorProcessor::GSeriesStyleBusCompressorProcessor()
{
    auto thr = std::make_unique<juce::AudioParameterFloat> ("gbus_threshold", "Threshold", juce::NormalisableRange<float> (-40.0f, 0.0f, 0.5f), -18.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
    auto rat = std::make_unique<juce::AudioParameterFloat> ("gbus_ratio", "Ratio", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        selectorText ([] (int i) { const char* n[] = { "2:1", "4:1", "10:1" }; return juce::String (n[juce::jlimit (0, 2, i)]); }));
    auto atk = std::make_unique<juce::AudioParameterFloat> ("gbus_attack", "Attack", juce::NormalisableRange<float> (0.0f, 5.0f, 1.0f), 3.0f,
        selectorText ([] (int i) { const char* n[] = { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }; return juce::String (n[juce::jlimit (0, 5, i)]); }));
    auto rel = std::make_unique<juce::AudioParameterFloat> ("gbus_release", "Release", juce::NormalisableRange<float> (0.0f, 4.0f, 1.0f), 2.0f,
        selectorText ([] (int i) { const char* n[] = { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }; return juce::String (n[juce::jlimit (0, 4, i)]); }));
    auto mk = std::make_unique<juce::AudioParameterFloat> ("gbus_makeup", "Make-up", juce::NormalisableRange<float> (0.0f, 15.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "+" + juce::String (v, 1) + " dB"; }));
    auto hp = std::make_unique<juce::AudioParameterFloat> ("gbus_hpf", "SC HPF", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        selectorText ([] (int i) { const char* n[] = { "Off", "60 Hz", "90 Hz" }; return juce::String (n[juce::jlimit (0, 2, i)]); }));

    threshold = thr.get(); ratio = rat.get(); attack = atk.get(); release = rel.get(); makeup = mk.get(); highPass = hp.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("gbus", "G-Series-Style Bus Compressor", "|", std::move (thr), std::move (rat),
        std::move (atk), std::move (rel), std::move (mk), std::move (hp));
}

void GSeriesStyleBusCompressorProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    appliedHpf = -1.0f;
    reset();
}

void GSeriesStyleBusCompressorProcessor::reset()
{
    for (auto& f : scHpf)
        f.reset();
    grDb = grFast = grSlow = 0.0;
}

void GSeriesStyleBusCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int ratioIndex = juce::jlimit (0, 2, juce::roundToInt (ratio->get()));
    const double r = ratios[(size_t) ratioIndex];
    const double thr = threshold->get() + thresholdShiftDb[(size_t) ratioIndex];
    const double aA = dyn::smoothingCoefficient (attackSeconds[(size_t) juce::jlimit (0, 5, juce::roundToInt (attack->get()))], sampleRate);
    const int releaseIndex = juce::jlimit (0, 4, juce::roundToInt (release->get()));
    const bool autoRelease = releaseIndex == 4;
    const double aR = dyn::smoothingCoefficient (autoRelease ? 0.1 : releaseSeconds[(size_t) releaseIndex], sampleRate);
    const double aRslow = dyn::smoothingCoefficient (1.2, sampleRate);
    const double makeupGain = dyn::fromDb (makeup->get());

    const int hpfIndex = juce::jlimit (0, 2, juce::roundToInt (highPass->get()));
    if ((float) hpfIndex != appliedHpf)
    {
        appliedHpf = (float) hpfIndex;
        for (auto& f : scHpf)
            if (hpfIndex > 0)
                f.makeHighPass (sampleRate, hpfIndex == 1 ? 60.0 : 90.0);
            else
            {
                f = Biquad {};
            }
    }

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        // True-peak full-wave detector on each channel (through the side-chain high-pass), the dominant channel wins.
        double peak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max (peak, std::abs (scHpf[(size_t) ch].process ((double) buffer.getSample (ch, i))));

        const double targetDb = dyn::staticReductionDb (dyn::toDb (peak), thr, r, kneeDb);

        if (autoRelease)
        {
            grFast += (targetDb - grFast) * (targetDb > grFast ? aA : aR);
            grSlow += (targetDb - grSlow) * (targetDb > grSlow ? aA * 0.25 : aRslow);
            grDb = std::max (grFast, 0.75 * grSlow);
        }
        else
        {
            grDb += (targetDb - grDb) * (targetDb > grDb ? aA : aR);
        }

        const float g = (float) (dyn::fromDb (-grDb) * makeupGain);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

void GSeriesStyleBusCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
