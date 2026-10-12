#include "SmallCloneStyleChorusProcessor.h"
#include "Effects/IconKit.h"
#include "Effects/PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    // Supplies: 9 V less the 1N5817 (VA, VC ~8.7 V); VB = VA/2 (R33/R34); VD = Q3's emitter, ~VC - Vbe.
    constexpr double vc = 8.7, vd = 8.1;
    constexpr double opAmpSwing = 3.3; // RC4558 on 8.7 V, about VB

    // LFO (IC3, LM358 on VC): the square swings 0 .. VC - 1.5 V.
    constexpr double squareLow = 0.02, squareHigh = vc - 1.5;
    constexpr double r25 = 180.0e3, r26 = 47.0e3, r27 = 120.0e3, r28 = 470.0e3, r29 = 68.0e3, r30 = 82.0e3, c14 = 2.2e-6, rateMax = 1.0e6;
    constexpr double integratorRef = vc * (r29 + r30) / (r25 + r29 + r30); // IC3A pin 3
    constexpr double schmittRef = vc * r30 / (r25 + r29 + r30);            // IC3B pin 6
    // CD4047 clock
    constexpr double r31 = 39.0e3, c16 = 10.0e-6, c17 = 100.0e-12, resetSeconds = 0.2e-6;
    constexpr double depthTrack = 10.0e3;

    constexpr int bbdStages = 1024; // MN3007
}

SmallCloneStyleChorusProcessor::SmallCloneStyleChorusProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> ("smallclone_rate", "Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto depth = std::make_unique<juce::AudioParameterFloat> ("smallclone_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v < 0.5f ? "Low" : "High"); }));
    rateParam = rate.get();
    depthParam = depth.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("smallclone", "Small Clone", "|", std::move (rate), std::move (depth));
}

void SmallCloneStyleChorusProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    // IC1A: 1 + R3 / (R4 + 1/sC2) = (1 + sC2 (R3 + R4)) / (1 + sC2 R4)
    const double r3 = 33.0e3, r4 = 6.8e3, c2 = 10.0e-9;
    const Rational preEmphasis { { 1.0, c2 * (r3 + r4) }, { 1.0, c2 * r4 } };

    // IC1B: the summing node's divider (dry R5, wet R24, out through R6 into the virtual ground) then -Zf / R6.
    const double g5 = 1.0 / 22.0e3, g24 = 1.0 / 27.0e3, g6 = 1.0 / 10.0e3, gSum = g5 + g24 + g6;
    const auto zf = parallel (resistor (33.0e3), seriesRC (6.8e3, 10.0e-9));
    const auto mixDry = cascade (gain (-g5 / gSum / 10.0e3), zf);
    const auto mixWet = cascade (gain (-g24 / gSum / 10.0e3), zf);

    const auto inFilter = bbdchorus::bbdFilter ({ { 3.3e3, 0.0, 3.3e-9, 0.0, 0.0 },
                                                  { 10.0e3, 0.0, 0.0, 15.0e-9, 0.0 },
                                                  { 10.0e3, 0.0, 470.0e-12, 0.0, 0.0 } });
    const auto outFilter = bbdchorus::bbdFilter ({ { 10.0e3, 0.0, 4.7e-9, 0.0, 0.0 },
                                                   { 39.0e3, 0.0, 0.0, 2.7e-9, 0.0 },
                                                   { 39.0e3, 0.0, 180.0e-12, 0.0, 0.0 } });

    for (auto& ch : channels)
    {
        ch.inputCoupling.prepare (couplingHighpass (33.0e-9, 220.0e3), newSampleRate, 0.0);     // C1 into R1
        ch.preEmphasis.prepare (preEmphasis, newSampleRate, 2340.0);
        // C5 into the bias trimmer's wiper (~25K at mid-travel), and C13 into R24 on the way back.
        ch.wetCoupling.prepare (cascade (couplingHighpass (1.0e-6, 25.0e3), couplingHighpass (1.0e-6, 27.0e3)), newSampleRate, 0.0);
        ch.mixDry.prepare (mixDry, newSampleRate, 2340.0);
        ch.mixWet.prepare (mixWet, newSampleRate, 2340.0);
        ch.bbd.prepare (newSampleRate, bbdStages, inFilter, outFilter);
    }

    smoothedRate.reset (newSampleRate, paramRampSeconds);
    smoothedRate.setCurrentAndTargetValue (rateParam->get());

    // IC3B switches when its + input (triangle through R27, square through R28) crosses the R29/R30 tap.
    const double toLow = (schmittRef * (r27 + r28) - squareHigh * r27) / r28;  // triangle falling while the square is high
    const double toHigh = (schmittRef * (r27 + r28) - squareLow * r27) / r28;  // triangle rising while it is low
    lfo.setThresholds (toLow, toHigh);
    updateControls (rateParam->get());
    reset();
}

void SmallCloneStyleChorusProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.inputCoupling.reset();
        ch.preEmphasis.reset();
        ch.wetCoupling.reset();
        ch.mixDry.reset();
        ch.mixWet.reset();
        ch.bbd.reset();
    }
    lfo.reset (1.8, true);
    controlVolts = 1.8;
    shortcut.reset();
}

void SmallCloneStyleChorusProcessor::updateControls (double rate) noexcept
{
    // Rate: the 1M reverse-log pot as a rheostat (wiper tied to one end), clockwise = less resistance, plus R26.
    const double r = r26 + rateMax * (1.0 - pots::reverseAudio (rate));
    lfo.setSlopes ((integratorRef - squareLow) / (r * c14), (squareHigh - integratorRef) / (r * c14));
}

double SmallCloneStyleChorusProcessor::advanceModulation() noexcept
{
    const double tri = lfo.advance (dt);

    // Depth: High = the triangle straight onto C16; Low = through a halving divider (source ~2.5K), so C16 also
    // smooths it. The CD4047's RC pin pushes current back through R31: its average sits VDD / ln(ratio) above C16.
    const bool high = depthParam->get() >= 0.5f;
    const double fraction = high ? 1.0 : 0.5, sourceOhms = high ? 0.0 : 0.25 * depthTrack;
    const double ratio = std::max (1.0001, (1.5 * vd - controlVolts) / std::max (1.0e-3, 0.5 * vd - controlVolts));
    const double pinAboveNode = vd / std::log (ratio);
    if (sourceOhms <= 0.0)
        controlVolts = fraction * tri;
    else
        controlVolts += dt / c16 * ((fraction * tri - controlVolts) / sourceOhms + pinAboveNode / r31);

    // Slow phase: the RC pin starts a full VDD above the 4047's threshold (VDD/2) and relaxes toward C16's voltage.
    const double period = bbdchorus::rcChargePeriod (r31, c17, 1.5 * vd, controlVolts, 0.5 * vd, resetSeconds);
    lastClockHz = 0.5 / period; // the 4047's Q outputs divide by two
    return lastClockHz;
}

void SmallCloneStyleChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedRate.setTargetValue (rateParam->get());
    const bool moving = smoothedRate.isSmoothing();

    for (int i = 0; i < numSamples; ++i)
    {
        if (moving)
            updateControls (smoothedRate.getNextValue());
        const double clockHz = advanceModulation();

        for (int c = 0; c < solveChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);

            const double pre = bbdchorus::railClip (ch.preEmphasis.process (ch.inputCoupling.process ((double) data[i])), -opAmpSwing, opAmpSwing);
            const double delayed = ch.wetCoupling.process (ch.bbd.process (pre, clockHz));
            const double out = ch.mixDry.process (pre) + ch.mixWet.process (delayed);
            data[i] = (float) bbdchorus::railClip (out, -opAmpSwing, opAmpSwing);
        }
    }

    shortcut.end (buffer);
}

void SmallCloneStyleChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
