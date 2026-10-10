#include "EPStyleBoosterProcessor.h"
#include "IconKit.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double supplyVolts = 22.0;          // the drawing's filtered rail (node 12 marked 22 V)
    constexpr double levelPotMax = 500.0e3;       // Record Level
    constexpr double outputLoadResistance = 1.0e6;

    // TIS58 (N-channel JFET). The drawing marks the bias (gate 0 V, source 1.1 V across 3.3K = 0.33 mA); Idss and pinch-off
    // are fitted so the model sits there. Not datasheet values (TIS58's spread is wide).
    const NodalCircuit::JfetParams tis58 { 1.6e-3, -2.0, 0.02 };
    // 2N3053 (Q3 in the EP-3, drawn as a plain NPN; the drawing gives no gain).
    const NodalCircuit::BjtParams npn2N3053 { 1.0e-14, 25.85e-3, 150.0, 4.0 };
}

EPStyleBoosterProcessor::EPStyleBoosterProcessor()
{
    auto level = std::make_unique<juce::AudioParameterFloat> ("epbooster_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f);
    levelParam = level.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("epbooster", "EP-Style Booster", "|", std::move (level));
}

void EPStyleBoosterProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;

    const auto rail = c.addNode(), in = c.addNode();
    c.addSource (rail, supplyVolts);
    ch.srcIn = c.addSource (in, 0.0);

    // ---- Q5 (JFET): 0.047 uF, 100K and 100 pF into the gate (1M to ground); source 3.3K unbypassed; drain 22K ----
    const auto nA = c.addNode(), nG = c.addNode(), nD = c.addNode(), nS = c.addNode();
    c.addCapacitor (in, nA, 0.047e-6);
    c.addResistor (nA, nG, 100.0e3);
    c.addCapacitor (nG, gnd, 100.0e-12);
    c.addResistor (nG, gnd, 1.0e6);
    c.addResistor (rail, nD, 22.0e3);
    c.addJfet (nD, nG, nS, tis58);
    c.addResistor (nS, gnd, 3.3e3);

    // ---- 0.1 uF into the Record Level pot; wiper -> 47K ----
    // The tape deck's 2 nF pre-emphasis across the 47K is deliberately NOT modelled: it is record-side
    // EQ that the playback chain de-emphasizes, so the standalone booster voicing (what an EP-style
    // booster pedal reproduces) leaves it out -- keeping it adds a +13 dB presence shelf at 3-6 kHz
    // the pedal never had.
    const auto nP = c.addNode(), nW = c.addNode(), n20 = c.addNode();
    c.addCapacitor (nD, nP, 0.1e-6);
    ch.rLevelTop = c.addResistor (nP, nW, levelPotMax * 0.5);
    ch.rLevelBottom = c.addResistor (nW, gnd, levelPotMax * 0.5);
    c.addResistor (nW, n20, 47.0e3);

    // ---- Q3: 0.1 uF to the base (47K to ground, 470K from the collector), emitter 1K || 100 uF, collector 22K, 0.1 uF out ----
    const auto nB = c.addNode(), nC = c.addNode(), nE = c.addNode(), nOut = c.addNode();
    c.addCapacitor (n20, nB, 0.1e-6);
    c.addResistor (nB, gnd, 47.0e3);
    c.addResistor (nC, nB, 470.0e3);
    c.addBjt (nC, nB, nE, false, npn2N3053);
    c.addResistor (rail, nC, 22.0e3);
    c.addResistor (nE, gnd, 1.0e3);
    c.addCapacitor (nE, gnd, 100.0e-6);
    c.addCapacitor (nC, nOut, 0.1e-6);
    c.addResistor (nOut, gnd, outputLoadResistance);

    ch.nD = nD;
    ch.nS = nS;
    ch.nB = nB;
    ch.nC = nC;
    ch.nE = nE;
    ch.nOut = nOut;

    // Bias guesses: the drawing's marked voltages.
    c.setInitialGuess (nD, 14.4);
    c.setInitialGuess (nS, 1.1);
    c.setInitialGuess (nP, 0.0);
    c.setInitialGuess (nB, 1.0);
    c.setInitialGuess (nC, 12.2);
    c.setInitialGuess (nE, 0.4);
}

void EPStyleBoosterProcessor::updatePots (double level)
{
    // Record Level: a 500K audio-taper pot to ground, clockwise = more (the wiper toward the top).
    const double bottom = juce::jmax (1.0, levelPotMax * pots::audio (level));
    const double top = juce::jmax (1.0, levelPotMax - bottom);
    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rLevelTop, top);
        ch.c.setResistance (ch.rLevelBottom, bottom);
    }
}

void EPStyleBoosterProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (levelParam->get());

    updatePots (levelParam->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void EPStyleBoosterProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (l);
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

void EPStyleBoosterProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::boost_svg, IconData::boost_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
