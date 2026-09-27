#include "OverdriverStyleOverdriveProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double supplyVolts = 9.0;
    constexpr double gainPotMax = 10.0e3;    // 10K rheostat
    constexpr double tonePotMax = 100.0e3;   // both tone pots 100K
    constexpr double outputLoadResistance = 1.0e6;

    // BC109: a low-noise small-signal NPN, hFE 200-800. beta is an assumption (the drawing gives none).
    const NodalCircuit::BjtParams bc109 { 1.0e-14, 25.85e-3, 350.0, 4.0 };
}

OverdriverStyleOverdriveProcessor::OverdriverStyleOverdriveProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };

    auto gain = make ("overdriver_gain", "Gain", 0.6f);
    auto bass = make ("overdriver_bass", "Bass", 0.5f);
    auto treble = make ("overdriver_treble", "Treble", 0.5f);
    gainParam = gain.get();
    bassParam = bass.get();
    trebleParam = treble.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "overdriver", "Overdriver-Style Overdrive", "|", std::move (gain));
    group->addChild (std::move (bass));
    group->addChild (std::move (treble));
    parameters = std::move (group);
}

void OverdriverStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;

    const auto rail = c.addNode(), in = c.addNode();
    c.addSource (rail, supplyVolts);
    ch.srcIn = c.addSource (in, 0.0);

    // ---- TR1: 0.22 uF into the base; the base's DC comes from TR2's emitter through 150K. TR1's collector is TR2's base. ----
    const auto nB1 = c.addNode(), nX = c.addNode(), nE1 = c.addNode();
    c.addCapacitor (in, nB1, 0.22e-6);
    c.addResistor (rail, nX, 120.0e3);
    c.addBjt (nX, nB1, nE1, false, bc109);
    // Emitter: 6.8K to ground, and 25 uF in series with the Gain rheostat to ground
    const auto nG1 = c.addNode();
    c.addResistor (nE1, gnd, 6.8e3);
    c.addCapacitor (nE1, nG1, 25.0e-6);
    ch.rGain = c.addResistor (nG1, gnd, 1.0e3);

    // ---- TR2: base = TR1's collector; 200 pF collector-base; emitter 470 ohm || 25 uF; collector 1.8K, 6.4 uF out ----
    const auto nC2 = c.addNode(), nE2 = c.addNode(), nF = c.addNode();
    c.addBjt (nC2, nX, nE2, false, bc109);
    c.addCapacitor (nC2, nX, 200.0e-12);
    c.addResistor (rail, nC2, 1.8e3);
    c.addResistor (nE2, gnd, 470.0);
    c.addCapacitor (nE2, gnd, 25.0e-6);
    c.addResistor (nE2, nB1, 150.0e3);          // DC feedback to TR1's base
    c.addCapacitor (nC2, nF, 6.4e-6);
    c.addResistor (nF, nE1, 12.0e3);            // feedback from TR2's output back to TR1's emitter

    // ---- the Baxandall network: input nF, far end nO (returned to TR3's collector through 25 uF), common node nW1 ----
    const auto nBa = c.addNode(), nBw = c.addNode(), nBb = c.addNode();
    const auto nTa = c.addNode(), nTw = c.addNode(), nTb = c.addNode();
    const auto nO = c.addNode(), nW1 = c.addNode();
    c.addResistor (nF, nBa, 4.7e3);
    ch.rBassLeft = c.addResistor (nBa, nBw, tonePotMax * 0.5);
    ch.rBassRight = c.addResistor (nBw, nBb, tonePotMax * 0.5);
    c.addCapacitor (nBa, nBb, 0.1e-6);           // across the whole Bass pot
    c.addResistor (nBb, nO, 4.7e3);
    c.addResistor (nBw, nW1, 39.0e3);
    c.addCapacitor (nF, nTa, 0.01e-6);
    ch.rTrebleLeft = c.addResistor (nTa, nTw, tonePotMax * 0.5);
    ch.rTrebleRight = c.addResistor (nTw, nTb, tonePotMax * 0.5);
    c.addCapacitor (nTb, nO, 0.01e-6);
    c.addResistor (nTw, nW1, 5.6e3);

    // ---- TR3: base from nW1 through 0.1 uF (150K/33K bias); collector 1.8K, and the network's far end via 25 uF ----
    const auto nB3 = c.addNode(), nC3 = c.addNode(), nE3 = c.addNode(), nOut = c.addNode();
    c.addCapacitor (nW1, nB3, 0.1e-6);
    c.addResistor (rail, nB3, 150.0e3);
    c.addResistor (nB3, gnd, 33.0e3);
    c.addBjt (nC3, nB3, nE3, false, bc109);
    c.addResistor (rail, nC3, 1.8e3);
    c.addResistor (nE3, gnd, 470.0);
    c.addCapacitor (nE3, gnd, 25.0e-6);
    c.addCapacitor (nO, nC3, 25.0e-6);
    c.addCapacitor (nC3, nOut, 0.22e-6);
    c.addResistor (nOut, gnd, outputLoadResistance);

    ch.nX = nX;
    ch.nC2 = nC2;
    ch.nE2 = nE2;
    ch.nC3 = nC3;
    ch.nE3 = nE3;
    ch.nOut = nOut;

    // Bias guesses: the values the drawing marks (TR2 and TR3: collector 5 V, emitter 1 V).
    c.setInitialGuess (nB1, 0.65);
    c.setInitialGuess (nX, 1.65);
    c.setInitialGuess (nE1, 0.0);
    c.setInitialGuess (nC2, 5.0);
    c.setInitialGuess (nE2, 1.0);
    c.setInitialGuess (nF, 1.0);
    c.setInitialGuess (nBa, 1.0);
    c.setInitialGuess (nBw, 1.0);
    c.setInitialGuess (nBb, 1.0);
    c.setInitialGuess (nTa, 1.0);
    c.setInitialGuess (nTw, 1.0);
    c.setInitialGuess (nTb, 1.0);
    c.setInitialGuess (nO, 1.0);
    c.setInitialGuess (nW1, 1.0);
    c.setInitialGuess (nB3, 1.65);
    c.setInitialGuess (nC3, 5.0);
    c.setInitialGuess (nE3, 1.0);
}

void OverdriverStyleOverdriveProcessor::updatePots (double gain, double bass, double treble)
{
    // Gain: a 10K rheostat in series with the emitter's 25 uF. Clockwise = more gain = less resistance (the pot's taper is not on
    // the drawing; linear is assumed).
    const double rGain = juce::jmax (1.0, gainPotMax * (1.0 - gain));

    // Bass / Treble: 100K linear. In a Baxandall network the wiper toward the INPUT end of the pot boosts and toward the output
    // end cuts. Clockwise = boost (assumed; checked in the tests against the network's actual response).
    const double left = juce::jmax (1.0, tonePotMax * (1.0 - bass)), leftT = juce::jmax (1.0, tonePotMax * (1.0 - treble));
    const double right = juce::jmax (1.0, tonePotMax - left), rightT = juce::jmax (1.0, tonePotMax - leftT);

    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rGain, rGain);
        ch.c.setResistance (ch.rBassLeft, left);
        ch.c.setResistance (ch.rBassRight, right);
        ch.c.setResistance (ch.rTrebleLeft, leftT);
        ch.c.setResistance (ch.rTrebleRight, rightT);
    }
}

void OverdriverStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedGain.reset (newSampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (gainParam->get());
    smoothedBass.reset (newSampleRate, 0.02);
    smoothedBass.setCurrentAndTargetValue (bassParam->get());
    smoothedTreble.reset (newSampleRate, 0.02);
    smoothedTreble.setCurrentAndTargetValue (trebleParam->get());

    updatePots (gainParam->get(), bassParam->get(), trebleParam->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void OverdriverStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedGain.setTargetValue (gainParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();
        const float b = smoothedBass.getNextValue();
        const float t = smoothedTreble.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (g, b, t);
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

void OverdriverStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
