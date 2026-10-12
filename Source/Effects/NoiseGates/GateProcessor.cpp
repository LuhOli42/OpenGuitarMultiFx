#include "GateProcessor.h"
#include "../IconKit.h"

#include <IconData.h>

#include <array>

namespace openguitarmultifx
{

GateProcessor::GateProcessor()
{
    auto threshold = std::make_unique<juce::AudioParameterFloat> (
        "gate_threshold", "Threshold",
        juce::NormalisableRange<float> (-80.0f, 0.0f), -50.0f);
    auto attack = std::make_unique<juce::AudioParameterFloat> (
        "gate_attack", "Attack",
        juce::NormalisableRange<float> (0.1f, 50.0f, 0.0f, 0.4f), 1.0f);
    auto release = std::make_unique<juce::AudioParameterFloat> (
        "gate_release", "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.4f), 100.0f);
    auto hysteresis = std::make_unique<juce::AudioParameterFloat> (
        "gate_hysteresis", "Hysteresis",
        juce::NormalisableRange<float> (0.0f, 20.0f), 6.0f);
    auto hold = std::make_unique<juce::AudioParameterFloat> (
        "gate_hold", "Hold",
        juce::NormalisableRange<float> (0.0f, 500.0f, 0.0f, 0.4f), 10.0f);

    thresholdDb = threshold.get();
    attackMs = attack.get();
    releaseMs = release.get();
    hysteresisDb = hysteresis.get();
    holdMs = hold.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "gate", "Noise Gate", "|",
        std::move (threshold), std::move (attack), std::move (release),
        std::move (hysteresis), std::move (hold));
}

void GateProcessor::prepare (double sr, int, int)
{
    sampleRate = sr;
    levelDetector.prepare (sampleRate);
    levelDetector.setAttackTime (2.0f);
    levelDetector.setReleaseTime (2.0f);

    gainSmoother.prepare (sampleRate);

    smoothedThresholdDb.reset (sampleRate, paramRampSeconds);
    smoothedThresholdDb.setCurrentAndTargetValue (thresholdDb->get());
    smoothedAttackMs.reset (sampleRate, paramRampSeconds);
    smoothedAttackMs.setCurrentAndTargetValue (attackMs->get());
    smoothedReleaseMs.reset (sampleRate, paramRampSeconds);
    smoothedReleaseMs.setCurrentAndTargetValue (releaseMs->get());
    smoothedHysteresisDb.reset (sampleRate, paramRampSeconds);
    smoothedHysteresisDb.setCurrentAndTargetValue (hysteresisDb->get());
    smoothedHoldMs.reset (sampleRate, paramRampSeconds);
    smoothedHoldMs.setCurrentAndTargetValue (holdMs->get());

    reset();
}

void GateProcessor::reset()
{
    levelDetector.reset();
    gainSmoother.reset(); // gate starts closed -- silent until the first transient opens it
    gateOpen = false;
    holdSamplesLeft = 0;
}

void GateProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Detector-side parameters only steer the gate's own slewed gain, so a
    // per-block stepped ramp is enough -- the actual output multiply is
    // already per-sample inside gainSmoother.
    smoothedAttackMs.setTargetValue (attackMs->get());
    smoothedReleaseMs.setTargetValue (releaseMs->get());
    gainSmoother.setAttackTime (smoothedAttackMs.skip (numSamples));
    gainSmoother.setReleaseTime (smoothedReleaseMs.skip (numSamples));
    smoothedThresholdDb.setTargetValue (thresholdDb->get());
    const float openThreshold = juce::Decibels::decibelsToGain (smoothedThresholdDb.skip (numSamples));
    smoothedHysteresisDb.setTargetValue (hysteresisDb->get());
    const float closeThreshold = openThreshold * juce::Decibels::decibelsToGain (-smoothedHysteresisDb.skip (numSamples));
    smoothedHoldMs.setTargetValue (holdMs->get());
    const int holdSamples = (int) (smoothedHoldMs.skip (numSamples) * 0.001 * sampleRate);

    // Caching the write pointer once per channel avoids the bounds-checked
    // indirection getSample()/setSample() would otherwise do on every one of
    // these hot per-sample accesses. Capped at 2 (mono/stereo) -- same
    // per-channel-state convention AnalogDelayProcessor's filterState uses.
    std::array<float*, 2> channelData {};
    const int cachedChannels = juce::jmin (numChannels, 2);
    for (int ch = 0; ch < cachedChannels; ++ch)
        channelData[(size_t) ch] = buffer.getWritePointer (ch);

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < cachedChannels; ++ch)
            peak = juce::jmax (peak, std::abs (channelData[(size_t) ch][i]));

        const float detectedLevel = levelDetector.processSample (peak);

        // Two-threshold state machine: once open, the gate only starts its
        // hold countdown when the level drops below the (lower) close
        // threshold, so a signal wobbling around the threshold can't
        // chatter the gate. targetGain still runs through gainSmoother, so
        // the 0<->1 transitions themselves stay click-free.
        if (! gateOpen)
        {
            if (detectedLevel >= openThreshold)
            {
                gateOpen = true;
                holdSamplesLeft = holdSamples;
            }
        }
        else if (detectedLevel >= closeThreshold)
        {
            holdSamplesLeft = holdSamples;
        }
        else if (--holdSamplesLeft < 0)
        {
            gateOpen = false;
        }

        const float gateGain = gainSmoother.processSample (gateOpen ? 1.0f : 0.0f);

        for (int ch = 0; ch < cachedChannels; ++ch)
            channelData[(size_t) ch][i] *= gateGain;
    }
}

void GateProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/gate.svg -- the
    // unified icon set's "Noise Gate" glyph (Dinamica category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::gate_svg, IconData::gate_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
