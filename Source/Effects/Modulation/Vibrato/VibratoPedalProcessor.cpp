#include "VibratoPedalProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"
#include "Effects/PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    double triangle (double p) { return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; }


    // Hammond: the scanner's sweep for V1 / V2 / V3 as a fraction of the delay line (~1.1 ms end to end).
    constexpr double hammondLineSeconds = 1.1e-3;
    constexpr double hammondDepth[3] { 0.35, 0.6, 1.0 };
    constexpr double hammondScanHz = 6.87; // 412 rpm scanner on a 60 Hz synchronous motor
}

VibratoPedalProcessor::VibratoPedalProcessor (VibratoModel m) : model (m)
{
    juce::String prefix, name;
    switch (model)
    {
        case VibratoModel::vb2: prefix = "vb2"; name = "VB-2"; break;
        case VibratoModel::magnatone: prefix = "magnatone"; name = "Magnatone Vibrato"; break;
        case VibratoModel::cloneTheory: prefix = "clonetheory"; name = "Clone Theory"; break;
        case VibratoModel::hammondScanner: prefix = "scanner"; name = "Scanner Vibrato"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");
    switch (model)
    {
        case VibratoModel::vb2:
            rate = add (prefix + "_rate", "Rate", 0.4f);
            depth = add (prefix + "_depth", "Depth", 0.5f);
            riseTime = add (prefix + "_rise", "Rise Time", 0.2f);
            break;
        case VibratoModel::magnatone:
            rate = add (prefix + "_speed", "Speed", 0.5f);
            depth = add (prefix + "_intensity", "Intensity", 0.6f);
            break;
        case VibratoModel::cloneTheory:
            rate = add (prefix + "_rate", "Rate", 0.4f);
            depth = add (prefix + "_depth", "Depth", 0.5f);
            blend = add (prefix + "_chvib", "Chorus-Vibrato", 1.0f);
            mode = add (prefix + "_vibraflange", "Vibraflange", 0.0f, 2, { "Off", "On" });
            break;
        case VibratoModel::hammondScanner:
            mode = add (prefix + "_mode", "Mode", 2.0f, 6, { "V1", "V2", "V3", "C1", "C2", "C3" });
            break;
    }
}

const char* VibratoPedalProcessor::getName() const
{
    switch (model)
    {
        case VibratoModel::vb2: return "VB-2";
        case VibratoModel::magnatone: return "Magnatone Vibrato";
        case VibratoModel::cloneTheory: return "Clone Theory";
        case VibratoModel::hammondScanner: return "Scanner Vibrato";
    }
    return "Vibrato";
}

juce::AudioParameterFloat* VibratoPedalProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
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

void VibratoPedalProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    // VB-2: the BYOC Analog Vibrato (a VB-2 clone, schematic rev 2.0, docs/circuits/VB2Vibrato.md). Q2 Sallen-Key ahead
    // of the MN3207 (R8 / R9 10k, C5 .01 feedback, C6 47p to ground); Q3 3rd-order after it (R13-R15 10k; C9 2n7, C10
    // 4n7 feedback, C11 220p).
    const auto vb2In = bbdchorus::bbdFilter ({ { 10.0e3, 0.0, 0.0, 10.0e-9, 0.0 }, { 10.0e3, 0.0, 47.0e-12, 0.0, 0.0 } });
    const auto vb2Out = bbdchorus::bbdFilter ({ { 10.0e3, 0.0, 2.7e-9, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 4.7e-9, 0.0 },
                                                { 10.0e3, 0.0, 220.0e-12, 0.0, 0.0 } });
    // Clone Theory: R5 10K / C4 22n ahead of the MN3007 (a dark 720 Hz pole -- the pedal is famously murky) and the
    // IC1B Sallen-Key (51K, 680p / 680p) after it.
    const auto ctIn = bbdchorus::bbdFilter ({ { 10.0e3, 0.0, 22.0e-9, 0.0, 0.0 }, { 2.4e3, 0.0, 2.2e-9, 0.0, 0.0 } });
    const auto ctOut = bbdchorus::bbdFilter ({ { 51.0e3, 0.0, 0.0, 680.0e-12, 0.0 }, { 51.0e3, 0.0, 680.0e-12, 0.0, 0.0 } });
    for (auto& ch : channels)
    {
        if (model == VibratoModel::vb2)
        {
            ch.bbd.prepare (newSampleRate, 1024, vb2In, vb2Out); // MN3207
            // IC1B pre-emphasis (R5 47k || C3 6n8 + R4 7k5 in, R6 47k || C4 33p feedback) and the output op-amp's
            // de-emphasis (R20 47k in, R21 47k || C15 6n8 + R22 10k || C14 33p feedback). Wet only.
            ch.pre.prepare (inverting (parallelRC (47.0e3, 33.0e-12), parallel (resistor (47.0e3), seriesRC (7.5e3, 6.8e-9))), newSampleRate, 2340.0);
            ch.post.prepare (inverting (parallel (parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9)), capacitor (33.0e-12)), resistor (47.0e3)), newSampleRate, 2340.0);
        }
        else if (model == VibratoModel::cloneTheory)
        {
            ch.bbd.prepare (newSampleRate, 1024, ctIn, ctOut); // MN3007
            ch.pre.prepare (Rational { { 1.0 }, { 1.0 } }, newSampleRate, 0.0);
            ch.post.prepare (Rational { { 1.0 }, { 1.0 } }, newSampleRate, 0.0);
        }
        ch.line.prepare (newSampleRate, 0.004);
        ch.lineLoss.prepare (newSampleRate, 5500.0, 0.6); // the LC line's own cutoff
    }
    smoothedDepth.reset (newSampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth != nullptr ? depth->get() : 1.0f);
    reset();
}

void VibratoPedalProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.bbd.reset();
        ch.pre.reset();
        ch.post.reset();
        ch.line.reset();
        ch.lineLoss.reset();
        for (auto& s : ch.phase)
            s.reset();
        ch.lastWet = 0.0;
    }
    phase = 0.0;
    rise = 0.0; // VB-2: every engage ramps the depth in over Rise Time
    shortcut.reset();
}

void VibratoPedalProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const bool stereoMagnatone = model == VibratoModel::magnatone && numChannels > 1;
    const int solve = stereoMagnatone ? 2 : shortcut.begin (channels, buffer, sampleRate);

    double hz = 0.0;
    switch (model)
    {
        case VibratoModel::vb2: hz = 0.3 * std::pow (12.0 / 0.3, (double) rate->get()); break;       // ~0.3-12 Hz
        case VibratoModel::magnatone: hz = 3.0 + 6.0 * rate->get(); break;                            // ~3-9 Hz
        case VibratoModel::cloneTheory: hz = 0.1 * std::pow (10.0 / 0.1, pots::reverseAudio (rate->get())); break;
        case VibratoModel::hammondScanner: hz = hammondScanHz; break;
    }
    if (depth != nullptr)
        smoothedDepth.setTargetValue (depth->get());
    const double riseStep = riseTime != nullptr ? 1.0 / ((0.01 + 3.0 * riseTime->get()) * sampleRate) : 1.0;
    const int hammondMode = mode != nullptr && model == VibratoModel::hammondScanner ? juce::jlimit (0, 5, juce::roundToInt (mode->get())) : 0;
    const double ctBlend = blend != nullptr ? (double) blend->get() : 1.0;
    const bool vibraflange = model == VibratoModel::cloneTheory && mode->get() >= 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double d = smoothedDepth.getNextValue();
        phase += hz / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        rise = std::min (1.0, rise + riseStep);

        for (int c = 0; c < solve; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];
            double out = x;

            switch (model)
            {
                case VibratoModel::vb2:
                {
                    // A true sine LFO into the MN3102: ~4.5 ms centre, up to +-3 ms of sweep at full Depth (calibrated).
                    lastDelay = 4.5e-3 * (1.0 + 0.66 * d * rise * fast::sinCycles (phase));
                    const double clock = 512.0 / lastDelay;
                    out = ch.post.process (ch.bbd.process (bbdchorus::railClip (ch.pre.process (x), -3.5, 3.5), clock));
                    break;
                }
                case VibratoModel::cloneTheory:
                {
                    lastDelay = 6.0e-3 * (1.0 + 0.6 * d * triangle (phase));
                    const double clock = 512.0 / lastDelay;
                    // IC1A: x3.9 inverting gain ahead of the line; the Vibraflange switch closes R21/R22 back to its input.
                    const double in = 3.9 * x + (vibraflange ? 0.55 * ch.lastWet : 0.0);
                    const double wet = ch.bbd.process (bbdchorus::railClip (in, -6.5, 6.5), clock) / 3.9;
                    ch.lastWet = wet * 3.9;
                    // Chorus-Vibrato pot: dry alone at one end, the wet alone at the other (vibrato), both at its centre.
                    out = std::min (1.0, 2.0 * (1.0 - ctBlend)) * x + std::min (1.0, 2.0 * ctBlend) * wet;
                    break;
                }
                case VibratoModel::magnatone:
                {
                    // Varistors (a bridge of them, biased by the LFO) are the R of a two-section RC phase network on the
                    // 680p of V3A's plate circuit: the phase -- and so the pitch -- swings; a little amplitude follows
                    // because the network is not a perfect all-pass. The stereo 280 swings its two sides in opposition.
                    const double lfo = fast::sinCycles (phase + (c == 1 ? 0.5 : 0.0));
                    const double varistor = 240.0e3 * std::pow (2.6, -d * lfo); // ~92K .. ~630K at full Intensity
                    double y = x;
                    for (int k = 0; k < 2; ++k)
                        y = ch.phase[(size_t) k].process (y, varistor, 680.0e-12, sampleRate);
                    out = y * (1.0 - 0.06 * d * lfo);
                    break;
                }
                case VibratoModel::hammondScanner:
                {
                    // The capacitive scanner reads the LC line's taps in turn and back: a triangle of delay, depth set by
                    // how much of the line V1 / V2 / V3 cover. C modes add the undelayed signal.
                    ch.line.push ((float) x);
                    const double depthFrac = hammondDepth[hammondMode % 3];
                    lastDelay = 0.05e-3 + 0.5 * hammondLineSeconds * depthFrac * (1.0 + triangle (phase));
                    const double wet = ch.lineLoss.process (ch.line.read (lastDelay * sampleRate));
                    out = hammondMode >= 3 ? 0.5 * (x + wet) : wet;
                    break;
                }
            }
            data[i] = (float) out;
        }
    }

    if (! stereoMagnatone)
        shortcut.end (buffer);
    for (int c = 2; c < numChannels; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void VibratoPedalProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::vibrato_svg, IconData::vibrato_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
