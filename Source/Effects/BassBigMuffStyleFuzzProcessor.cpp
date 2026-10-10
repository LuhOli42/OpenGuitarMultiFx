#include "BassBigMuffStyleFuzzProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double v9 = 9.0;

    // 1N914 / 1N4148-class silicon, the project's standard set (N = 1.752 matters: see docs/circuits/GainAudit.md).
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;

    // The Bass Big Muff sits in the Sovtek/Russian family (Kit Rae): same values as the Green Russian trace.
    constexpr double rIn = 39.0e3;
    constexpr double cInFlat = 0.022e-6;      // C1 flat position
    constexpr double cInBoost = 0.082e-6;     // C1 + the boost cap in parallel (BASS BOOST on)
    constexpr double rInBase = 100.0e3;       // R14
    constexpr double cFeedback = 500.0e-12;   // two 1 nF in series on every stage
    constexpr double rCollector = 12.0e3;
    constexpr double rEmitter1 = 390.0;
    constexpr double rEmitterClip = 390.0;
    constexpr double cStageOut = 0.1e-6;      // C4
    constexpr double cClipSeries = 0.047e-6;  // C6 / C7
    constexpr double cToneTreble = 0.0039e-6; // C9
    constexpr double rToneBass = 20.0e3;      // R8
    constexpr double rOutBaseTop = 470.0e3;   // R7
    constexpr double rOutCollector = 10.0e3;  // R6
    constexpr double rOutEmitter = 2.7e3;     // R4
    constexpr double beta = 500.0;            // 2N5089-class

    // Values shared with every Big Muff variant.
    constexpr double rFeedback = 470.0e3;
    constexpr double rClipBase = 100.0e3;
    constexpr double rStageIn = 10.0e3;
    constexpr double cStageCouple = 0.1e-6;
    constexpr double rSustainBottom = 1.0e3;
    constexpr double rToneTrebleShunt = 22.0e3;
    constexpr double cToneBassShunt = 0.01e-6;
    constexpr double cToneOut = 0.1e-6;
    constexpr double rOutBaseBottom = 100.0e3;
    constexpr double cOutCouple = 0.1e-6;
    constexpr double sustainPot = 100.0e3, tonePot = 100.0e3, volumePot = 100.0e3;
    constexpr double outputLoadResistance = 1.0e6;

    constexpr double rDryOn = 10.0e3;         // DRY path: input source summed into the output node
    constexpr double rDryOff = 1.0e9;
}

BassBigMuffStyleFuzzProcessor::BassBigMuffStyleFuzzProcessor()
{
    auto sustain = std::make_unique<juce::AudioParameterFloat> (
        "bbmp_sustain", "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto tone = std::make_unique<juce::AudioParameterFloat> (
        "bbmp_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        "bbmp_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto bassBoost = std::make_unique<juce::AudioParameterFloat> (
        "bbmp_bass_boost", "Bass Boost", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f);
    auto dry = std::make_unique<juce::AudioParameterFloat> (
        "bbmp_dry", "Dry", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f);

    sustainParam = sustain.get();
    toneParam = tone.get();
    volumeParam = volume.get();
    bassBoostParam = bassBoost.get();
    dryParam = dry.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "bbmp", "Bass Big Muff-Style Fuzz", "|", std::move (sustain));
    group->addChild (std::move (tone));
    group->addChild (std::move (volume));
    group->addChild (std::move (bassBoost));
    group->addChild (std::move (dry));
    parameters = std::move (group);
}

void BassBigMuffStyleFuzzProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;
    const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, beta, 4.0 };

    const auto vcc = c.addNode(), nIn = c.addNode();
    c.addSource (vcc, v9);
    ch.srcIn = c.addSource (nIn, 0.0);

    // ---- stage 1: input. C1 is the Bass Boost switch's cap (handled, see updateSwitches). ----
    const auto a1 = c.addNode(), b1 = c.addNode(), c1 = c.addNode(), e1 = c.addNode();
    c.addResistor (nIn, a1, rIn);
    ch.cIn = c.addCapacitor (a1, b1, cInFlat);
    c.addResistor (b1, gnd, rInBase);
    c.addResistor (b1, c1, rFeedback);
    c.addCapacitor (b1, c1, cFeedback);
    c.addResistor (c1, vcc, rCollector);
    c.addResistor (e1, gnd, rEmitter1);
    c.addBjt (c1, b1, e1, false, npn);
    ch.nCollector[0] = c1;

    // ---- Sustain pot. ----
    const auto susTop = c.addNode(), susW = c.addNode(), susBot = c.addNode();
    c.addCapacitor (c1, susTop, cStageOut);
    ch.rSustainTop = c.addResistor (susTop, susW, sustainPot * 0.5);
    ch.rSustainBottom = c.addResistor (susW, susBot, sustainPot * 0.5);
    c.addResistor (susBot, gnd, rSustainBottom);

    // ---- stages 2 and 3: the clippers. ----
    NodalCircuit::Node previousOut = susW;
    for (int stage = 0; stage < 2; ++stage)
    {
        const auto s = c.addNode(), b = c.addNode(), col = c.addNode(), e = c.addNode(), dmid = c.addNode();
        c.addCapacitor (previousOut, s, cStageCouple);
        c.addResistor (s, b, rStageIn);
        c.addResistor (b, gnd, rClipBase);
        c.addResistor (b, col, rFeedback);
        c.addCapacitor (b, col, cFeedback);
        c.addCapacitor (b, dmid, cClipSeries);
        c.addDiode (dmid, col, siIs, siNVt, 4.0e-9);
        c.addDiode (col, dmid, siIs, siNVt, 4.0e-9);
        c.addResistor (col, vcc, rCollector);
        c.addResistor (e, gnd, rEmitterClip);
        c.addBjt (col, b, e, false, npn);
        ch.nCollector[stage + 1] = col;
        previousOut = col;
    }

    // ---- tone stack. ----
    const auto tTreble = c.addNode(), tBass = c.addNode(), tW = c.addNode();
    c.addCapacitor (previousOut, tTreble, cToneTreble);
    c.addResistor (tTreble, gnd, rToneTrebleShunt);
    c.addResistor (previousOut, tBass, rToneBass);
    c.addCapacitor (tBass, gnd, cToneBassShunt);
    ch.rToneTreble = c.addResistor (tTreble, tW, tonePot * 0.5);
    ch.rToneBass = c.addResistor (tW, tBass, tonePot * 0.5);

    // ---- stage 4: output recovery. ----
    const auto b4 = c.addNode(), c4 = c.addNode(), e4 = c.addNode();
    c.addCapacitor (tW, b4, cToneOut);
    c.addResistor (b4, vcc, rOutBaseTop);
    c.addResistor (b4, gnd, rOutBaseBottom);
    c.addResistor (c4, vcc, rOutCollector);
    c.addResistor (e4, gnd, rOutEmitter);
    c.addBjt (c4, b4, e4, false, npn);
    ch.nCollector[3] = c4;

    const auto volTop = c.addNode(), volW = c.addNode(), o = c.addNode();
    c.addCapacitor (c4, volTop, cOutCouple);
    ch.rVolumeTop = c.addResistor (volTop, volW, volumePot * 0.5);
    ch.rVolumeBottom = c.addResistor (volW, gnd, volumePot * 0.5);
    // DRY sums downstream of the Volume divider (constant-level dry + whatever the wiper passes),
    // matching the real pedal's "Volume becomes a blend" behaviour. The tap is the input source
    // itself so the dry level doesn't depend on the clip stages.
    c.addResistor (volW, o, 22.0e3);
    ch.rDry = c.addResistor (nIn, o, rDryOff);
    c.addResistor (o, gnd, outputLoadResistance);
    ch.nOut = o;

    c.setInitialGuess (c1, 6.5);
    c.setInitialGuess (ch.nCollector[1], 3.8);
    c.setInitialGuess (ch.nCollector[2], 3.8);
    c.setInitialGuess (b4, 1.7);
    c.setInitialGuess (c4, 4.5);
}

void BassBigMuffStyleFuzzProcessor::updatePots (double sustain, double tone, double volume)
{
    const auto clampFraction = [] (double x) { return juce::jlimit (0.001, 0.999, x); };
    const double s = clampFraction (sustain), t = clampFraction (tone), v = clampFraction (volume);
    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rSustainTop, sustainPot * (1.0 - s));
        ch.c.setResistance (ch.rSustainBottom, sustainPot * s);
        ch.c.setResistance (ch.rToneTreble, tonePot * (1.0 - t));
        ch.c.setResistance (ch.rToneBass, tonePot * t);
        ch.c.setResistance (ch.rVolumeTop, volumePot * (1.0 - v));
        ch.c.setResistance (ch.rVolumeBottom, volumePot * v);
    }
}

void BassBigMuffStyleFuzzProcessor::updateSwitches()
{
    const int boost = bassBoostParam->get() > 0.5f ? 1 : 0;
    const int dry = dryParam->get() > 0.5f ? 1 : 0;
    if (boost == appliedBoost && dry == appliedDry)
        return;
    appliedBoost = boost;
    appliedDry = dry;
    for (auto& ch : channels)
    {
        ch.c.setCapacitance (ch.cIn, boost ? cInBoost : cInFlat);
        ch.c.setResistance (ch.rDry, dry ? rDryOn : rDryOff);
    }
}

void BassBigMuffStyleFuzzProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedSustain.reset (newSampleRate, 0.02);
    smoothedSustain.setCurrentAndTargetValue (sustainParam->get());
    smoothedTone.reset (newSampleRate, 0.02);
    smoothedTone.setCurrentAndTargetValue (toneParam->get());
    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volumeParam->get());

    appliedBoost = appliedDry = -1;
    updateSwitches();
    updatePots (sustainParam->get(), toneParam->get(), volumeParam->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

double BassBigMuffStyleFuzzProcessor::debugCollector (int stage) const noexcept
{
    return channels[0].c.voltage (channels[0].nCollector[juce::jlimit (0, 3, stage)]);
}

void BassBigMuffStyleFuzzProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedSustain.setTargetValue (sustainParam->get());
    smoothedTone.setTargetValue (toneParam->get());
    smoothedVolume.setTargetValue (volumeParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float s = smoothedSustain.getNextValue();
        const float t = smoothedTone.getNextValue();
        const float v = smoothedVolume.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updateSwitches();
            updatePots (s, t, v);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.c.setSource (ch.srcIn, (double) data[i]);
            const bool ok = ch.c.solveSample();
            data[i] = (float) ch.c.voltage (ch.nOut);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void BassBigMuffStyleFuzzProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Same stand-in as BigMuffStyleFuzzProcessor: no Fuzz glyph exists on the sheet yet.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
