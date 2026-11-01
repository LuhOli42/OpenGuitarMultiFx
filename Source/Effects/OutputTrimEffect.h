#pragma once

#include "EffectProcessor.h"
#include "TubeAmpCommon.h"

#include <cmath>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Multiplies another EffectProcessor's output by a fixed gain.

    Each circuit-modelled pedal has its own natural loudness: measured with the same guitar-like signal and every
    knob at noon, the Klon and the BD-2 came out ~12 dB louder than bypass while the OD-1 and the Tube Screamers
    were ~8 dB quieter. Real pedals are like that (each has a different Level range), but in a chain feeding an
    amp model the pedals must be swappable at "unity" without the amp being driven 20 dB harder by one than by
    another. So the registry wraps each pedal in the trim that makes its noon-everything setting unity for a
    reference signal (see EffectRegistry.cpp; `PedalUnityLevelTests` keeps it honest). The Level knob keeps its
    whole natural range around that point.
*/
class OutputTrimEffect : public EffectProcessor
{
public:
    // `limit` is opt-in and reserved for amp models: a pedal's Level knob is allowed to drive the
    // next effect several volts hot (that's what boosters are for), but an amp emitting over full
    // scale just hard-clips in the host -- real amps cannot swing past their rails anyway.
    // Amps (limit = true) also get the Smooth knob, see SmoothShelf below.
    OutputTrimEffect (std::unique_ptr<EffectProcessor> innerProcessor, float trimDecibels, bool limit = false)
        : inner (std::move (innerProcessor)), gain (juce::Decibels::decibelsToGain (trimDecibels)),
          limitOutput (limit)
    {
        if (limitOutput)
            smoothParam = SmoothShelf::addParameter (inner->getParameters());
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        inner->prepare (sampleRate, maxBlockSize, numChannels);
        smooth.prepare (sampleRate, numChannels);
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        inner->process (buffer);
        if (smoothParam != nullptr)
            smooth.process (buffer, smoothParam->get());
        buffer.applyGain (gain);
        if (limitOutput)
        {
            // The trim can push a hot emit over full scale -- a model must not hand the host an
            // unbounded float that hard-clips downstream, so the post-trim level saturates once,
            // at the shared ceiling (emits stay unceiled so nothing gets limited twice).
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* s = buffer.getWritePointer (ch);
                const int n = buffer.getNumSamples();
                for (int i = 0; i < n; ++i)
                    s[i] = (float) tubeamp::outputCeiling (s[i]);
            }
        }
    }

    void reset() override
    {
        inner->reset();
        smooth.reset();
    }

    juce::AudioProcessorParameterGroup* getParameters() override { return inner->getParameters(); }
    bool isParameterActive (const juce::AudioParameterFloat& p) const override { return inner->isParameterActive (p); }
    bool prefersSteppedKnob (const juce::AudioParameterFloat& p) const override { return inner->prefersSteppedKnob (p); }
    std::unique_ptr<juce::XmlElement> getState() const override { return inner->getState(); }
    void setState (const juce::XmlElement& state) override { inner->setState (state); }
    const char* getName() const override { return inner->getName(); }
    juce::String getStatusText() const override { return inner->getStatusText(); }
    juce::Colour getAccentColour() const override { return inner->getAccentColour(); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override { inner->drawIcon (g, b); }

    EffectProcessor& getInner() noexcept { return *inner; }

    /** The amp's "Smooth" knob: a gentle high shelf (5 kHz, slope 0.7, 0 to -6 dB) after the model. It attenuates the
        Eco/Normal fizz together with the real treble above it; the fizz is mixed with the real harmonics in the same band,
        so no fixed EQ can remove one without the other, and the default (0) leaves the amp exactly as it is: the filter is
        not even run. */
    class SmoothShelf
    {
    public:
        static constexpr double cornerHz = 5000.0, slope = 0.7, maxCutDb = -6.0;
        static constexpr int controlInterval = 16;
        static constexpr int tailSamples = 256; // keeps running at 0 dB until the shelf's state has decayed to nothing

        static juce::AudioParameterFloat* addParameter (juce::AudioProcessorParameterGroup* group)
        {
            if (group == nullptr)
                return nullptr;
            auto param = std::make_unique<juce::AudioParameterFloat> (
                "smooth", "Smooth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                {
                    return v <= 0.0f ? juce::String ("Off") : juce::String (maxCutDb * (double) v, 1) + " dB";
                })
                .withValueFromStringFunction ([] (const juce::String& text)
                {
                    const double db = std::abs (text.trim().getDoubleValue()); // "-3", "3 dB" and "Off" (0) all work
                    return (float) juce::jlimit (0.0, 1.0, db / -maxCutDb);
                }));
            auto* raw = param.get();
            // the amps' secondary controls live on their last sub-group ("page 2"); an amp without one gets it on page 1
            juce::AudioProcessorParameterGroup* target = group;
            for (auto* node : *group)
                if (auto* sub = node->getGroup())
                    target = const_cast<juce::AudioProcessorParameterGroup*> (sub);
            target->addChild (std::move (param));
            return raw;
        }

        void prepare (double newSampleRate, int numChannels)
        {
            sampleRate = newSampleRate;
            state.assign ((size_t) juce::jmax (1, numChannels), {});
            reset();
        }

        void reset() noexcept
        {
            for (auto& s : state)
                s = {};
            current = -1.0;
            counter = 0;
            idleSamples = tailSamples;
        }

        void process (juce::AudioBuffer<float>& buffer, float target) noexcept
        {
            if (sampleRate <= 0.0)
                return;
            if (current < 0.0) // first block after prepare/reset: no ramp from silence
                current = (double) target;
            if (juce::exactlyEqual (current, 0.0) && target <= 0.0f && idleSamples >= tailSamples)
            {
                if (active)
                {
                    for (auto& s : state)
                        s = {};
                    active = false;
                }
                return;
            }
            active = true;
            const int numChannels = juce::jmin (buffer.getNumChannels(), (int) state.size());
            const int numSamples = buffer.getNumSamples();
            const double step = (double) controlInterval / juce::jmax (1.0, EffectProcessor::paramRampSeconds * sampleRate);
            for (int i = 0; i < numSamples; ++i)
            {
                if (counter-- <= 0)
                {
                    counter = controlInterval - 1;
                    const double t = (double) target;
                    current = current < t ? juce::jmin (t, current + step) : juce::jmax (t, current - step);
                    design (maxCutDb * current);
                    idleSamples = juce::exactlyEqual (current, 0.0) ? idleSamples + controlInterval : 0;
                }
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& s = state[(size_t) ch];
                    auto* data = buffer.getWritePointer (ch);
                    const double x = (double) data[i];
                    const double y = b0 * x + s.z1;
                    s.z1 = b1 * x - a1 * y + s.z2;
                    s.z2 = b2 * x - a2 * y;
                    data[i] = (float) y;
                }
            }
        }

    private:
        // RBJ cookbook high shelf, normalised by a0; transposed direct form II.
        void design (double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = juce::MathConstants<double>::twoPi * cornerHz / sampleRate;
            const double cw = std::cos (w), sqA = std::sqrt (A);
            const double alpha = std::sin (w) / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
            const double a0 = (A + 1.0) - (A - 1.0) * cw + 2.0 * sqA * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * sqA * alpha) / a0;
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
            b2 = A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * sqA * alpha) / a0;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
            a2 = ((A + 1.0) - (A - 1.0) * cw - 2.0 * sqA * alpha) / a0;
        }

        struct State { double z1 = 0.0, z2 = 0.0; };
        std::vector<State> state;
        double sampleRate = 0.0, current = -1.0;
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        int counter = 0, idleSamples = 0;
        bool active = false;
    };

private:
    std::unique_ptr<EffectProcessor> inner;
    float gain;
    bool limitOutput = false;
    juce::AudioParameterFloat* smoothParam = nullptr;
    SmoothShelf smooth;
};

} // namespace openguitarmultifx
