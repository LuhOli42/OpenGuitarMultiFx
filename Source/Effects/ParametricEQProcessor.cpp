#include "ParametricEQProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    juce::NormalisableRange<float> gainRange() { return juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f); }

    juce::AudioParameterFloatAttributes dbText()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; });
    }

    juce::AudioParameterFloatAttributes hzText()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        });
    }
}

ParametricEQProcessor::ParametricEQProcessor()
{
    auto gain = [] (const char* id, const char* name) { return std::make_unique<juce::AudioParameterFloat> (id, name, gainRange(), 0.0f, dbText()); };
    auto freq = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (lo, hi, 0.0f, std::log (0.5f) / std::log ((def - lo) / (hi - lo))), def, hzText());
    };
    auto qParam = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.3f, 8.0f, 0.0f, 0.4f), def);
    };

    auto lg = gain ("peq_low_gain", "Low"), lf = freq ("peq_low_freq", "Low Freq", 30.0f, 500.0f, 100.0f);
    auto mg = gain ("peq_lm_gain", "Low-Mid"), mf = freq ("peq_lm_freq", "LM Freq", 80.0f, 2500.0f, 400.0f), mq = qParam ("peq_lm_q", "LM Q", 1.0f);
    auto ng = gain ("peq_hm_gain", "High-Mid"), nf = freq ("peq_hm_freq", "HM Freq", 400.0f, 9000.0f, 2500.0f), nq = qParam ("peq_hm_q", "HM Q", 1.0f);
    auto hg = gain ("peq_high_gain", "High"), hf = freq ("peq_high_freq", "High Freq", 1500.0f, 14000.0f, 6000.0f);
    auto lv = gain ("peq_level", "Level");

    lowGain = lg.get(); lowFreq = lf.get();
    lmGain = mg.get(); lmFreq = mf.get(); lmQ = mq.get();
    hmGain = ng.get(); hmFreq = nf.get(); hmQ = nq.get();
    highGain = hg.get(); highFreq = hf.get();
    level = lv.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("parametric_eq", "Parametric EQ", "|", std::move (lg), std::move (lf),
                                                                         std::move (mg), std::move (mf), std::move (mq), std::move (lv));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("parametric_eq_2", "Page 2", "|", std::move (ng), std::move (nf), std::move (nq),
                                                                        std::move (hg), std::move (hf));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void ParametricEQProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    auto init = [&] (juce::SmoothedValue<float>& s, float v)
    {
        s.reset (newSampleRate, 0.03);
        s.setCurrentAndTargetValue (v);
    };
    init (sLowGain, lowGain->get()); init (sLowFreq, lowFreq->get());
    init (sLmGain, lmGain->get()); init (sLmFreq, lmFreq->get()); init (sLmQ, lmQ->get());
    init (sHmGain, hmGain->get()); init (sHmFreq, hmFreq->get()); init (sHmQ, hmQ->get());
    init (sHighGain, highGain->get()); init (sHighFreq, highFreq->get());
    init (sLevel, level->get());
    updateCoefficients();
    reset();
}

void ParametricEQProcessor::reset()
{
    for (auto& ch : sections)
        for (auto& s : ch)
            s.reset();
}

void ParametricEQProcessor::updateCoefficients() noexcept
{
    const double nyq = 0.49 * sampleRate;
    Biquad low, lm, hm, high;
    low.makeLowShelf (sampleRate, juce::jmin ((double) sLowFreq.getCurrentValue(), nyq), sLowGain.getCurrentValue());
    lm.makePeak (sampleRate, juce::jmin ((double) sLmFreq.getCurrentValue(), nyq), sLmQ.getCurrentValue(), sLmGain.getCurrentValue());
    hm.makePeak (sampleRate, juce::jmin ((double) sHmFreq.getCurrentValue(), nyq), sHmQ.getCurrentValue(), sHmGain.getCurrentValue());
    high.makeHighShelf (sampleRate, juce::jmin ((double) sHighFreq.getCurrentValue(), nyq), sHighGain.getCurrentValue());

    for (auto& ch : sections)
    {
        auto copy = [] (Biquad& dst, const Biquad& src) { dst.b0 = src.b0; dst.b1 = src.b1; dst.b2 = src.b2; dst.a1 = src.a1; dst.a2 = src.a2; };
        copy (ch[0], low); copy (ch[1], lm); copy (ch[2], hm); copy (ch[3], high);
    }
    outputGain = std::pow (10.0, (double) sLevel.getCurrentValue() / 20.0);
}

double ParametricEQProcessor::responseAt (double freq) const noexcept
{
    double m = outputGain;
    for (const auto& s : sections[0])
        m *= s.magnitude (freq, sampleRate);
    return m;
}

void ParametricEQProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    sLowGain.setTargetValue (lowGain->get()); sLowFreq.setTargetValue (lowFreq->get());
    sLmGain.setTargetValue (lmGain->get()); sLmFreq.setTargetValue (lmFreq->get()); sLmQ.setTargetValue (lmQ->get());
    sHmGain.setTargetValue (hmGain->get()); sHmFreq.setTargetValue (hmFreq->get()); sHmQ.setTargetValue (hmQ->get());
    sHighGain.setTargetValue (highGain->get()); sHighFreq.setTargetValue (highFreq->get());
    sLevel.setTargetValue (level->get());

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        sLowGain.getNextValue(); sLowFreq.getNextValue(); sLmGain.getNextValue(); sLmFreq.getNextValue(); sLmQ.getNextValue();
        sHmGain.getNextValue(); sHmFreq.getNextValue(); sHmQ.getNextValue(); sHighGain.getNextValue(); sHighFreq.getNextValue();
        sLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updateCoefficients();
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            double x = buffer.getSample (ch, i);
            for (auto& s : sections[(size_t) ch])
                x = s.process (x);
            buffer.setSample (ch, i, (float) (x * outputGain));
        }
    }
}

void ParametricEQProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No equaliser glyph on the icon sheet yet: the pulse trace (the sheet's Compressor / Expander glyph) stands in until one is added.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
