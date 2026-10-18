#include "DividerOctaverProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;
    Rational lowpass1 (double hz) { return { { 1.0 }, { 1.0, 1.0 / (2.0 * juce::MathConstants<double>::pi * hz) } }; }
}

DividerOctaverProcessor::DividerOctaverProcessor (DividerOctaverModel m) : model (m)
{
    juce::String prefix, name;
    switch (model)
    {
        case DividerOctaverModel::octaveMultiplexer: prefix = "octmux"; name = "Octave Multiplexer"; break;
        case DividerOctaverModel::fx35: prefix = "fx35"; name = "Octoplus"; break;
        case DividerOctaverModel::oct1: prefix = "oct1"; name = "OCT-1"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");
    switch (model)
    {
        case DividerOctaverModel::octaveMultiplexer:
            octave = add (prefix + "_blend", "Blend", 0.6f);       // dry (ccw) <-> subharmonic (cw)
            toneOrFilter = add (prefix + "_filter", "Filter", 0.5f);
            toggle = add (prefix + "_bass", "Bass", 0.0f, 2, { "Off", "On" });
            break;
        case DividerOctaverModel::fx35:
            direct = add (prefix + "_direct", "Direct Level", 0.6f);
            octave = add (prefix + "_octave", "Octave Level", 0.6f);
            toneOrFilter = add (prefix + "_tone", "Tone", 0.5f);
            break;
        case DividerOctaverModel::oct1:
            direct = add (prefix + "_direct", "Direct Level", 0.6f);
            octave = add (prefix + "_effect", "Effect Level", 0.6f);
            toggle = add (prefix + "_octave", "Octave", 0.0f, 2, { "-1", "-2" });
            break;
    }
}

const char* DividerOctaverProcessor::getName() const
{
    switch (model)
    {
        case DividerOctaverModel::octaveMultiplexer: return "Octave Multiplexer";
        case DividerOctaverModel::fx35: return "Octoplus";
        case DividerOctaverModel::oct1: return "OCT-1";
    }
    return "Octaver";
}

juce::AudioParameterFloat* DividerOctaverProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
                                                         juce::StringArray labels)
{
    std::unique_ptr<juce::AudioParameterFloat> p;
    if (positions >= 2)
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, (float) (positions - 1), 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([labels] (float v, int)
            { return labels[juce::jlimit (0, labels.size() - 1, juce::roundToInt (v))]; }));
    else
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    auto* raw = p.get();
    parameters->addChild (std::move (p));
    return raw;
}

void DividerOctaverProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        ch.inputHp.prepare (couplingHighpass (0.022e-6, 1.0e6), newSampleRate, 0.0);
        switch (model)
        {
            case DividerOctaverModel::octaveMultiplexer:
                // 700 Hz-ish analyser, the sub low-passed by the Filter control (Tone 10K + 1.8K, .15 u) and Bass (.012 / 150K).
                ch.analyser.prepare (newSampleRate, 500.0, 2, 0.35, 0.15);
                ch.subLp1.prepare (lowpass1 (600.0), newSampleRate, 0.0);
                ch.bassLp.prepare (lowpass1 (88.0), newSampleRate, 0.0);
                break;
            case DividerOctaverModel::fx35:
                // The drawing's "3rd order lowpass" (100K / .033 / 120p) in front of the comparators; the octave out
                // through 220K / .005 / 220K / 120p (~140 Hz + ~6 kHz).
                ch.analyser.prepare (newSampleRate, 350.0, 3, 0.3, 0.15);
                ch.subLp1.prepare (lowpass1 (145.0), newSampleRate, 0.0);
                ch.subLp2.prepare (lowpass1 (2500.0), newSampleRate, 0.0);
                break;
            case DividerOctaverModel::oct1:
                // R39/R38 330K / 2.2n and 220p in front of IC3; the VCA's square out through R5 27K / C5 .022 (~270 Hz).
                ch.analyser.prepare (newSampleRate, 220.0, 2, 0.3, 0.12);
                ch.subLp1.prepare (lowpass1 (268.0), newSampleRate, 0.0);
                break;
        }
        ch.ff.reset();
        ch.env = 0.0;
    }
    envA = 1.0 - std::exp (-1.0 / (0.01 * newSampleRate));
    for (auto* s : { &smoothedDirect, &smoothedOctave, &smoothedTone })
        s->reset (newSampleRate, paramRampSeconds);
    smoothedDirect.setCurrentAndTargetValue (direct != nullptr ? direct->get() : 0.0f);
    smoothedOctave.setCurrentAndTargetValue (octave->get());
    smoothedTone.setCurrentAndTargetValue (toneOrFilter != nullptr ? toneOrFilter->get() : 0.5f);
    shortcut.reset();
}

void DividerOctaverProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.inputHp.reset();
        ch.subLp1.reset();
        ch.subLp2.reset();
        ch.bassLp.reset();
        ch.analyser.reset();
        ch.ff.reset();
        ch.env = 0.0;
    }
    shortcut.reset();
}

void DividerOctaverProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;
    const int numSamples = buffer.getNumSamples();
    const int solve = shortcut.begin (channels, buffer, sampleRate);
    if (direct != nullptr) smoothedDirect.setTargetValue (direct->get());
    smoothedOctave.setTargetValue (octave->get());
    if (toneOrFilter != nullptr) smoothedTone.setTargetValue (toneOrFilter->get());
    const bool bass = model == DividerOctaverModel::octaveMultiplexer && toggle->get() >= 0.5f;
    const int stage = model == DividerOctaverModel::oct1 && toggle->get() >= 0.5f ? 1 : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const double dLevel = smoothedDirect.getNextValue(), oLevel = smoothedOctave.getNextValue(), t = smoothedTone.getNextValue();
        for (int c = 0; c < solve; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = ch.inputHp.process (data[i]);
            if (ch.analyser.process (x))
                ch.ff.clock();
            const double polarity = ch.ff.q[stage] ? 1.0 : -1.0;
            double out = 0.0;
            switch (model)
            {
                case DividerOctaverModel::octaveMultiplexer:
                {
                    // 2N4302 choppers: the analyser-filtered signal switched between its two phase-split copies.
                    double sub = ch.subLp1.process (polarity * ch.analyser.lastFiltered()) * 2.0;
                    // Filter: the Tone pot blends the bright and the rolled-off sub (cw = brighter).
                    sub = t * sub + (1.0 - t) * ch.bassLp.process (sub) * 1.5;
                    if (bass)
                        sub *= 1.4;
                    out = std::min (1.0, 2.0 * (1.0 - oLevel)) * x + std::min (1.0, 2.0 * oLevel) * sub;
                    break;
                }
                case DividerOctaverModel::fx35:
                {
                    const double sub = ch.subLp2.process (ch.subLp1.process (polarity * ch.analyser.lastFiltered())) * 2.5;
                    // Tone: 100K pot from the octave towards a 10K / 0.1u shelf -- cw = brighter.
                    const double toned = sub * (0.6 + 0.8 * t);
                    out = 2.0 * dLevel * x + 2.0 * oLevel * toned;
                    break;
                }
                case DividerOctaverModel::oct1:
                {
                    // The divided square through the NJM13600 VCA, its gain the envelope of the playing.
                    ch.env += envA * (std::abs (x) - ch.env);
                    const double sq = polarity * 1.6 * ch.env;
                    out = 2.0 * dLevel * x + 2.0 * oLevel * ch.subLp1.process (sq) * 2.0;
                    break;
                }
            }
            data[i] = (float) out;
        }
    }
    shortcut.end (buffer);
}

void DividerOctaverProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::octaver_svg, IconData::octaver_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
