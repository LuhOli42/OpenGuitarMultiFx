#include "CE1StyleChorusProcessor.h"
#include "Effects/IconKit.h"
#include "Effects/PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace analog;

    constexpr double railVolts = 12.0;    // TA7136 on the CE-1's +-13 V supplies
    constexpr double railKnee = 1.0;

    // LFO (IC-4, uPC1458 on +-13 V): Schmitt R80 180K / R81 220K, integrator C43 82n through R76 18K.
    constexpr double squareVolts = 14.0 * 0.82; // what makes the triangle the service notes' 23 Vpp (wave K)
    constexpr double r80 = 180.0e3, r81 = 220.0e3, r82 = 100.0e3, r65 = 1.5e3, r57 = 100.0e3, r76 = 18.0e3, c43 = 0.082e-6, vr12 = 10.0e3;

    // Clock (discrete multivibrator Q3-Q6): wave G spans 5..16 us; waves E/N show a 3 ms delay at chorus minimum.
    constexpr double chorusCentreHz = 256.0 / 3.0e-3;
    constexpr double chorusSwing = 0.25;
    constexpr double vibratoCentreHz = 0.5 * (1.0 / 5.0e-6 + 1.0 / 16.0e-6), vibratoSwingHz = 0.5 * (1.0 / 5.0e-6 - 1.0 / 16.0e-6);

    constexpr int bbdStages = 512; // MN3002
    constexpr double testHz = 440.0;
}

CE1StyleChorusProcessor::CE1StyleChorusProcessor()
{
    using Range = juce::NormalisableRange<float>;
    auto level = std::make_unique<juce::AudioParameterFloat> ("ce1_level", "Level", Range (0.0f, 1.0f), 0.5f);
    auto intensity = std::make_unique<juce::AudioParameterFloat> ("ce1_intensity", "Intensity", Range (0.0f, 1.0f), 0.5f);
    auto mode = std::make_unique<juce::AudioParameterFloat> ("ce1_mode", "Mode", Range (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v < 0.5f ? "Chorus" : "Vibrato"); }));
    auto rate = std::make_unique<juce::AudioParameterFloat> ("ce1_rate", "Rate", Range (0.0f, 1.0f), 0.5f);
    auto depth = std::make_unique<juce::AudioParameterFloat> ("ce1_depth", "Depth", Range (0.0f, 1.0f), 0.5f);
    levelParam = level.get();
    intensityParam = intensity.get();
    modeParam = mode.get();
    rateParam = rate.get();
    depthParam = depth.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ce1", "CE-1", "|", std::move (level), std::move (intensity),
                                                                        std::move (mode), std::move (rate), std::move (depth));
}

void CE1StyleChorusProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    dt = 1.0 / newSampleRate;

    // IC-1: 1 + R11 / (R12 + 1/sC07)
    const double r11 = 470.0e3, r12 = 10.0e3, c07 = 0.47e-6;
    const Rational preamp { { 1.0, c07 * (r12 + r11) }, { 1.0, c07 * r12 } };

    const std::vector<LadderNode> inLadder { { 22.0e3, 6.8e-9, 0.0, 0.0, 10.0e3 },   // R13 || C06, R5 to ground
                                             { 10.0e3, 0.0, 0.0, 4.7e-9, 0.0 },      // R14, C12 to Q2's emitter
                                             { 10.0e3, 0.0, 1.0e-9, 0.0, 0.0 } };    // R16, C11
    const std::vector<LadderNode> outLadder { { 22.0e3, 0.0, 4.7e-9, 0.0, 0.0 },     // R41, C28
                                              { 6.8e3, 0.0, 0.0, 1.0e-9, 0.0 },      // R42, C26 to Q10's emitter
                                              { 15.0e3, 0.0, 0.0, 1.0e-9, 0.0 },     // R43, C29 to Q10's emitter
                                              { 15.0e3, 0.0, 470.0e-12, 0.0, 0.0 } }; // R44, C27
    const auto inH = ladderTransfer (inLadder), outH = ladderTransfer (outLadder);
    const auto inFilter = bbdchorus::bbdFilter (inLadder), outFilter = bbdchorus::bbdFilter (outLadder);

    // Mix levels from the service notes' waveforms, both referred to IC-1's output (C, 4 Vpp at 440 Hz): M = 240 mVpp
    // of dry, N = 350 mVpp of wet. The wet figure includes the filters' own gain at 440 Hz, so divide it back out.
    const Complex s (0.0, 2.0 * juce::MathConstants<double>::pi * testHz);
    dryGain = 0.240 / 4.0;
    wetGain = (0.350 / 4.0) / std::abs (inH.at (s) * outH.at (s));

    for (auto& ch : channels)
    {
        ch.preamp.prepare (preamp, newSampleRate, 0.0);
        ch.wetCoupling.prepare (couplingHighpass (0.1e-6, 100.0e3), newSampleRate, 0.0); // C14 into R21
        ch.bbd.prepare (newSampleRate, bbdStages, inFilter, outFilter);
    }

    smoothedLevel.reset (newSampleRate, paramRampSeconds);
    smoothedLevel.setCurrentAndTargetValue (levelParam->get());
    smoothedIntensity.reset (newSampleRate, paramRampSeconds);
    smoothedIntensity.setCurrentAndTargetValue (intensityParam->get());
    smoothedRate.reset (newSampleRate, paramRampSeconds);
    smoothedRate.setCurrentAndTargetValue (rateParam->get());
    smoothedDepth.reset (newSampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depthParam->get());

    const double threshold = squareVolts * r80 / r81;
    lfo.setThresholds (-threshold, threshold);
    updateControls (intensityParam->get(), rateParam->get());
    reset();
}

void CE1StyleChorusProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.preamp.reset();
        ch.wetCoupling.reset();
        ch.bbd.reset();
    }
    lfo.reset (0.0, true);
    vibratoPhase = 0.0;
    shortcut.reset();
}

void CE1StyleChorusProcessor::updateControls (double intensity, double rate) noexcept
{
    // Chorus Intensity: R82 from the Schmitt output into VR12's wiper; one end of the track through R65 to ground,
    // the other through R57 and R76 into the integrator's virtual ground. Clockwise moves the wiper toward R65's end,
    // which shunts less of the drive away.
    const double toGround = std::clamp (intensity, 0.0, 1.0) * vr12 + r65;
    const double toIntegrator = (1.0 - std::clamp (intensity, 0.0, 1.0)) * vr12 + r57 + r76;
    const double shunt = toGround * toIntegrator / (toGround + toIntegrator);
    const double wiperVolts = squareVolts * shunt / (r82 + shunt);
    const double slope = wiperVolts / toIntegrator / c43;
    lfo.setSlopes (slope, slope);

    // Vibrato Rate (VR14 100KC): 0.5 .. 8 Hz, reverse-log -- an estimate, see the doc.
    vibratoHz = 0.5 * std::pow (16.0, pots::reverseAudio (rate));
}

double CE1StyleChorusProcessor::advanceModulation() noexcept
{
    vibrato = modeParam->get() >= 0.5f;
    const double tri = lfo.advance (dt);
    if (! vibrato)
    {
        lastClockHz = chorusCentreHz * (1.0 + chorusSwing * tri / (squareVolts * r80 / r81));
        return lastClockHz;
    }

    vibratoPhase += vibratoHz * dt;
    if (vibratoPhase >= 1.0)
        vibratoPhase -= 1.0;
    // Wave L: at Vibrato the LFO is a sine (the diode network around IC-4b rounds it).
    const double depth = (double) smoothedDepth.getCurrentValue();
    lastClockHz = vibratoCentreHz + vibratoSwingHz * depth * std::sin (2.0 * juce::MathConstants<double>::pi * vibratoPhase);
    return lastClockHz;
}

void CE1StyleChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedLevel.setTargetValue (levelParam->get());
    smoothedIntensity.setTargetValue (intensityParam->get());
    smoothedRate.setTargetValue (rateParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    const bool moving = smoothedIntensity.isSmoothing() || smoothedRate.isSmoothing();

    for (int i = 0; i < numSamples; ++i)
    {
        if (moving)
            updateControls (smoothedIntensity.getNextValue(), smoothedRate.getNextValue());
        smoothedDepth.getNextValue();
        const double level = pots::audio (smoothedLevel.getNextValue());
        const double clockHz = advanceModulation();
        const double dryAmount = vibrato ? 0.0 : dryGain;

        for (int c = 0; c < solveChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);

            // VR11 (input Level) into IC-1, which clips at its rails.
            const double pre = bbdchorus::railClip (ch.preamp.process (level * (double) data[i]), -railVolts, railVolts, railKnee);
            const double wet = ch.bbd.process (ch.wetCoupling.process (pre), clockHz);
            data[i] = (float) (dryAmount * pre + wetGain * wet);
        }
    }

    shortcut.end (buffer);
}

void CE1StyleChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
