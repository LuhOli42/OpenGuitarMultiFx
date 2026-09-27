#include "SqueezerStyleCompressorProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vcc = 9.0;

    // 2N5457 (Q2): a wide spread (cut-off -0.5 .. -6 V, Idss 1 .. 5 mA; DryBell): a typical part, Vp -2 V, Idss 2.5 mA. The trimmer
    // makes up for the spread on the real pedal.
    const NodalCircuit::JfetParams jfet2N5457 { 2.5e-3, -2.0, 0.02 };

    // 1N100 germanium: "forward voltage below 200 mV for currents below 1 mA" (DryBell): Is 2.5 uA, N 1.3 gives 0.2 V at 1 mA.
    constexpr double geIs = 2.5e-6;
    constexpr double geNVt = 1.3 * 25.85e-3;

    // JRC4558: 100 dB, 3 MHz, ~75 ohm out, swing ~1.5 V short of each rail of the 9 V supply (DryBell: "output signal swing +3 / -4.2 V").
    const NodalCircuit::OpAmpMacro jrc4558 { 1.0e5, 3.0e6, 75.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double q1Current = 500.0e-6;  // Q1 + R10 2K2: "around 500 uA"
    constexpr double biasPotMax = 10.0e3;   // VR2
    constexpr double volumePotMax = 10.0e3; // VR1 10K (the output attenuator)
    constexpr double downstreamLoad = 1.0e6;
}

SqueezerStyleCompressorProcessor::SqueezerStyleCompressorProcessor()
{
    auto vol = std::make_unique<juce::AudioParameterFloat> ("squeezer_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto bia = std::make_unique<juce::AudioParameterFloat> ("squeezer_bias", "Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    volume = vol.get();
    bias = bia.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("squeezer", "Orange Squeezer-Style Compressor", "|", std::move (vol), std::move (bia));
}

void SqueezerStyleCompressorProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    auto& c = ch.c;

    const auto v9 = c.addNode(), in = c.addNode(), n1 = c.addNode(), n2 = c.addNode(), nGate = c.addNode(), nB = c.addNode();
    const auto nY = c.addNode(), nC4 = c.addNode(), plus = c.addNode(), minus = c.addNode(), nR7 = c.addNode(), op = c.addNode(), oc = c.addNode();
    const auto nD = c.addNode(), wiper = c.addNode();
    c.addSource (v9, vcc);
    ch.srcIn = c.addSource (in, 0.0);

    // C1 47 nF, R2 82K into the divider node N2
    c.addCapacitor (in, n1, 47.0e-9);
    c.addResistor (n1, n2, 82.0e3);

    // Q2 as the lower leg of the divider: drain on N2, source on the bias node B (4u7 to ground, VR2 to ground, Q1's 500 uA into it),
    // gate on the control node G: R9 470K to the integrator, and 2n2 + R4 470K from the drain (the gate-drain feedback that
    // linearises a JFET resistor)
    c.addJfet (n2, nGate, nB, jfet2N5457);
    c.addCurrentSource (nB, q1Current);
    c.addCapacitor (nB, gnd, 4.7e-6);
    ch.rBias = c.addResistor (nB, gnd, 4.0e3);
    c.addCapacitor (n2, nC4, 2.2e-9);
    c.addResistor (nC4, nGate, 470.0e3);
    c.addResistor (nGate, nY, 470.0e3);

    // The amplifier: C2 47 nF into the (+) pin (R1 390K to +9 V, R5 470K to ground); R6 220K feedback, R7 10K + 4u7 in the (-) leg: x23
    c.addCapacitor (n2, plus, 47.0e-9);
    c.addResistor (plus, v9, 390.0e3);
    c.addResistor (plus, gnd, 470.0e3);
    c.addOpAmpMacro (plus, minus, op, jrc4558);
    c.addResistor (minus, op, 220.0e3);
    c.addResistor (minus, nR7, 10.0e3);
    c.addCapacitor (nR7, gnd, 4.7e-6);

    // C3 4u7 to the output node; the rectifier takes it through R3 1K5 and the germanium D1 (anode towards the output) into C6 4u7 || R8 100K
    c.addCapacitor (op, oc, 4.7e-6);
    c.addResistor (oc, nD, 1.5e3);
    c.addDiode (nD, nY, geIs, geNVt);
    c.addCapacitor (nY, gnd, 4.7e-6);
    c.addResistor (nY, gnd, 100.0e3);

    // VR1, the Volume, into the load
    ch.rVolTop = c.addResistor (oc, wiper, 1.0e3);
    ch.rVolBottom = c.addResistor (wiper, gnd, 1.0e3);
    c.addResistor (wiper, gnd, downstreamLoad);

    ch.nOp = op;
    ch.nControl = nY;
    ch.nDivider = n2;
    ch.nOut = wiper;

    for (auto n : { plus, minus, op })
        c.setInitialGuess (n, vcc * 470.0 / 860.0);
    c.setInitialGuess (nR7, 0.0);
    c.setInitialGuess (nB, 2.0);
    for (auto n : { n1, n2, nGate, nY, nC4, oc, nD, wiper })
        c.setInitialGuess (n, 0.0);
}

void SqueezerStyleCompressorProcessor::updatePots (double volumeKnob, double biasKnob)
{
    // Bias: the VR2 trimmer (a 10K rheostat from Q2's source to ground): Q1's 500 uA across it sets Vgs. 0 = 0 V (Q2 fully on, all
    // attenuation), 1 = 5 V (Q2 pinched off, no attenuation).
    const double rBias = juce::jmax (1.0, biasPotMax * biasKnob);

    // Volume: 10K "log" ("A") pot on the output.
    const double lBottom = juce::jmax (1.0, volumePotMax * pots::audio (volumeKnob));
    const double lTop = juce::jmax (1.0, volumePotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rBias, rBias);
        ch.c.setResistance (ch.rVolTop, lTop);
        ch.c.setResistance (ch.rVolBottom, lBottom);
    }
}

void SqueezerStyleCompressorProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volume->get());
    updatePots (volume->get(), bias->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void SqueezerStyleCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);
    smoothedVolume.setTargetValue (volume->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float v = smoothedVolume.getNextValue();
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (v, bias->get());
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

void SqueezerStyleCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The sheet's Compressor glyph (Dinamica): the pulse trace.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
