#include "GuitarSynthProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;
    double expRange (double knob, double lo, double hi) { return lo * std::pow (hi / lo, std::clamp (knob, 0.0, 1.0)); }
    Rational bandpass (double hz, double q)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * hz;
        return { { 0.0, w0 / q }, { w0 * w0, w0 / q, 1.0 } };
    }
}

GuitarSynthProcessor::GuitarSynthProcessor (GuitarSynthModel m) : model (m)
{
    juce::String p, name;
    switch (model)
    {
        case GuitarSynthModel::microSynth: p = "microsynth"; name = "Micro Synth"; break;
        case GuitarSynthModel::bassMicroSynth: p = "bassmicrosynth"; name = "Bass Micro Synth"; break;
        case GuitarSynthModel::guitarSynth: p = "guitarsynth"; name = "Guitar Synth"; break;
        case GuitarSynthModel::brassmaster: p = "brassmaster"; name = "Brassmaster"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (p, name, "|");
    if (model == GuitarSynthModel::brassmaster)
    {
        guitar = add (p + "_bass", "Bass Volume", 0.5f);
        sensitivity = add (p + "_sensitivity", "Sensitivity", 0.6f);
        octUp = add (p + "_brass", "Brass Volume", 0.6f);
        harmonic = add (p + "_harmonic", "Harmonic", 0.0f, 2, { "Hi", "Low" });
        brass = add (p + "_voice", "Brass", 0.0f, 2, { "Brass 1", "Brass 2" });
        return;
    }
    const bool bassVoiced = model == GuitarSynthModel::bassMicroSynth;
    guitar = add (p + "_guitar", bassVoiced ? "Bass" : "Guitar", 0.4f);
    octUp = add (p + "_octave", "Octave Up", 0.0f);
    sub = add (p + "_sub", "Sub Octave", 0.6f);
    square = add (p + "_square", "Square Wave", 0.4f);
    if (model == GuitarSynthModel::guitarSynth)
        sub2 = add (p + "_sub2", "2 Oct Down", 0.0f);
    resonance = add (p + "_resonance", "Resonance", 0.5f);
    start = add (p + "_start", "Start Freq", 0.8f);
    stop = add (p + "_stop", "Stop Freq", 0.2f);
    rate = add (p + "_rate", "Rate", 0.4f);
    if (model == GuitarSynthModel::guitarSynth)
        decay = add (p + "_decay", "Decay", 0.5f);
    attack = add (p + "_attack", "Attack Delay", 0.0f);
    trigger = add (p + "_trigger", "Trigger", 0.5f);
}

const char* GuitarSynthProcessor::getName() const
{
    switch (model)
    {
        case GuitarSynthModel::microSynth: return "Micro Synth";
        case GuitarSynthModel::bassMicroSynth: return "Bass Micro Synth";
        case GuitarSynthModel::guitarSynth: return "Guitar Synth";
        case GuitarSynthModel::brassmaster: return "Brassmaster";
    }
    return "Synth";
}

juce::AudioParameterFloat* GuitarSynthProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
                                                      juce::StringArray labels)
{
    std::unique_ptr<juce::AudioParameterFloat> prm;
    if (positions >= 2)
        prm = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, (float) (positions - 1), 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([labels] (float v, int)
            { return labels[juce::jlimit (0, labels.size() - 1, juce::roundToInt (v))]; }));
    else
        prm = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    auto* raw = prm.get();
    parameters->addChild (std::move (prm));
    return raw;
}

void GuitarSynthProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    const bool bassVoiced = model == GuitarSynthModel::bassMicroSynth;
    for (auto& ch : channels)
    {
        ch.inputHp.prepare (couplingHighpass (0.47e-6, 68.0e3), newSampleRate, 0.0);
        // The Micro Synth's "700 Hz low pass filter" (3.3K / 3.3K / 2.2K, 0.022 / 0.0022 / 0.1 u) feeds the rectifier and
        // the trackers; the bass version's sits lower.
        ch.analyser.prepare (newSampleRate, bassVoiced ? 300.0 : 700.0, 3, 0.3, 0.12);
        ch.octDc.prepare (couplingHighpass (0.47e-6, 10.0e3), newSampleRate, 0.0); // C4 0.47u: strips the rectifier's DC
        // Brassmaster's Brass 1 / Brass 2 networks: two RC band-passes around Q5 (the 6.8K / 47K / .005 / .01 ladders).
        ch.brassA.prepare (bandpass (1100.0, 1.4), newSampleRate, 1100.0);
        ch.brassB.prepare (bandpass (700.0, 2.2), newSampleRate, 700.0);
        ch.vcf.reset();
        ch.ff.reset();
        ch.env = ch.lastLevel = ch.holdOff = 0.0;
        ch.sweep = 1.0;
        ch.vca = 1.0;
        ch.gate = false;
    }
    envA = 1.0 - std::exp (-1.0 / (0.008 * newSampleRate));
    shortcut.reset();
}

void GuitarSynthProcessor::reset()
{
    if (sampleRate > 0.0)
        prepare (sampleRate, 0, 0);
}

void GuitarSynthProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;
    const int numSamples = buffer.getNumSamples();
    const int solve = shortcut.begin (channels, buffer, sampleRate);

    if (model == GuitarSynthModel::brassmaster)
    {
        const double sens = 2.0 + 60.0 * sensitivity->get();
        const double dryGain = 2.0 * guitar->get(), brassGain = 2.0 * octUp->get();
        const bool low = harmonic->get() >= 0.5f, voiceB = brass->get() >= 0.5f;
        for (int i = 0; i < numSamples; ++i)
            for (int c = 0; c < solve; ++c)
            {
                auto& ch = channels[(size_t) c];
                auto* data = buffer.getWritePointer (c);
                const double x = ch.inputHp.process (data[i]);
                // Q2 / Q3: gain (Sensitivity) into the phase splitter, clipped by the stages' supply; the transformer and the
                // 1N4001 bridge rectify both halves (octave up, Hi) -- or, Low, the clipped signal passes un-rectified.
                // The stage saturates between Vce(sat) and the supply: linear, then a short knee into each limit.
                const double v = sens * x, knee = 0.25;
                const double driven = v > 1.0 - knee ? 1.0 - knee + knee * (v - 1.0 + knee) / (knee + (v - 1.0 + knee))
                                      : (v < -1.0 + knee ? -1.0 + knee - knee * (-1.0 + knee - v) / (knee + (-1.0 + knee - v)) : v);
                const double rectified = std::max (0.0, std::abs (driven) - 0.05) * 1.6; // two bridge drops (~0.6 V each, scaled)
                double brassSig = low ? driven * 0.7 : ch.octDc.process (rectified);
                brassSig = voiceB ? ch.brassB.process (brassSig) * 1.8 : ch.brassA.process (brassSig) * 1.5;
                data[i] = (float) (dryGain * x + brassGain * 0.4 * brassSig);
            }
        shortcut.end (buffer);
        return;
    }

    const bool bassVoiced = model == GuitarSynthModel::bassMicroSynth;
    const double gGuitar = 2.0 * guitar->get(), gOct = 2.0 * octUp->get(), gSub = 2.0 * sub->get(), gSq = 2.0 * square->get();
    const double gSub2 = sub2 != nullptr ? 2.0 * sub2->get() : 0.0;
    const double res = resonance->get();
    const double f0 = expRange (start->get(), bassVoiced ? 60.0 : 100.0, bassVoiced ? 3000.0 : 6000.0);
    const double f1 = expRange (stop->get(), bassVoiced ? 60.0 : 100.0, bassVoiced ? 3000.0 : 6000.0);
    const double sweepCoeff = 1.0 - std::exp (-1.0 / (expRange (1.0 - rate->get(), 0.02, 3.0) * sampleRate));
    const double attackCoeff = 1.0 - std::exp (-1.0 / (expRange (attack->get(), 0.001, 1.5) * sampleRate));
    const double decayCoeff = decay != nullptr ? 1.0 - std::exp (-1.0 / (expRange (decay->get(), 0.1, 4.0) * sampleRate)) : 0.0;
    const double threshold = 0.004 * std::pow (20.0, 1.0 - trigger->get()); // Trigger cw = more sensitive

    for (int i = 0; i < numSamples; ++i)
        for (int c = 0; c < solve; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = ch.inputHp.process (data[i]) * 3.0; // A2A preamp
            if (ch.analyser.process (x))
                ch.ff.clock();
            const double f = ch.analyser.lastFiltered();
            const double level = ch.analyser.level();
            ch.env += envA * (std::abs (f) - ch.env);

            // Attack detector: the level jumping well above where it was = a new note -> restart the sweep and the VCA.
            ch.holdOff = std::max (0.0, ch.holdOff - 1.0);
            if (level > threshold && level > 1.6 * ch.lastLevel + threshold && ch.holdOff <= 0.0)
            {
                ch.sweep = 0.0;
                ch.vca = 0.0;
                ch.gate = true;
                ch.holdOff = 0.05 * sampleRate;
            }
            ch.lastLevel += 0.002 * (level - ch.lastLevel);
            if (level < 0.5 * threshold)
                ch.gate = false; // stop detector: squelch at the end of the note
            ch.sweep += sweepCoeff * (1.0 - ch.sweep);
            ch.vca += attackCoeff * ((ch.gate ? 1.0 : 0.0) - ch.vca);

            // Voices.
            const double octave = ch.octDc.process (std::abs (f)) * 1.5;               // full-wave rectifier
            const double subVoice = (ch.ff.q[0] ? 1.0 : -1.0) * f * 1.3;              // track-and-hold modulator
            const double sub2Voice = (ch.ff.q[1] ? 1.0 : -1.0) * f * 1.3;
            const double squareVoice = (ch.analyser.isHigh() ? 1.0 : -1.0) * ch.env * 1.2; // 2N5087 square modulator
            const double mix = gGuitar * x * 0.35 + gOct * octave + gSub * subVoice + gSub2 * sub2Voice + gSq * squareVoice;

            // VCF swept from Start to Stop on each attack (exponential), with the Guitar Synth's filter decay on top.
            double cutoff = f0 * std::pow (f1 / f0, ch.sweep);
            if (decayCoeff > 0.0)
                cutoff *= 0.3 + 0.7 * std::exp (-ch.sweep * 3.0);
            const double y = ch.vcf.process (mix, cutoff, res, sampleRate);
            data[i] = (float) (y * ch.vca);
        }
    shortcut.end (buffer);
}

void GuitarSynthProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::octaver_svg, IconData::octaver_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
