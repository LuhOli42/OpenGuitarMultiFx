#include "AlgorithmicFlangerProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double tzfCentreMs = 4.0;            // the reference tape's lag; the moving tape swings 0 .. 2x this
    constexpr double barberMinMs = 0.3, barberMaxMs = 8.0;
    constexpr int barberTaps = 4;
    constexpr double instantMinMs = 0.25, instantMaxMs = 12.0;
    constexpr double envMinMs = 0.3, envMaxMs = 9.0;

    double rateHz (double knob, double lo, double hi) { return lo * std::pow (hi / lo, knob); }

    // Mix is a crossfade from the input to the effect (docs/circuits/MixLaw.md); the effect is the input plus its
    // delayed copy, which on average has twice the input's energy -- 1/sqrt(2) brings it back to the input's level.
    constexpr double flangeGain = 0.70710678118654752;
}

AlgorithmicFlangerProcessor::AlgorithmicFlangerProcessor (AlgorithmicFlanger k) : kind (k)
{
    switch (kind)
    {
        case AlgorithmicFlanger::throughZero:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("tzflanger", "Through-Zero Flanger", "|");
            rate = add ("tzflanger_rate", "Rate", 0.4f);
            depth = add ("tzflanger_depth", "Depth", 0.7f);
            feedback = add ("tzflanger_feedback", "Feedback", 0.3f);
            mix = add ("tzflanger_mix", "Mix", 1.0f);
            break;
        case AlgorithmicFlanger::barberpole:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("barberpole", "Barberpole Flanger", "|");
            rate = add ("barberpole_rate", "Rate", 0.4f);
            depth = add ("barberpole_range", "Range", 0.7f);
            feedback = add ("barberpole_feedback", "Feedback", 0.5f);
            mix = add ("barberpole_mix", "Mix", 0.5f);
            direction = add ("barberpole_direction", "Direction", 0.0f, 2, { "Up", "Down" });
            break;
        case AlgorithmicFlanger::instant:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("instantflanger", "Instant Flanger", "|");
            manual = add ("instantflanger_manual", "Manual", 0.5f);
            rate = add ("instantflanger_rate", "Rate", 0.4f);
            depth = add ("instantflanger_sweep", "Sweep", 0.5f);
            envAmount = add ("instantflanger_envelope", "Envelope", 0.0f);
            feedback = add ("instantflanger_feedback", "Feedback", 0.5f);
            mix = add ("instantflanger_mix", "Mix", 0.5f);
            break;
        case AlgorithmicFlanger::envelope:
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("envflanger", "Envelope Flanger", "|");
            sensitivity = add ("envflanger_sensitivity", "Sensitivity", 0.5f);
            depth = add ("envflanger_range", "Range", 0.7f);
            decay = add ("envflanger_decay", "Decay", 0.5f);
            feedback = add ("envflanger_feedback", "Feedback", 0.5f);
            mix = add ("envflanger_mix", "Mix", 0.5f);
            direction = add ("envflanger_direction", "Direction", 0.0f, 2, { "Up", "Down" });
            break;
    }
}

const char* AlgorithmicFlangerProcessor::getName() const
{
    switch (kind)
    {
        case AlgorithmicFlanger::throughZero: return "Through-Zero Flanger";
        case AlgorithmicFlanger::barberpole: return "Barberpole Flanger";
        case AlgorithmicFlanger::instant: return "Instant Flanger";
        case AlgorithmicFlanger::envelope: return "Envelope Flanger";
    }
    return "Flanger";
}

juce::AudioParameterFloat* AlgorithmicFlangerProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
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

void AlgorithmicFlangerProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        ch.reference.prepare (newSampleRate, 2.0 * tzfCentreMs * 1.0e-3 + 0.002);
        ch.modulated.prepare (newSampleRate, 0.025);
        ch.lowpass.prepare (newSampleRate, 12000.0);
    }
    for (auto* s : { &smoothedDepth, &smoothedManual, &smoothedFeedback, &smoothedMix })
        s->reset (newSampleRate, 0.05);
    smoothedDepth.setCurrentAndTargetValue (depth != nullptr ? depth->get() : 0.5f);
    smoothedManual.setCurrentAndTargetValue (manual != nullptr ? manual->get() : 0.5f);
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.setCurrentAndTargetValue (mix->get());
    reset();
}

void AlgorithmicFlangerProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.reference.reset();
        ch.modulated.reset();
        ch.lowpass.reset();
        ch.lastOut = ch.envelope = 0.0;
    }
    phase = 0.0;
}

void AlgorithmicFlangerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const double msToSamples = 1.0e-3 * sampleRate;

    if (depth != nullptr) smoothedDepth.setTargetValue (depth->get());
    if (manual != nullptr) smoothedManual.setTargetValue (manual->get());
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());

    const bool down = direction != nullptr && direction->get() >= 0.5f;
    double inc = 0.0;
    if (rate != nullptr)
        inc = (kind == AlgorithmicFlanger::barberpole ? rateHz (rate->get(), 0.02, 1.0) : rateHz (rate->get(), 0.03, 6.0)) / sampleRate;

    // Envelope follower: ~2 ms attack; release from Decay (Envelope Flanger) or a fixed 150 ms (Instant Flanger).
    const double attack = 1.0 - std::exp (-1.0 / (0.002 * sampleRate));
    const double releaseSec = decay != nullptr ? 0.03 * std::pow (30.0, (double) decay->get()) : 0.15;
    const double release = 1.0 - std::exp (-1.0 / (releaseSec * sampleRate));
    const double sens = sensitivity != nullptr ? std::pow (10.0, 2.0 * sensitivity->get()) : 10.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const double d = smoothedDepth.getNextValue(), man = smoothedManual.getNextValue();
        const double fb = 0.95 * smoothedFeedback.getNextValue(), m = smoothedMix.getNextValue();
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];
            const double rect = std::abs (x) * sens;
            ch.envelope += (rect > ch.envelope ? attack : release) * (rect - ch.envelope);
            const double env = std::min (1.0, ch.envelope);

            double out = x;
            switch (kind)
            {
                case AlgorithmicFlanger::throughZero:
                {
                    // Reference tape: fixed lag. Moving tape: 0 .. 2x the lag, so the two cross -- at the crossing the
                    // comb's first notch runs off to infinity and the sound "goes through zero", which no BBD can do.
                    ch.reference.push ((float) x);
                    ch.modulated.push ((float) (x + fb * ch.lastOut));
                    const double ref = ch.reference.read (tzfCentreMs * msToSamples);
                    const double movingMs = tzfCentreMs * (1.0 + d * fast::sinCycles (phase));
                    const double mov = ch.modulated.read (std::max (1.0, movingMs * msToSamples));
                    // Mix 0 = the reference tape alone (no effect), 1 = both tapes equally (the full flange).
                    ch.lastOut = mov;
                    out = (1.0 - m) * ref + m * flangeGain * (ref + mov);
                    break;
                }
                case AlgorithmicFlanger::barberpole:
                {
                    ch.modulated.push ((float) (x + fb * ch.lastOut));
                    const double span = (barberMaxMs - barberMinMs) * (0.2 + 0.8 * d);
                    double wet = 0.0, coherent = 0.0;
                    for (int t = 0; t < barberTaps; ++t)
                    {
                        double p = phase + (double) t / barberTaps;
                        if (p >= 1.0)
                            p -= 1.0;
                        // Rising pitch = shrinking delay; each ramp fades in and out with a sin window. The output sums
                        // the taps with sin weights (they are nearly uncorrelated, so they add in energy: constant
                        // level); the feedback sums them with sin^2 weights, whose total is exactly 1 for a coherent
                        // signal -- so the loop gain never exceeds the Feedback knob.
                        const double pos = down ? p : 1.0 - p;
                        const double w = fast::sinCycles (0.5 * p);
                        const double tap = ch.modulated.read ((barberMinMs + span * pos) * msToSamples);
                        wet += w * tap;
                        coherent += w * w * tap;
                    }
                    wet *= std::sqrt (2.0 / barberTaps);
                    coherent *= 2.0 / barberTaps;
                    ch.lastOut = coherent;
                    // The ramps' combs cancel more than one flanger's does: +3.8 dB, calibrated on MixLawTests' plucked
                    // notes, puts Mix 1 at the input's level (the WetLevelMatcher's per-effect calibration, done once).
                    out = (1.0 - m) * x + m * flangeGain * 1.55 * (x + wet);
                    break;
                }
                case AlgorithmicFlanger::instant:
                {
                    ch.modulated.push ((float) (x + fb * ch.lastOut));
                    const double amount = envAmount != nullptr ? envAmount->get() : 0.0;
                    const double u = juce::jlimit (0.0, 1.0, man + 0.5 * d * fast::sinCycles (phase) + amount * env);
                    const double delayMs = instantMaxMs * std::pow (instantMinMs / instantMaxMs, u);
                    const double wet = ch.lowpass.process (ch.modulated.read (delayMs * msToSamples));
                    ch.lastOut = wet;
                    out = (1.0 - m) * x + m * flangeGain * (x + wet);
                    break;
                }
                case AlgorithmicFlanger::envelope:
                {
                    ch.modulated.push ((float) (x + fb * ch.lastOut));
                    const double u = juce::jlimit (0.0, 1.0, (down ? 1.0 - env : env) * (0.3 + 0.7 * d) + (down ? 0.7 * d : 0.0));
                    const double delayMs = envMaxMs * std::pow (envMinMs / envMaxMs, u);
                    const double wet = ch.modulated.read (delayMs * msToSamples);
                    ch.lastOut = wet;
                    out = (1.0 - m) * x + m * flangeGain * (x + wet);
                    break;
                }
            }
            data[i] = (float) out;
        }
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void AlgorithmicFlangerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::flanger_svg, IconData::flanger_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
