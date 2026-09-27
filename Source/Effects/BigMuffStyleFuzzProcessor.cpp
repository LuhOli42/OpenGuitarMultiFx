#include "BigMuffStyleFuzzProcessor.h"
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

    // Values shared by both models (identical on both traces).
    constexpr double rFeedback = 470.0e3;     // R9 / R17 / R15, base-to-collector bias + feedback
    constexpr double rClipBase = 100.0e3;     // R20 / R16, clipping stages' base to ground
    constexpr double rStageIn = 10.0e3;       // R19 / R12, series into each clipping stage's base
    constexpr double cStageCouple = 0.1e-6;   // C5 / C13, coupling between stages
    constexpr double rSustainBottom = 1.0e3;  // R23, under the Sustain pot
    constexpr double rToneTrebleShunt = 22.0e3; // R5
    constexpr double cToneBassShunt = 0.01e-6;  // C8
    constexpr double cToneOut = 0.1e-6;       // C3, tone wiper into the output stage
    constexpr double rOutBaseBottom = 100.0e3; // R3
    constexpr double cOutCouple = 0.1e-6;     // C2, output coupling
    constexpr double sustainPot = 100.0e3, tonePot = 100.0e3, volumePot = 100.0e3;
    // The amplifier this pedal is plugged into. Same assumption as the DS-1 model.
    constexpr double outputLoadResistance = 1.0e6;
}

const BigMuffStyleFuzzProcessor::ModelSpec& BigMuffStyleFuzzProcessor::specFor (Model m) noexcept
{
    // USA Version 3, from Kit Rae's trace of a 1976 V3 ("76#3"), the most common V3 circuit. BC239 transistors.
    static const ModelSpec us {
        "Big Muff-Style Fuzz", "bmp",
        39.0e3,    // R2
        10.0e-6,   // C1
        47.0e3,    // R14
        470.0e-12, // C10 / C12 / C11
        15.0e3,    // R13 / R18 / R11
        100.0,     // R22
        150.0,     // R21 / R10
        1.0e-6,    // C4
        1.0e-6,    // C6 / C7, in series with the diodes
        0.004e-6,  // C9
        39.0e3,    // R8
        430.0e3,   // R7
        15.0e3,    // R6
        3.3e3,     // R4
        400.0      // BC239-class
    };
    // Version 7B/7C, from Kit Rae's trace of the all-green Civil War / Tall Font Russian. Unmarked Russian NPN;
    // Kit Rae gives 2N5089 as the modern equivalent, so the beta is higher than the BC239's.
    static const ModelSpec ru {
        "Russian Big Muff-Style Fuzz", "bmpru",
        39.0e3,    // R2
        0.1e-6,    // C1
        100.0e3,   // R14
        500.0e-12, // C10A+C10B etc: two 1 nF in series
        12.0e3,    // R13 / R18 / R11
        390.0,     // R22
        390.0,     // R21 / R10
        0.1e-6,    // C4
        0.047e-6,  // C6 / C7
        0.0039e-6, // C9
        20.0e3,    // R8
        470.0e3,   // R7
        10.0e3,    // R6
        2.7e3,     // R4
        500.0      // 2N5089-class
    };
    // Version 7, 1st edition (Red Army Overdrive / the first Sovtek Big Muff Pi). Identical to the Russian above in every
    // value Kit Rae's trace gives, except the three feedback/filter caps: 430 pF instead of two 1 nF in series (500 pF).
    // That cap sets the corner of each stage's feedback low-pass with its 470k (1/(2 pi 470k C): 787 Hz here, 677 Hz
    // with 500 pF), so this one is a touch brighter than the Green Russian. KT3102E transistors (hFE 400-1000).
    static const ModelSpec sovtek {
        "Sovtek Big Muff-Style Fuzz", "bmpsv",
        39.0e3, 0.1e-6, 100.0e3, 430.0e-12, 12.0e3, 390.0, 390.0, 0.1e-6, 0.047e-6, 0.0039e-6, 20.0e3, 470.0e3, 10.0e3, 2.7e3, 500.0
    };
    switch (m)
    {
        case Model::usV3: return us;
        case Model::russianGreen: return ru;
        case Model::sovtekFirstEdition: return sovtek;
    }
    return us;
}

BigMuffStyleFuzzProcessor::BigMuffStyleFuzzProcessor (Model m)
    : model (m), spec (specFor (m))
{
    const juce::String p (spec.idPrefix);
    auto sustain = std::make_unique<juce::AudioParameterFloat> (
        p + "_sustain", "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto tone = std::make_unique<juce::AudioParameterFloat> (
        p + "_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        p + "_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    sustainParam = sustain.get();
    toneParam = tone.get();
    volumeParam = volume.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        p, spec.displayName, "|", std::move (sustain));
    group->addChild (std::move (tone));
    group->addChild (std::move (volume));
    parameters = std::move (group);
}

void BigMuffStyleFuzzProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;
    const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, spec.beta, 4.0 };

    const auto vcc = c.addNode(), nIn = c.addNode();
    c.addSource (vcc, v9);
    ch.srcIn = c.addSource (nIn, 0.0);

    // ---- stage 1: input. R2 in series, C1 coupling, shunt-feedback bias (R14 to ground, R9 to the collector). ----
    const auto a1 = c.addNode(), b1 = c.addNode(), c1 = c.addNode(), e1 = c.addNode();
    c.addResistor (nIn, a1, spec.rIn);
    c.addCapacitor (a1, b1, spec.cIn);
    c.addResistor (b1, gnd, spec.rInBase);
    c.addResistor (b1, c1, rFeedback);
    c.addCapacitor (b1, c1, spec.cFeedback);
    c.addResistor (c1, vcc, spec.rCollector);
    c.addResistor (e1, gnd, spec.rEmitter1);
    c.addBjt (c1, b1, e1, false, npn);
    ch.nCollector[0] = c1;

    // ---- Sustain pot: the input stage's output feeds the top, R23 sits under the bottom. ----
    const auto susTop = c.addNode(), susW = c.addNode(), susBot = c.addNode();
    c.addCapacitor (c1, susTop, spec.cStageOut);
    ch.rSustainTop = c.addResistor (susTop, susW, sustainPot * 0.5);
    ch.rSustainBottom = c.addResistor (susW, susBot, sustainPot * 0.5);
    c.addResistor (susBot, gnd, rSustainBottom);

    // ---- stages 2 and 3: the clippers. Identical cells; stage 2 is fed from the Sustain wiper. ----
    NodalCircuit::Node previousOut = susW;
    for (int stage = 0; stage < 2; ++stage)
    {
        const auto s = c.addNode(), b = c.addNode(), col = c.addNode(), e = c.addNode(), dmid = c.addNode();
        c.addCapacitor (previousOut, s, cStageCouple);
        c.addResistor (s, b, rStageIn);
        c.addResistor (b, gnd, rClipBase);
        c.addResistor (b, col, rFeedback);
        c.addCapacitor (b, col, spec.cFeedback);
        // The clipping branch: a capacitor IN SERIES with an anti-parallel diode pair, base to collector. The cap
        // blocks DC, so the diodes only ever see the signal and the stage's bias is untouched by them.
        c.addCapacitor (b, dmid, spec.cClipSeries);
        c.addDiode (dmid, col, siIs, siNVt, 4.0e-9);
        c.addDiode (col, dmid, siIs, siNVt, 4.0e-9);
        c.addResistor (col, vcc, spec.rCollector);
        c.addResistor (e, gnd, spec.rEmitterClip);
        c.addBjt (col, b, e, false, npn);
        ch.nCollector[stage + 1] = col;
        previousOut = col;
    }

    // ---- tone stack: a treble branch (C9 into R5 to ground) and a bass branch (R8 into C8 to ground), the pot
    //      blending between them. The middle sits in the notch between the two at every setting. ----
    const auto tTreble = c.addNode(), tBass = c.addNode(), tW = c.addNode();
    c.addCapacitor (previousOut, tTreble, spec.cToneTreble);
    c.addResistor (tTreble, gnd, rToneTrebleShunt);
    c.addResistor (previousOut, tBass, spec.rToneBass);
    c.addCapacitor (tBass, gnd, cToneBassShunt);
    ch.rToneTreble = c.addResistor (tTreble, tW, tonePot * 0.5);
    ch.rToneBass = c.addResistor (tW, tBass, tonePot * 0.5);

    // ---- stage 4: output recovery, a plain divider-biased common-emitter stage with an unbypassed emitter. ----
    const auto b4 = c.addNode(), c4 = c.addNode(), e4 = c.addNode();
    c.addCapacitor (tW, b4, cToneOut);
    c.addResistor (b4, vcc, spec.rOutBaseTop);
    c.addResistor (b4, gnd, rOutBaseBottom);
    c.addResistor (c4, vcc, spec.rOutCollector);
    c.addResistor (e4, gnd, spec.rOutEmitter);
    c.addBjt (c4, b4, e4, false, npn);
    ch.nCollector[3] = c4;

    const auto volTop = c.addNode(), volW = c.addNode();
    c.addCapacitor (c4, volTop, cOutCouple);
    ch.rVolumeTop = c.addResistor (volTop, volW, volumePot * 0.5);
    ch.rVolumeBottom = c.addResistor (volW, gnd, volumePot * 0.5);
    c.addResistor (volW, gnd, outputLoadResistance);
    ch.nOut = volW;

    // Bias guesses (hand-derived in the doc): shunt-feedback stages settle well above mid-rail on the input stage
    // and near 3.8 V on the clippers; the output stage's divider puts its base at ~1.7 V.
    c.setInitialGuess (c1, 6.5);
    c.setInitialGuess (ch.nCollector[1], 3.8);
    c.setInitialGuess (ch.nCollector[2], 3.8);
    c.setInitialGuess (b4, 1.7);
    c.setInitialGuess (c4, 4.5);
}

void BigMuffStyleFuzzProcessor::updatePots (double sustain, double tone, double volume)
{
    const auto clampFraction = [] (double x) { return juce::jlimit (0.001, 0.999, x); };
    const double s = clampFraction (sustain), t = clampFraction (tone), v = clampFraction (volume);
    for (auto& ch : channels)
    {
        // All three are linear-taper pots on both traces ("Linear taper potentiometers" on the schematics), so the
        // wiper splits each track in proportion -- no PotTaper law here, unlike the log-pot pedals.
        ch.c.setResistance (ch.rSustainTop, sustainPot * (1.0 - s));
        ch.c.setResistance (ch.rSustainBottom, sustainPot * s);
        ch.c.setResistance (ch.rToneTreble, tonePot * (1.0 - t));
        ch.c.setResistance (ch.rToneBass, tonePot * t);
        ch.c.setResistance (ch.rVolumeTop, volumePot * (1.0 - v));
        ch.c.setResistance (ch.rVolumeBottom, volumePot * v);
    }
}

void BigMuffStyleFuzzProcessor::prepare (double newSampleRate, int, int)
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

    updatePots (sustainParam->get(), toneParam->get(), volumeParam->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

double BigMuffStyleFuzzProcessor::debugCollector (int stage) const noexcept
{
    return channels[0].c.voltage (channels[0].nCollector[juce::jlimit (0, 3, stage)]);
}

void BigMuffStyleFuzzProcessor::process (juce::AudioBuffer<float>& buffer)
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

void BigMuffStyleFuzzProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Placeholder: the reference sheet lists Fuzz as its own Drive-category glyph, but that sheet still is not in
    // the repo (see docs/icons/AGENT-icon-notes.md), and this project never invents a glyph. The distortion wave is
    // the closest existing same-category icon -- the same stand-in precedent the DS-1 and the IR Reverb role set.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
