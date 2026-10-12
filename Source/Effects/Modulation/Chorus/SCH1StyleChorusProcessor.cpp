#include "SCH1StyleChorusProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    constexpr double bias = 4.5, squareSwing = 3.5, opAmpSwing = 3.5;
    constexpr double vbe = 0.6, diodeDrop = 0.6;

    // LFO (IC3)
    constexpr double r42 = 1.0e6, r43 = 470.0e3, r44 = 5.6e3, r45 = 470.0e3, c32 = 0.1e-6, vr1 = 100.0e3;
    constexpr double vr2 = 100.0e3, r51 = 10.0e3, r46 = 390.0e3, c30 = 22.0e-9;
    constexpr double q7InputOhms = 300.0 * 10.0e3; // beta * R47
    // MN3102 clock: C31 47p charged through R49 160K, reset by Q8.
    constexpr double r49 = 160.0e3, c31 = 47.0e-12, clockSupply = 9.0, clockThreshold = 4.5, resetSeconds = 0.3e-6;

    constexpr int bbdStages = 1024; // MN3207

    Rational toneAt (double t)
    {
        // Nodes: 1 = IC1a out, 2 = after R29, 3 = pot H, 4 = wiper, 5 = pot L, 6 = IC1b pin 3.
        const double pot = 100.0e3, upper = std::max (100.0, (1.0 - t) * pot), lower = std::max (100.0, t * pot);
        const auto network = networkTransfer ({ { 1, 2, 1.0e3, 0.0 },
                                                { 2, 3, 0.0, 2.2e-9 },
                                                { 2, 5, 33.0e3, 0.0 },
                                                { 3, 4, upper, 0.0 },
                                                { 4, 5, lower, 0.0 },
                                                { 5, 0, 0.0, 15.0e-9 },
                                                { 4, 6, 0.0, 10.0e-9 },
                                                { 6, 0, 1.0e6, 0.0 } },
                                              7, 6);
        // IC1b, non-inverting: 1 + R58 / (R57 + 1/sC38)
        const double r57 = 33.0e3, r58 = 51.0e3, c38 = 10.0e-9;
        return cascade (network, Rational { { 1.0, c38 * (r57 + r58) }, { 1.0, c38 * r57 } });
    }
}

SCH1StyleChorusProcessor::SCH1StyleChorusProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> ("sch1_rate", "Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto depth = std::make_unique<juce::AudioParameterFloat> ("sch1_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto toneKnob = std::make_unique<juce::AudioParameterFloat> ("sch1_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    rateParam = rate.get();
    depthParam = depth.get();
    toneParam = toneKnob.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("sch1", "SCH-1", "|", std::move (rate), std::move (depth), std::move (toneKnob));
}

void SCH1StyleChorusProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    inputCoupling.prepare (cascade (couplingHighpass (1.0e-6, 470.0e3), couplingHighpass (1.0e-6, 47.0e3)), newSampleRate, 0.0);
    // IC1a: -(R9 || C6) / (R7 || (R8 + 1/sC5)) -- the CE-2's values.
    preEmphasis.prepare (inverting (parallelRC (47.0e3, 100.0e-12), parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9))), newSampleRate, 2340.0);
    wetCoupling.prepare (cascade (couplingHighpass (33.0e-9, 100.0e3), couplingHighpass (22.0e-9, 1.0e6)), newSampleRate, 0.0); // C7/R11, C15/R20

    for (int i = 0; i <= toneSteps; ++i)
        toneTable[(size_t) i].prepare (toneAt ((double) i / toneSteps), newSampleRate, 1000.0);
    tone = toneTable[(size_t) toneSteps / 2];

    // IC2: feedback (R34 + C22) || R36 || C24; dry through R30/R31 47K, wet through C20/C21 22n + R32/R33 39K.
    const auto zf = parallel (parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9)), capacitor (100.0e-12));
    mixDry.prepare (inverting (zf, resistor (47.0e3)), newSampleRate, 2340.0);
    mixWetA.prepare (inverting (zf, seriesRC (39.0e3, 22.0e-9)), newSampleRate, 2340.0);
    mixWetB.prepare (cascade (gain (-1.0), inverting (zf, seriesRC (39.0e3, 22.0e-9))), newSampleRate, 2340.0); // Q5's collector

    const std::vector<LadderNode> inSk { { 10.0e3, 0.0, 3.3e-9, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 8.2e-9, 0.0 }, { 10.0e3, 0.0, 470.0e-12, 0.0, 0.0 } };
    const std::vector<LadderNode> outSk { { 10.0e3, 0.0, 4.7e-9, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 8.2e-9, 0.0 }, { 10.0e3, 0.0, 470.0e-12, 0.0, 0.0 } };
    bbd.prepare (newSampleRate, bbdStages, bbdchorus::bbdFilter (inSk), bbdchorus::bbdFilter (outSk));

    smoothedRate.reset (newSampleRate, paramRampSeconds);
    smoothedRate.setCurrentAndTargetValue (rateParam->get());
    smoothedDepth.reset (newSampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depthParam->get());
    smoothedTone.reset (newSampleRate, paramRampSeconds);
    smoothedTone.setCurrentAndTargetValue (toneParam->get());

    const double threshold = squareSwing * r45 / r42;
    lfo.setThresholds (-threshold, threshold);
    updateControls (rateParam->get(), depthParam->get(), toneParam->get());
    reset();
}

void SCH1StyleChorusProcessor::reset()
{
    inputCoupling.reset();
    preEmphasis.reset();
    wetCoupling.reset();
    tone.reset();
    mixDry.reset();
    mixWetA.reset();
    mixWetB.reset();
    bbd.reset();
    lfo.reset (0.0, true);
    lfoSmoothing.reset (smoothingGain * bias);
}

void SCH1StyleChorusProcessor::updateControls (double rate, double depth, double toneKnob) noexcept
{
    // Rate: VR1 divides the Schmitt square (H) against R44 to the bias (L); R43 into C32 integrates it.
    const auto rateDiv = bbdchorus::Divider::of (rate, vr1, 0.0, r44);
    const double slope = rateDiv.fraction * squareSwing / ((r43 + rateDiv.sourceOhms) * c32);
    lfo.setSlopes (slope, slope);

    // Depth: VR2 from the triangle (max) to R51 at the bias (min); R46 + C30 into Q7.
    const auto depthDiv = bbdchorus::Divider::of (depth, vr2, 0.0, r51);
    depthFraction = depthDiv.fraction;
    const double seriesOhms = r46 + depthDiv.sourceOhms;
    smoothingGain = q7InputOhms / (q7InputOhms + seriesOhms);
    if (sampleRate > 0.0)
        lfoSmoothing.setTimeConstant (sampleRate, c30 * seriesOhms * q7InputOhms / (seriesOhms + q7InputOhms));

    const double pos = std::clamp (toneKnob, 0.0, 1.0) * toneSteps;
    const int idx = std::min (toneSteps - 1, (int) pos);
    tone.interpolateFrom (toneTable[(size_t) idx], toneTable[(size_t) idx + 1], pos - idx);
}

double SCH1StyleChorusProcessor::advanceModulation() noexcept
{
    const double tri = lfo.advance (dt);
    const double base = lfoSmoothing.process (smoothingGain * (bias + depthFraction * tri));
    // Q7's emitter through D4 sets where C31 restarts after Q8 resets it; halved as in the CE-2's R36/R37 (see the doc
    // -- the SCH-1 drawing is not legible enough here to confirm that divider).
    const double offset = std::max (0.0, 0.5 * (base - vbe) - diodeDrop);
    const double period = bbdchorus::rcChargePeriod (r49, c31, offset, clockSupply, clockThreshold, resetSeconds);
    lastClockHz = 0.5 / period; // the MN3102 divides by two
    return lastClockHz;
}

void SCH1StyleChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    smoothedRate.setTargetValue (rateParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    smoothedTone.setTargetValue (toneParam->get());
    const bool moving = smoothedRate.isSmoothing() || smoothedDepth.isSmoothing() || smoothedTone.isSmoothing();

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        if (moving)
            updateControls (smoothedRate.getNextValue(), smoothedDepth.getNextValue(), smoothedTone.getNextValue());
        const double clockHz = advanceModulation();

        const double x = right != nullptr ? 0.5 * ((double) left[i] + (double) right[i]) : (double) left[i];
        const double pre = bbdchorus::railClip (preEmphasis.process (inputCoupling.process (x)), -opAmpSwing, opAmpSwing);
        const double dry = bbdchorus::railClip (tone.process (pre), -opAmpSwing, opAmpSwing);
        const double wet = bbd.process (wetCoupling.process (pre), clockHz);

        const double dryMixed = mixDry.process (dry);
        left[i] = (float) bbdchorus::railClip (dryMixed + mixWetA.process (wet), -opAmpSwing, opAmpSwing);
        if (right != nullptr)
            right[i] = (float) bbdchorus::railClip (dryMixed + mixWetB.process (wet), -opAmpSwing, opAmpSwing);
    }

    for (int c = 2; c < numChannels; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void SCH1StyleChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
