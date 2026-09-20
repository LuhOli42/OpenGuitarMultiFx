#include "HM2StyleDistortionProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double v9 = 9.0;
    constexpr double v45 = 4.5; // 10K/10K divider + 47uF, treated as an ideal 4.5 V rail

    // Assumed device parameters (see the doc; both transistor stages are self-biased, so DC is forgiving).
    const NodalCircuit::JfetParams jfet2SK30 { 4.0e-3, -1.5, 0.02 };
    const NodalCircuit::BjtParams npn2SC2240 { 1.0e-14, 25.85e-3, 400.0, 4.0 };
    const NodalCircuit::BjtParams pnp2SA970 { 1.0e-14, 25.85e-3, 400.0, 4.0 };

    // 1N4148-class silicon; germanium ("GE.DIOD") ~ 1N34A-class as in the Centaur model.
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;
    constexpr double geIs = 200.0e-9;
    constexpr double geNVt = 1.3 * 25.85e-3;

    constexpr double distPotMax = 250.0e3;
    constexpr double tonePotMax = 10.0e3;
    constexpr double levelPotMax = 10.0e3;
}

HM2StyleDistortionProcessor::HM2StyleDistortionProcessor()
{
    auto dist = std::make_unique<juce::AudioParameterFloat> (
        "hm2_dist", "Dist", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto low = std::make_unique<juce::AudioParameterFloat> (
        "hm2_low", "Low", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto high = std::make_unique<juce::AudioParameterFloat> (
        "hm2_high", "High", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto level = std::make_unique<juce::AudioParameterFloat> (
        "hm2_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    distParam = dist.get();
    lowParam = low.get();
    highParam = high.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "hm2", "HM-2-Style Distortion", "|", std::move (dist));
    group->addChild (std::move (low));
    group->addChild (std::move (high));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void HM2StyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block 1
    // Input JFET follower, two self-biased transistor stages, and the DIST branch off Q6's collector.
    {
        auto& c = ch.b1;
        const auto in = c.addNode(), nv9 = c.addNode(), nv45 = c.addNode();
        const auto xin = c.addNode(), g1 = c.addNode(), s1 = c.addNode(), x2 = c.addNode();
        const auto b6 = c.addNode(), c6 = c.addNode(), e6 = c.addNode(), v9f = c.addNode();
        const auto x3 = c.addNode(), b7 = c.addNode(), e7 = c.addNode(), c7 = c.addNode();
        const auto l1 = c.addNode(), l2 = c.addNode(), r2n = c.addNode(), r3n = c.addNode();
        const auto n6 = c.addNode(), o1 = c.addNode();
        c.addSource (nv9, v9);
        c.addSource (nv45, v45);
        ch.srcIn = c.addSource (in, 0.0);

        c.addResistor (in, xin, 10.0e3);
        c.addCapacitor (xin, g1, 47.0e-9);
        c.addResistor (g1, nv45, 1.0e6);
        c.addFollower (g1, s1, -0.964);           // Q1 2SK30 / BF245 source follower (vgs -0.96 V at DC): a buffer
        c.addResistor (s1, gnd, 10.0e3);

        // Filtered supply for the transistor stages: 1K + 47uF.
        c.addResistor (nv9, v9f, 1.0e3);
        c.addCapacitor (v9f, gnd, 47.0e-6);

        // Q6 (NPN), self-biased from its collector through 470K || 10pF.
        c.addCapacitor (s1, x2, 47.0e-9);
        c.addResistor (x2, b6, 22.0e3);
        c.addResistor (b6, gnd, 100.0e3);
        c.addResistor (b6, c6, 470.0e3);
        c.addCapacitor (b6, c6, 10.0e-12);
        c.addBjt (c6, b6, e6, false, npn2SC2240);
        c.addResistor (e6, gnd, 22.0);
        c.addResistor (v9f, c6, 10.0e3);

        // Q7 (PNP), same idea, 120 ohm emitter degeneration, 10K collector load to ground.
        c.addCapacitor (c6, x3, 47.0e-9);
        c.addResistor (x3, b7, 22.0e3);
        c.addResistor (v9f, b7, 100.0e3);
        c.addResistor (v9f, e7, 120.0);
        c.addBjt (c7, b7, e7, true, pnp2SA970);
        c.addResistor (b7, c7, 470.0e3);
        c.addCapacitor (b7, c7, 10.0e-12);
        c.addResistor (c7, gnd, 10.0e3);
        c.addResistor (nv45, c7, 68.0e3);

        // DIST branch: 10uF + 150 ohm into the pot (wiper grounded: segment A loads the source,
        // segment B is the op-amp's gain leg), then 47K + 47nF into the op-amp's (-) node.
        c.addCapacitor (c6, l1, 10.0e-6);
        c.addResistor (l1, l2, 150.0);
        ch.rDistA = c.addResistor (l2, gnd, 125.0e3);
        ch.rDistB = c.addResistor (r2n, gnd, 125.0e3);
        c.addResistor (r2n, r3n, 47.0e3);
        c.addCapacitor (r3n, n6, 47.0e-9);

        // Clipping op-amp IC1B (ideal): (+) is Q7's collector; feedback = 1 diode one way, 2 in series the other,
        // 220K and 10 pF. It shares a block with the stages that drive it: the 47 nF into (-) has ~3.8 mS of
        // conductance and the op-amp multiplies its current by the 220K feedback, so ANY mismatch between the
        // two sides (an earlier version predicted the (-) node one sample ahead) is amplified ~840x into a glitch.
        c.addOpAmp (c7, n6, o1);
        c.addResistor (n6, o1, 220.0e3);
        c.addCapacitor (n6, o1, 10.0e-12);
        c.addDiode (n6, o1, siIs, siNVt);         // 1 diode: conducts when (-) is above the output
        c.addDiode (o1, n6, siIs, 2.0 * siNVt);   // 2 in series the other way = one diode with twice nVt (exact)

        ch.nC7 = c7;
        ch.nN6 = n6;
        ch.nO1 = o1;
        c.setInitialGuess (g1, v45); c.setInitialGuess (s1, 5.4); c.setInitialGuess (x2, 0.0);
        c.setInitialGuess (b6, 0.65); c.setInitialGuess (c6, 4.0); c.setInitialGuess (e6, 0.01);
        c.setInitialGuess (v9f, 8.0); c.setInitialGuess (b7, 7.3); c.setInitialGuess (e7, 7.9);
        c.setInitialGuess (c7, 3.0); c.setInitialGuess (x3, 4.0);
        c.setInitialGuess (l1, 0.0); c.setInitialGuess (r2n, 0.0); c.setInitialGuess (r3n, v45);
        c.setInitialGuess (n6, 3.0); c.setInitialGuess (o1, 3.0);
    }

    // ================================================================ block 2
    // Everything after IC1B's (ideal) output: C 1uF, then the germanium pair IN SERIES with the signal, the
    // silicon pair + 1nF shunt, and 1uF into IC1A. The op-amp output is an ideal source, so nothing here
    // loads block 1 back.
    {
        auto& c = ch.b2;
        const auto nv45 = c.addNode(), o1 = c.addNode();
        const auto na1 = c.addNode(), nx1 = c.addNode(), nx2 = c.addNode(), ny = c.addNode(), np = c.addNode();
        c.addSource (nv45, v45);
        ch.srcO1 = c.addSource (o1, 3.0);

        c.addCapacitor (o1, na1, 1.0e-6);
        c.addResistor (na1, nx1, 10.0e3);
        c.addDiode (nx1, nx2, geIs, geNVt);       // germanium pair, in series with the signal
        c.addDiode (nx2, nx1, geIs, geNVt);
        c.addResistor (nx2, ny, 10.0e3);
        c.addDiode (ny, gnd, siIs, siNVt);        // silicon pair to ground
        c.addDiode (gnd, ny, siIs, siNVt);
        c.addCapacitor (ny, gnd, 1.0e-9);
        c.addCapacitor (ny, np, 1.0e-6);
        c.addResistor (np, nv45, 68.0e3);

        ch.nP = np;
        c.setInitialGuess (np, v45);
    }

    // ================================================================ block 3
    // IC1A buffer -> 3.3K -> IC3A (differential op-amp) whose LOW/HIGH pot tracks run from its (+) node to its
    // (-) node; the pot wipers are fed by three follower-based gyrators (IC3B, IC2B, IC2A); then Level.
    {
        auto& c = ch.b3;
        const auto nv45 = c.addNode(), fin = c.addNode();
        const auto f = c.addNode(), n2 = c.addNode(), o3 = c.addNode(), wl = c.addNode(), wh = c.addNode();
        const auto pl = c.addNode(), ol = c.addNode(), ql = c.addNode();
        const auto pm = c.addNode(), om = c.addNode(), qm = c.addNode();
        const auto ph = c.addNode(), oh = c.addNode(), qh = c.addNode();
        const auto nc = c.addNode(), nlev = c.addNode(), out = c.addNode();
        c.addSource (nv45, v45);
        ch.srcFin = c.addSource (fin, v45);

        c.addResistor (fin, f, 3.3e3);
        // IC3A (M5218AL on 9 V: 20000 V/V minimum, output about 1.5 V short of each rail). It is the stage that
        // boosts the Color controls, so at full Low/High it really does run out of swing.
        NodalCircuit::OpAmpSpec m5218;
        m5218.lowRail = 1.5;
        m5218.highRail = v45 * 2.0 - 1.5;
        c.addSaturatingOpAmp (f, n2, o3, m5218);
        c.addResistor (n2, o3, 3.3e3);
        ch.rLowF = c.addResistor (f, wl, 5.0e3);  // LOW pot track: (+) node -> wiper
        ch.rLowN = c.addResistor (wl, n2, 5.0e3); //                wiper -> (-) node
        ch.rHighF = c.addResistor (f, wh, 5.0e3); // HIGH pot
        ch.rHighN = c.addResistor (wh, n2, 5.0e3);

        // Gyrator followers: (+) node biased by R to 4.5 V, C from (+) to the output-side node Q,
        // 330 ohm from the follower's output to Q, then the coupling cap into the pot wiper.
        c.addResistor (pl, nv45, 100.0e3);        // IC3B (LOW)
        c.addCapacitor (pl, ql, 68.0e-9);
        c.addOpAmp (pl, ol, ol);
        c.addResistor (ol, ql, 330.0);
        c.addCapacitor (ql, wl, 1.5e-6);

        c.addResistor (pm, nv45, 100.0e3);        // IC2B (HIGH, mid)
        c.addCapacitor (pm, qm, 4.7e-9);
        c.addOpAmp (pm, om, om);
        c.addResistor (om, qm, 330.0);
        c.addCapacitor (qm, wh, 0.1e-6);

        c.addResistor (ph, nv45, 82.0e3);         // IC2A (HIGH)
        c.addCapacitor (ph, qh, 6.8e-9);
        c.addOpAmp (ph, oh, oh);
        c.addResistor (oh, qh, 330.0);
        c.addCapacitor (qh, wh, 0.15e-6);

        c.addCapacitor (o3, nc, 10.0e-6);
        c.addResistor (nc, nlev, 10.0e3);
        ch.rLevelTop = c.addResistor (nlev, out, 5.0e3);
        ch.rLevelBottom = c.addResistor (out, gnd, 5.0e3);
        c.addResistor (out, gnd, 1.0e6);          // assumed downstream input impedance

        ch.nOut = out;
        for (auto n : { fin, f, n2, o3, wl, wh, pl, ol, ql, pm, om, qm, ph, oh, qh })
            c.setInitialGuess (n, v45);
    }
}

void HM2StyleDistortionProcessor::updatePots (double dist, double low, double high, double level)
{
    // DIST: 250K "log", wiper grounded, knob CW = wiper toward pin 3. A = pin 1 -> wiper, B = wiper -> pin 3.
    // Audio taper approximated as knob^2 (same documented stand-in as the other log pots).
    const double distFrac = dist * dist;
    const double rA = juce::jmax (1.0, distPotMax * distFrac);
    const double rB = juce::jmax (1.0, distPotMax * (1.0 - distFrac));

    // LOW / HIGH: 10K linear, centre = flat. Knob up moves the wiper toward the (-) node = boost.
    const double lowF = juce::jmax (1.0, tonePotMax * low), lowN = juce::jmax (1.0, tonePotMax * (1.0 - low));
    const double highF = juce::jmax (1.0, tonePotMax * high), highN = juce::jmax (1.0, tonePotMax * (1.0 - high));

    const double lBottom = juce::jmax (1.0, levelPotMax * level * level);
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.b1.setResistance (ch.rDistA, rA);
        ch.b1.setResistance (ch.rDistB, rB);
        ch.b3.setResistance (ch.rLowF, lowF);
        ch.b3.setResistance (ch.rLowN, lowN);
        ch.b3.setResistance (ch.rHighF, highF);
        ch.b3.setResistance (ch.rHighN, highN);
        ch.b3.setResistance (ch.rLevelTop, lTop);
        ch.b3.setResistance (ch.rLevelBottom, lBottom);
    }
}

void HM2StyleDistortionProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedDist.reset (newSampleRate, 0.02);
    smoothedDist.setCurrentAndTargetValue (distParam->get());
    smoothedLow.reset (newSampleRate, 0.02);
    smoothedLow.setCurrentAndTargetValue (lowParam->get());
    smoothedHigh.reset (newSampleRate, 0.02);
    smoothedHigh.setCurrentAndTargetValue (highParam->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (levelParam->get());

    updatePots (distParam->get(), lowParam->get(), highParam->get(), levelParam->get());

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.b1.prepare (newSampleRate) && dcOk;
        ch.b2.setSource (ch.srcO1, ch.b1.voltage (ch.nO1));
        dcOk = ch.b2.prepare (newSampleRate) && dcOk;
        ch.b3.setSource (ch.srcFin, ch.b2.voltage (ch.nP));
        dcOk = ch.b3.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true; // freshly built channels hold identical state
    channel1Stale = false;
    identicalRun = 0;
}

void HM2StyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    // Dual-mono shortcut: identical INPUT does not imply identical OUTPUT unless the two circuits also have
    // identical STATE (capacitor charges), so it is used only while the channels are known to be in sync.
    // While it runs, channel 1's circuit is left behind; it is caught up by copying channel 0's the moment the
    // channels diverge. After a divergence the shortcut stays off until the input has been identical for 10 s,
    // by which time the circuits' memory has decayed enough that re-syncing by copy is inaudible.
    const bool dualMono = numChannels == 2 && blockIsDualMono (buffer);
    bool useShortcut = false;
    if (numChannels == 2)
    {
        identicalRun = dualMono ? identicalRun + numSamples : 0;

        if (! channelsSynced && identicalRun >= (long long) (10.0 * sampleRate))
        {
            channels[1] = channels[0];
            channelsSynced = true;
            channel1Stale = false;
        }

        useShortcut = dualMono && channelsSynced;

        if (! useShortcut && channel1Stale)
        {
            channels[1] = channels[0];
            channel1Stale = false;
        }

        if (! dualMono)
            channelsSynced = false;
    }
    const int solveChannels = useShortcut ? 1 : numChannels;
    if (useShortcut)
        channel1Stale = true;

    smoothedDist.setTargetValue (distParam->get());
    smoothedLow.setTargetValue (lowParam->get());
    smoothedHigh.setTargetValue (highParam->get());
    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDist.getNextValue();
        const float lo = smoothedLow.getNextValue();
        const float hi = smoothedHigh.getNextValue();
        const float lv = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, lo, hi, lv);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.b1.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.b1.solveSample();
            if (! ok && chIdx == 0) ++perBlockFailures[0];

            ch.b2.setSource (ch.srcO1, ch.b1.voltage (ch.nO1));
            const bool ok2 = ch.b2.solveSample();
            if (! ok2 && chIdx == 0) ++perBlockFailures[1];
            ok = ok2 && ok;

            ch.b3.setSource (ch.srcFin, ch.b2.voltage (ch.nP));
            const bool ok3 = ch.b3.solveSample();
            if (! ok3 && chIdx == 0) ++perBlockFailures[2];
            ok = ok3 && ok;

            data[i] = (float) ch.b3.voltage (ch.nOut);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void HM2StyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The DS-1-Style Distortion's placeholder glyph is the overdrive icon (the sheet's "Distortion" glyph is
    // not saved in the repo -- see docs/icons/AGENT-icon-notes.md); reuse it here for the same reason.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
