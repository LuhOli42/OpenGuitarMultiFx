#include "AlgorithmicPitchProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double windowMs = 40.0;
    // Whammy intervals (semitones), the classic pedal's modes.
    constexpr double whammyIntervals[9] { 24.0, 12.0, 7.0, 5.0, -2.0, -12.0, -24.0, 0.25, -0.25 };
    const juce::StringArray whammyNames { "+2 Oct", "+1 Oct", "+5th", "+4th", "-2nd", "-1 Oct", "-2 Oct", "Detune +", "Detune -" };
}

double AlgorithmicPitchProcessor::Shifter::process (const EnsembleDelay& line, double ratio, double w) noexcept
{
    // Delay grows at (1 - ratio) samples per sample: ratio < 1 reads ever further back (pitch down), > 1 catches up.
    phase += (1.0 - ratio) / w;
    phase -= std::floor (phase);
    double y = 0.0;
    for (int t = 0; t < 2; ++t)
    {
        double p = phase + 0.5 * t;
        if (p >= 1.0) p -= 1.0;
        const double win = fast::sinCycles (0.5 * p);
        y += win * win * line.read (2.0 + p * w);
    }
    return y;
}

AlgorithmicPitchProcessor::AlgorithmicPitchProcessor (AlgorithmicPitch k) : kind (k)
{
    juce::String p, name;
    switch (kind)
    {
        case AlgorithmicPitch::pog: p = "pog"; name = "Poly Octave"; break;
        case AlgorithmicPitch::whammy: p = "whammy"; name = "Whammy"; break;
        case AlgorithmicPitch::organ: p = "organ"; name = "Organ Machine"; break;
        case AlgorithmicPitch::monoSynth: p = "monosynth"; name = "Mono Synth"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (p, name, "|");
    switch (kind)
    {
        case AlgorithmicPitch::pog:
            knobs[0] = add (p + "_dry", "Dry", 0.7f);
            knobs[1] = add (p + "_sub2", "-2 Oct", 0.0f);
            knobs[2] = add (p + "_sub1", "-1 Oct", 0.5f);
            knobs[3] = add (p + "_up1", "+1 Oct", 0.4f);
            knobs[4] = add (p + "_up2", "+2 Oct", 0.0f);
            break;
        case AlgorithmicPitch::whammy:
            knobs[0] = add (p + "_interval", "Interval", 1.0f, 9, whammyNames);
            knobs[1] = add (p + "_position", "Position", 1.0f);
            knobs[2] = add (p + "_mix", "Mix", 1.0f);
            break;
        case AlgorithmicPitch::organ:
            knobs[0] = add (p + "_dry", "Dry", 0.3f);
            knobs[1] = add (p + "_organ", "Organ", 0.7f);
            knobs[2] = add (p + "_mod", "Mod", 0.4f);
            knobs[3] = add (p + "_click", "Click", 0.4f);
            break;
        case AlgorithmicPitch::monoSynth:
            knobs[0] = add (p + "_wave", "Wave", 0.0f, 2, { "Saw", "Square" });
            knobs[1] = add (p + "_octave", "Octave", 1.0f, 3, { "-1", "0", "+1" });
            knobs[2] = add (p + "_glide", "Glide", 0.2f);
            knobs[3] = add (p + "_cutoff", "Cutoff", 0.5f);
            knobs[4] = add (p + "_resonance", "Resonance", 0.4f);
            knobs[5] = add (p + "_env", "Env Amount", 0.5f);
            knobs[6] = add (p + "_mix", "Mix", 0.8f);
            break;
    }
}

const char* AlgorithmicPitchProcessor::getName() const
{
    switch (kind)
    {
        case AlgorithmicPitch::pog: return "Poly Octave";
        case AlgorithmicPitch::whammy: return "Whammy";
        case AlgorithmicPitch::organ: return "Organ Machine";
        case AlgorithmicPitch::monoSynth: return "Mono Synth";
    }
    return "Pitch";
}

juce::AudioParameterFloat* AlgorithmicPitchProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
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

void AlgorithmicPitchProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        // Room for the slowest voice: an octave-down window and a little headroom.
        ch.line.prepare (newSampleRate, 4.0 * windowMs * 1.0e-3 + 0.01);
        ch.analyser.prepare (newSampleRate, 600.0, 3, 0.3, 0.1);
        ch.tone.prepare (newSampleRate, 5000.0);
    }
    smoothedPosition.reset (newSampleRate, 0.03);
    smoothedPosition.setCurrentAndTargetValue (kind == AlgorithmicPitch::whammy ? knobs[1]->get() : 0.0f);
    reset();
}

void AlgorithmicPitchProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.line.reset();
        for (auto& s : ch.shifters)
            s.phase = 0.0;
        ch.analyser.reset();
        ch.vcf.reset();
        ch.tone.reset();
        ch.env = ch.oscPhase = ch.freq = ch.click = ch.lastLevel = ch.modPhase = 0.0;
    }
    trackedHz = 0.0;
}

void AlgorithmicPitchProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const double w = windowMs * 1.0e-3 * sampleRate;
    if (kind == AlgorithmicPitch::whammy)
        smoothedPosition.setTargetValue (knobs[1]->get());
    const double envA = 1.0 - std::exp (-1.0 / (0.006 * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const double position = smoothedPosition.getNextValue();
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];
            ch.line.push ((float) x);
            double out = x;

            switch (kind)
            {
                case AlgorithmicPitch::pog:
                {
                    static constexpr double ratios[4] { 0.25, 0.5, 2.0, 4.0 };
                    double sum = 2.0 * knobs[0]->get() * x;
                    for (int v = 0; v < 4; ++v)
                    {
                        const double lvl = knobs[(size_t) v + 1]->get();
                        if (lvl > 0.001)
                            sum += 2.0 * lvl * ch.shifters[(size_t) v].process (ch.line, ratios[v], v < 2 ? 2.0 * w : w);
                    }
                    out = 0.5 * sum;
                    break;
                }
                case AlgorithmicPitch::whammy:
                {
                    const double semis = whammyIntervals[juce::jlimit (0, 8, juce::roundToInt (knobs[0]->get()))] * position;
                    const double wet = ch.shifters[0].process (ch.line, std::pow (2.0, semis / 12.0), semis < 0.0 ? 2.0 * w : w);
                    const double m = knobs[2]->get();
                    out = (1.0 - m) * x + m * wet;
                    break;
                }
                case AlgorithmicPitch::organ:
                {
                    // Drawbars: 16' (sub), 8' (dry), 4' (octave), 2 2/3' (the 19th, an octave + a fifth).
                    const double drawbars = 0.8 * ch.shifters[0].process (ch.line, 0.5, 2.0 * w) + 0.6 * x
                                            + 0.7 * ch.shifters[1].process (ch.line, 2.0, w) + 0.45 * ch.shifters[2].process (ch.line, 2.9966, w);
                    // Key click: a short burst on each new attack.
                    const double level = std::abs (x);
                    ch.env += envA * (level - ch.env);
                    if (ch.env > 0.02 && ch.env > 2.0 * ch.lastLevel)
                        ch.click = 1.0;
                    ch.lastLevel += 0.001 * (ch.env - ch.lastLevel);
                    ch.click *= std::exp (-1.0 / (0.004 * sampleRate));
                    const double clickSig = knobs[3]->get() * ch.click * ch.env * 2.0 * (fast::sinCycles (ch.modPhase * 300.0) );
                    // Mod: a rotating-speaker shimmer -- a few cents of vibrato and a little amplitude motion at ~6 Hz.
                    ch.modPhase += 6.0 / sampleRate;
                    if (ch.modPhase >= 1.0) ch.modPhase -= 1.0;
                    const double mod = knobs[2]->get();
                    const double organ = ch.tone.process (drawbars) * (1.0 + 0.15 * mod * fast::sinCycles (ch.modPhase + (c == 1 ? 0.5 : 0.0)));
                    out = 2.0 * knobs[0]->get() * x + 2.0 * knobs[1]->get() * (organ + clickSig);
                    break;
                }
                case AlgorithmicPitch::monoSynth:
                {
                    ch.analyser.process (x);
                    ch.env += envA * (std::abs (x) - ch.env);
                    const double period = ch.analyser.periodSamples();
                    if (period > 1.0 && ch.analyser.level() > 0.003)
                    {
                        const int oct = juce::roundToInt (knobs[1]->get()) - 1;
                        const double target = sampleRate / period * std::pow (2.0, oct);
                        const double glide = 1.0 - std::exp (-1.0 / ((0.001 + 0.4 * knobs[2]->get()) * sampleRate));
                        ch.freq = ch.freq <= 0.0 ? target : ch.freq + glide * (target - ch.freq);
                    }
                    if (c == 0)
                        trackedHz = ch.freq;
                    ch.oscPhase += ch.freq / sampleRate;
                    ch.oscPhase -= std::floor (ch.oscPhase);
                    const double osc = knobs[0]->get() >= 0.5f ? (ch.oscPhase < 0.5 ? 1.0 : -1.0) : 2.0 * ch.oscPhase - 1.0;
                    const double cutoff = 80.0 * std::pow (100.0, (double) knobs[3]->get()) * (1.0 + 6.0 * knobs[5]->get() * std::min (1.0, 4.0 * ch.env));
                    const double synth = ch.vcf.process (osc * std::min (1.0, 3.0 * ch.env), cutoff, knobs[4]->get(), sampleRate);
                    const double m = knobs[6]->get();
                    out = (1.0 - m) * x + m * synth;
                    break;
                }
            }
            data[i] = (float) out;
        }
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void AlgorithmicPitchProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pitch_shift_svg, IconData::pitch_shift_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
