#include "DC2StyleChorusProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    constexpr double opAmpSwing = 3.5;       // M5218L on 9 V, about 4.5 V
    constexpr int bbdStages = 1024;          // MN3207
    constexpr double centreClockHz = 120.0e3; // ~4.3 ms -- calibrated, see the doc
    constexpr double companderUnity = 0.3;    // rectified-average volts where the NE570 sits at unity
    constexpr double rectifierCap = 0.47e-6;  // C10, C47, C48

    // Mode buttons 1-4: LFO speed and depth (fractional clock swing). Modes 1-2 slow, 3-4 fast; 2 and 4 deeper.
    struct Mode { double hz, swing; };
    constexpr Mode modes[4] { { 0.25, 0.04 }, { 0.25, 0.08 }, { 0.5, 0.04 }, { 0.5, 0.08 } };

    std::vector<LadderNode> bbdLadder()
    {
        return { { 10.0e3, 0.0, 470.0e-12, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 1.8e-9, 0.0 }, { 10.0e3, 0.0, 220.0e-12, 0.0, 0.0 } };
    }
}

DC2StyleChorusProcessor::DC2StyleChorusProcessor()
{
    auto mode = std::make_unique<juce::AudioParameterFloat> ("dc2_mode", "Mode", juce::NormalisableRange<float> (1.0f, 4.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); }));
    modeParam = mode.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("dc2", "DC-2", "|", std::move (mode));
}

void DC2StyleChorusProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    inputCoupling.prepare (couplingHighpass (0.047e-6, 1.0e6), newSampleRate, 0.0); // C4 into R5
    // IC1b: -(R10 || C8) / (R7 || (R8 + 1/sC5))
    preEmphasis.prepare (inverting (parallelRC (33.0e3, 100.0e-12), parallel (resistor (47.0e3), seriesRC (10.0e3, 4.7e-9))), newSampleRate, 3390.0);

    // IC12: feedback R75 47K || (R74 10K + C51 4.7n) || C50 47p; dry through 33K, own BBD through R86 39K (A) / R85 47K (B),
    // the other BBD through R80/R81 180K.
    const auto zf = parallel (parallel (resistor (47.0e3), seriesRC (10.0e3, 4.7e-9)), capacitor (47.0e-12));
    mixDry.prepare (inverting (zf, resistor (33.0e3)), newSampleRate, 3390.0);
    mixOwnA.prepare (inverting (zf, resistor (39.0e3)), newSampleRate, 3390.0);
    mixOwnB.prepare (inverting (zf, resistor (47.0e3)), newSampleRate, 3390.0);
    mixCross.prepare (cascade (gain (-1.0), inverting (zf, resistor (180.0e3))), newSampleRate, 3390.0);
    mixCrossB = mixCross;

    const auto f = bbdchorus::bbdFilter (bbdLadder());
    bbdA.prepare (newSampleRate, bbdStages, f, f);
    bbdB.prepare (newSampleRate, bbdStages, f, f);

    compressor.prepare (newSampleRate, rectifierCap, companderUnity);
    expanderA.prepare (newSampleRate, rectifierCap, companderUnity);
    expanderB.prepare (newSampleRate, rectifierCap, companderUnity);

    lfo.setThresholds (-1.0, 1.0);
    currentMode = -1;
    updateMode (juce::roundToInt (modeParam->get()));
    reset();
}

void DC2StyleChorusProcessor::reset()
{
    inputCoupling.reset();
    preEmphasis.reset();
    mixDry.reset();
    mixOwnA.reset();
    mixOwnB.reset();
    mixCross.reset();
    mixCrossB.reset();
    bbdA.reset();
    bbdB.reset();
    compressor.reset();
    expanderA.reset();
    expanderB.reset();
    lfo.reset (0.0, true);
}

void DC2StyleChorusProcessor::updateMode (int mode) noexcept
{
    mode = juce::jlimit (1, 4, mode);
    if (mode == currentMode)
        return;
    currentMode = mode;
    const auto& m = modes[mode - 1];
    lfo.setSlopes (4.0 * m.hz, 4.0 * m.hz); // a +-1 triangle at m.hz
    swing = m.swing;
}

void DC2StyleChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    updateMode (juce::roundToInt (modeParam->get()));

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const double tri = lfo.advance (dt);
        // The two clock drivers see the LFO with opposite signs (IC5a inverts it for IC3/IC4).
        clockA = centreClockHz * (1.0 + swing * tri);
        clockB = centreClockHz * (1.0 - swing * tri);

        const double x = right != nullptr ? 0.5 * ((double) left[i] + (double) right[i]) : (double) left[i];
        const double pre = bbdchorus::railClip (preEmphasis.process (inputCoupling.process (x)), -opAmpSwing, opAmpSwing);
        const double squeezed = bbdchorus::railClip (compressor.compress (pre), -opAmpSwing, opAmpSwing);
        const double wetA = bbdchorus::railClip (expanderA.expand (bbdA.process (squeezed, clockA)), -opAmpSwing, opAmpSwing);
        const double wetB = bbdchorus::railClip (expanderB.expand (bbdB.process (squeezed, clockB)), -opAmpSwing, opAmpSwing);

        const double dry = mixDry.process (pre);
        left[i] = (float) bbdchorus::railClip (dry + mixOwnA.process (wetA) + mixCross.process (wetB), -opAmpSwing, opAmpSwing);
        if (right != nullptr)
        {
            right[i] = (float) bbdchorus::railClip (dry + mixOwnB.process (wetB) + mixCrossB.process (wetA), -opAmpSwing, opAmpSwing);
        }
    }

    for (int c = 2; c < numChannels; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void DC2StyleChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
