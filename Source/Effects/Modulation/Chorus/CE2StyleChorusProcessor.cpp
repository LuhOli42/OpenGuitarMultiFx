#include "CE2StyleChorusProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    constexpr double bias = 4.5;            // R27/R28/VR3 divider, trimmed to half the 9 V supply
    constexpr double squareSwing = 3.5;     // TL022 output on a 9 V single supply: ~1 V .. 8 V about the bias
    constexpr double opAmpSwing = 3.5;      // uPC4558 on 9 V, about the bias
    constexpr double vbe = 0.6, diodeDrop = 0.6;

    // LFO (IC2, TL022)
    constexpr double r29 = 33.0e3, r30 = 47.0e3, r31 = 10.0e3, r32 = 1.0e6, c19 = 0.1e-6, vr1 = 100.0e3;
    constexpr double vr2 = 100.0e3, r35 = 220.0e3, c21 = 0.01e-6;
    constexpr double q4InputOhms = 300.0 * (4.7e3 + 4.7e3); // beta * (R36 + R37)
    // MN3101 clock
    constexpr double r38 = 150.0e3, c22 = 47.0e-12, clockSupply = 9.0, clockThreshold = 4.5, dischargeSeconds = 0.3e-6;

    constexpr int bbdStages = 1024; // MN3007

    std::vector<LadderNode> sallenKey (double ra, double rb, double rc)
    {
        // C6/C11 3.3n to ground, C7/C12 8.2n to the follower's emitter, C8/C13 470p to ground.
        return { { ra, 0.0, 3.3e-9, 0.0, 0.0 }, { rb, 0.0, 0.0, 8.2e-9, 0.0 }, { rc, 0.0, 470.0e-12, 0.0, 0.0 } };
    }
}

CE2StyleChorusProcessor::CE2StyleChorusProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> ("ce2_rate", "Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto depth = std::make_unique<juce::AudioParameterFloat> ("ce2_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    rateParam = rate.get();
    depthParam = depth.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ce2", "CE-2", "|", std::move (rate), std::move (depth));
}

void CE2StyleChorusProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    // Q1 input: C1 into ~407K (R1 + R2 || beta R3), then C2 into the pre-emphasis input (R4 at low frequencies).
    const auto inputCoupling = cascade (couplingHighpass (0.047e-6, 407.0e3), couplingHighpass (0.47e-6, 47.0e3));
    // IC1a: -(R6 || C4) / (R4 || (R5 + 1/sC3))
    const auto preEmphasis = inverting (parallelRC (47.0e3, 100.0e-12), parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9)));
    // IC1b: feedback R24 || (R23 + 1/sC15) || C16; dry in through R21, wet through C14 + R22.
    const auto zf = parallel (parallel (resistor (47.0e3), seriesRC (10.0e3, 6.8e-9)), capacitor (100.0e-12));
    const auto mixDry = inverting (zf, resistor (47.0e3));
    const auto mixWet = inverting (zf, seriesRC (47.0e3, 0.033e-6));

    const auto inFilter = bbdchorus::bbdFilter (sallenKey (10.0e3, 10.0e3, 10.0e3));   // R9-R11
    const auto outFilter = bbdchorus::bbdFilter (sallenKey (10.0e3, 10.0e3, 10.0e3));  // R16-R18

    for (auto& ch : channels)
    {
        ch.inputCoupling.prepare (inputCoupling, newSampleRate, 0.0);
        ch.preEmphasis.prepare (preEmphasis, newSampleRate, 2340.0);
        ch.wetCoupling.prepare (couplingHighpass (0.033e-6, 100.0e3), newSampleRate, 0.0); // C5 into R8
        ch.bbdOutCoupling.prepare (couplingHighpass (0.033e-6, 330.0e3), newSampleRate, 0.0); // C10 into R15
        ch.mixDry.prepare (mixDry, newSampleRate, 2340.0);
        ch.mixWet.prepare (mixWet, newSampleRate, 2340.0);
        ch.bbd.prepare (newSampleRate, bbdStages, inFilter, outFilter);
    }

    smoothedRate.reset (newSampleRate, paramRampSeconds);
    smoothedRate.setCurrentAndTargetValue (rateParam->get());
    smoothedDepth.reset (newSampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depthParam->get());

    const double threshold = squareSwing * r29 / r30;
    lfo.setThresholds (-threshold, threshold);
    updateControls (rateParam->get(), depthParam->get());
    reset();
}

void CE2StyleChorusProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.inputCoupling.reset();
        ch.preEmphasis.reset();
        ch.wetCoupling.reset();
        ch.bbdOutCoupling.reset();
        ch.mixDry.reset();
        ch.mixWet.reset();
        ch.bbd.reset();
    }
    lfo.reset (0.0, true);
    lfoSmoothing.reset (smoothingGain * bias);
    shortcut.reset();
}

void CE2StyleChorusProcessor::updateControls (double rate, double depth) noexcept
{
    // Rate: VR1 divides the square (about the bias) with R31 below its track; R32 into C19 integrates it.
    const auto rateDiv = bbdchorus::Divider::of (rate, vr1, 0.0, r31);
    const double slope = rateDiv.fraction * squareSwing / ((r32 + rateDiv.sourceOhms) * c19);
    lfo.setSlopes (slope, slope);

    // Depth: VR2 between the triangle and the bias; R35 + C21 into Q4's base (which loads it).
    const auto depthDiv = bbdchorus::Divider::of (depth, vr2);
    depthFraction = depthDiv.fraction;
    const double seriesOhms = r35 + depthDiv.sourceOhms;
    smoothingGain = q4InputOhms / (q4InputOhms + seriesOhms);
    if (sampleRate > 0.0)
        lfoSmoothing.setTimeConstant (sampleRate, c21 * seriesOhms * q4InputOhms / (seriesOhms + q4InputOhms));
}

double CE2StyleChorusProcessor::advanceModulation() noexcept
{
    const double tri = lfo.advance (dt);
    const double base = lfoSmoothing.process (smoothingGain * (bias + depthFraction * tri));
    // Q4 emitter, halved by R36/R37, less D1: the voltage C22 is charged back up to after each discharge.
    const double offset = std::max (0.0, 0.5 * (base - vbe) - diodeDrop);
    const double period = bbdchorus::rcChargePeriod (r38, c22, offset, clockSupply, clockThreshold, dischargeSeconds);
    lastClockHz = 0.5 / period; // the MN3101 divides by two
    return lastClockHz;
}

void CE2StyleChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedRate.setTargetValue (rateParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    const bool moving = smoothedRate.isSmoothing() || smoothedDepth.isSmoothing();

    for (int i = 0; i < numSamples; ++i)
    {
        if (moving)
            updateControls (smoothedRate.getNextValue(), smoothedDepth.getNextValue());
        const double clockHz = advanceModulation();

        for (int c = 0; c < solveChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);

            const double in = ch.inputCoupling.process ((double) data[i]);
            const double pre = bbdchorus::railClip (ch.preEmphasis.process (in), -opAmpSwing, opAmpSwing);
            const double delayed = ch.bbdOutCoupling.process (ch.bbd.process (ch.wetCoupling.process (pre), clockHz));
            const double out = ch.mixDry.process (pre) + ch.mixWet.process (delayed);
            data[i] = (float) bbdchorus::railClip (out, -opAmpSwing, opAmpSwing);
        }
    }

    shortcut.end (buffer);
}

void CE2StyleChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
