#include "ODB3StyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    // The Boss regulator (DC jack -> Q16 capacitance multiplier -> V+, R61 / R62 10k / 10k -> Vref): the same ~8 V /
    // 4 V the BD-2 model runs on.
    constexpr double vcc = 8.0;
    constexpr double vb = 4.0;

    // The discrete gain stages use the BD-2's parts (BD2StyleOverdriveProcessor.cpp has the datasheet notes).
    const NodalCircuit::JfetParams jfet2SK184 { 4.5e-3, -1.2, 0.02 };
    const NodalCircuit::BjtParams pnp2SA1049 { 1.0e-14, 25.85e-3, 300.0, 4.0 };

    // D2 / D3: 3 mm red LEDs (~1.7 V at 1 mA).
    constexpr double ledIs = 6.0e-12, ledNVt = 0.09;
    // D9 1SS133 (~1N4148).
    constexpr double siIs = 2.52e-9, siNVt = 1.752 * 25.85e-3;

    // reducedOrder: each discrete stage as the BD-2's op-amp macro (finite gain + common-mode term, one pole, a
    // saturating output behind the collector load), with this pedal's DC points. Fitted against the transistor-level
    // stages here (Tests/ODB3StyleOverdriveProcessorTests.cpp, "reduced matches the transistor-level reference").
    constexpr double macroGain = 251.0 * 1.147;
    constexpr double macroCommonMode = 0.0228;
    constexpr double macroLowRail = 0.05, macroHighRail = 8.0;
    constexpr double stage2HighRail = 7.3; // the Q8 collector into R18 + R19 tops out lower (fitted, see above)
    constexpr double macroPole1 = 30.0e3, macroPole2 = 30.0e3;
    constexpr double stage1Dc = 4.16, stage2PlusDc = 4.16, stage2Dc = 4.67;

    constexpr double switchOnResistance = 100.0; // the JFET bypass switches, on
    constexpr double gainPotMax = 250.0e3;       // VR1a / VR1b 250k log, ganged
    constexpr double balancePotMax = 250.0e3;    // VR2 250k lin
    constexpr double tonePotMax = 50.0e3;        // VR3a / VR3b 50k (W)
    constexpr double levelPotMax = 50.0e3;       // VR4 50k log
    constexpr double potMin = 1.0;
}

ODB3StyleOverdriveProcessor::ODB3StyleOverdriveProcessor()
{
    auto makeParam = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    // Front-panel order on the real pedal: LEVEL, HIGH, LOW, BALANCE, GAIN.
    auto levelParam = makeParam ("odb3_level", "Level");
    auto highParam = makeParam ("odb3_high", "High");
    auto lowParam = makeParam ("odb3_low", "Low");
    auto balanceParam = makeParam ("odb3_balance", "Balance");
    auto gainParam = makeParam ("odb3_gain", "Gain");

    level = levelParam.get();
    high = highParam.get();
    low = lowParam.get();
    balance = balanceParam.get();
    gain = gainParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "odb3", "ODB-3", "|", std::move (levelParam));
    group->addChild (std::move (highParam));
    group->addChild (std::move (lowParam));
    group->addChild (std::move (balanceParam));
    group->addChild (std::move (gainParam));
    parameters = std::move (group);
}

void ODB3StyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ A: input follower + effect-path coupling
    {
        auto& c = ch.a;
        const auto in = c.addNode(), x1 = c.addNode(), g1 = c.addNode(), s1 = c.addNode(), n2 = c.addNode(),
                   n2b = c.addNode(), g3 = c.addNode(), v4 = c.addNode();
        c.addSource (v4, vb);
        ch.srcIn = c.addSource (in, 0.0);
        c.addResistor (in, x1, 10.0e3);           // R1
        c.addCapacitor (x1, g1, 47.0e-9);         // C1
        c.addResistor (g1, v4, 1.0e6);            // R2
        c.addFollower (g1, s1, -0.819);           // Q1 2SK184 source follower
        c.addResistor (s1, gnd, 10.0e3);          // R3
        c.addCapacitor (s1, n2, 0.1e-6);          // C2
        c.addResistor (n2, v4, 1.0e6);            // R4
        c.addResistor (n2, n2b, switchOnResistance); // Q2 2SK118 (on)
        c.addResistor (n2b, v4, 1.0e6);           // R5
        c.addCapacitor (n2b, g3, 0.1e-6);         // C3
        c.addResistor (g3, v4, 100.0e3);          // R6
        ch.nG1 = g1;
        ch.nS1 = s1;
        ch.nG3 = g3;
        for (auto n : { x1, g1, n2, n2b, g3 })
            c.setInitialGuess (n, vb);
        c.setInitialGuess (s1, vb + 0.819);
    }

    // ================================================================ B: the two discrete gain stages + bridged-T
    {
        auto& c = ch.b;
        const auto v8 = c.addNode(), g3 = c.addNode();
        c.addSource (v8, vcc);
        ch.srcB = c.addSource (g3, vb);

        // Stage 1: Q3 / Q4 2SK184 pair (R8 2k2 drain, R7 4k7 tail) driving Q5 2SA1049 (R11 2k2 collector load).
        // Feedback: the collector -> R10 2k2 + VR1a (250k log) -> Q4's gate, with R9 1k5 + C4 10u to ground and C5
        // 330p across the feedback leg.
        const auto f1 = c.addNode(), n9 = c.addNode(), nr10 = c.addNode(), o1 = c.addNode();
        const auto macroStage = [&] (NodalCircuit::Node plus, NodalCircuit::Node minus, NodalCircuit::Node out,
                                     double plusDc, double outDc, double pole, double load, double highRail)
        {
            const auto amp = c.addNode(), px = c.addNode(), ob = c.addNode();
            c.addFiniteGainOpAmp (plus, minus, amp, macroGain,
                                  (1.0 + macroCommonMode) * plusDc - (1.0 - macroCommonMode) * outDc - outDc / macroGain,
                                  macroCommonMode);
            c.addResistor (amp, px, 1.0e3);
            c.addCapacitor (px, gnd, 1.0 / (2.0 * juce::MathConstants<double>::pi * 1.0e3 * pole));
            c.addSaturatingOpAmp (px, ob, ob, { macroLowRail, highRail });
            c.addResistor (ob, out, load);
            c.setInitialGuess (amp, outDc); c.setInitialGuess (px, outDc); c.setInitialGuess (ob, outDc);
        };
        if (reducedOrder)
            macroStage (g3, f1, o1, vb, stage1Dc, macroPole1, 2.2e3, macroHighRail);
        else
        {
            const auto d3 = c.addNode(), t1 = c.addNode();
            c.addJfet (d3, g3, t1, jfet2SK184);
            c.addResistor (v8, d3, 2.2e3);
            c.addResistor (t1, gnd, 4.7e3);
            c.addJfet (v8, f1, t1, jfet2SK184, 5.0);
            c.addBjt (o1, d3, v8, true, pnp2SA1049);
            c.addResistor (o1, gnd, 2.2e3);
            c.setInitialGuess (d3, 7.3); c.setInitialGuess (t1, 4.9);
        }
        c.addResistor (o1, nr10, 2.2e3);
        ch.rGain1 = c.addResistor (nr10, f1, 0.5 * gainPotMax);
        c.addCapacitor (o1, f1, 330.0e-12);
        c.addResistor (f1, n9, 1.5e3);
        c.addCapacitor (n9, gnd, 10.0e-6);

        // Stage 2, DC-coupled from stage 1's collector: Q6 / Q7 pair (R14 2k2, R15 4k7) driving Q8 (R18 1k5 + R19
        // 820 collector load). Feedback: the collector -> (C8 330p || D2 / D3 red LEDs anti-parallel || R17 100k) ->
        // Q7's gate, with R16 1k8 + C7 100n to ground.
        const auto f2 = c.addNode(), n16 = c.addNode(), o2 = c.addNode(), m = c.addNode();
        if (reducedOrder)
            macroStage (o1, f2, o2, stage2PlusDc, stage2Dc, macroPole2, 1.0, stage2HighRail); // R18 + R19 stay the load
        else
        {
            const auto d6 = c.addNode(), t2 = c.addNode();
            c.addJfet (d6, o1, t2, jfet2SK184);
            c.addResistor (v8, d6, 2.2e3);
            c.addResistor (t2, gnd, 4.7e3);
            c.addJfet (v8, f2, t2, jfet2SK184, 5.0);
            c.addBjt (o2, d6, v8, true, pnp2SA1049);
            c.setInitialGuess (d6, 7.3); c.setInitialGuess (t2, 5.1);
        }
        c.addResistor (o2, m, 1.5e3);
        c.addResistor (m, gnd, 820.0);
        c.addCapacitor (o2, f2, 330.0e-12);
        c.addDiode (o2, f2, ledIs, ledNVt, 4.0e-9);
        c.addDiode (f2, o2, ledIs, ledNVt, 4.0e-9);
        c.addResistor (o2, f2, 100.0e3);
        c.addResistor (f2, n16, 1.8e3);
        c.addCapacitor (n16, gnd, 0.1e-6);

        // From the R18 / R19 tap: R20 3k3 -> C26 2u2 -> the bridged-T (C10 33n across, R21 / R22 4k7 to C11 0.22u)
        // -> R28 47k to ground and the Balance pot's drive end.
        const auto n20 = c.addNode(), ba = c.addNode(), bc = c.addNode(), bn = c.addNode();
        c.addResistor (m, n20, 3.3e3);
        c.addCapacitor (n20, ba, 2.2e-6);
        c.addCapacitor (ba, bn, 33.0e-9);
        c.addResistor (ba, bc, 4.7e3);
        c.addResistor (bn, bc, 4.7e3);
        c.addCapacitor (bc, gnd, 0.22e-6);
        c.addResistor (bn, gnd, 47.0e3);

        ch.nO1 = o1;
        ch.nO2 = o2;
        ch.nBn = bn;
        c.setInitialGuess (g3, vb);
        c.setInitialGuess (f1, 4.2); c.setInitialGuess (o1, 4.2); c.setInitialGuess (nr10, 4.2);
        c.setInitialGuess (n9, 4.2);
        c.setInitialGuess (f2, 4.4);
        c.setInitialGuess (o2, 4.4); c.setInitialGuess (m, 1.6); c.setInitialGuess (n16, 4.4);
        c.setInitialGuess (n20, 1.6);
        for (auto n : { ba, bc, bn })
            c.setInitialGuess (n, 0.0);
    }

    // ================================================================ C: the clean op-amp IC1b
    // Non-inverting from the stage-1 input node: R27 68k + C14 1u to ground from (-), R28 47k + VR1b (the Gain pot's
    // second gang) || C15 10p as feedback, D9 1SS133 from (-) to (+).
    {
        auto& c = ch.c;
        const auto plus = c.addNode(), minus = c.addNode(), out = c.addNode(), n27 = c.addNode(), nfb = c.addNode();
        ch.srcCPlus = c.addSource (plus, vb);
        NodalCircuit::OpAmpSpec njm4558;
        njm4558.lowRail = 1.0;
        njm4558.highRail = vcc - 1.0;
        c.addSaturatingOpAmp (plus, minus, out, njm4558);
        c.addResistor (minus, n27, 68.0e3);
        c.addCapacitor (n27, gnd, 1.0e-6);
        c.addResistor (out, nfb, 47.0e3);
        ch.rGain2 = c.addResistor (nfb, minus, 0.5 * gainPotMax);
        c.addCapacitor (out, minus, 10.0e-12);
        c.addDiode (minus, plus, siIs, siNVt);
        ch.nClean = out;
        for (auto n : { plus, minus, out, n27, nfb })
            c.setInitialGuess (n, vb);
    }

    // ================================================================ D: Balance, tone, Level, output
    {
        auto& c = ch.d;
        const auto v4 = c.addNode(), clean = c.addNode(), drive = c.addNode(), bw = c.addNode();
        c.addSource (v4, vb);
        ch.srcDClean = c.addSource (clean, vb);
        ch.srcDDrive = c.addSource (drive, 0.0);

        // VR2 Balance 250k lin: clean (IC1b) at one end, the drive path at the other.
        ch.rBalTop = c.addResistor (clean, bw, 0.5 * balancePotMax);
        ch.rBalBot = c.addResistor (bw, drive, 0.5 * balancePotMax);

        // R29 10k -> C16 1u -> IC1a's (+), R34 100k to Vref. Low (VR3a) and High (VR3b) span (+) and (-); the Low wiper
        // drives C19 0.15u into the Q9 gyrator (R31 3k3 to its emitter, C20 47n to its base, R33 100k base bias, R35
        // 10k emitter load); the High wiper R30 2k2 + C18 15n. R35' 22k || C17 47p feedback.
        const auto n29 = c.addNode(), p = c.addNode(), mi = c.addNode(), o = c.addNode();
        c.addResistor (bw, n29, 10.0e3);
        c.addCapacitor (n29, p, 1.0e-6);
        c.addResistor (p, v4, 100.0e3);
        NodalCircuit::OpAmpSpec njm4558;
        njm4558.lowRail = 1.0;
        njm4558.highRail = vcc - 1.0;
        c.addSaturatingOpAmp (p, mi, o, njm4558);
        c.addResistor (o, mi, 22.0e3);
        c.addCapacitor (o, mi, 47.0e-12);

        const auto lw = c.addNode(), lg = c.addNode(), lb = c.addNode(), le = c.addNode();
        ch.rLowPlus = c.addResistor (p, lw, 0.5 * tonePotMax);
        ch.rLowMinus = c.addResistor (lw, mi, 0.5 * tonePotMax);
        c.addCapacitor (lw, lg, 0.15e-6);
        c.addCapacitor (lg, lb, 47.0e-9);
        c.addResistor (lb, v4, 100.0e3);
        c.addFollower (lb, le, 0.6);              // Q9 2SC2459 emitter follower
        c.addResistor (lg, le, 3.3e3);
        c.addResistor (le, gnd, 10.0e3);

        const auto hw = c.addNode(), hr = c.addNode();
        ch.rHighPlus = c.addResistor (p, hw, 0.5 * tonePotMax);
        ch.rHighMinus = c.addResistor (hw, mi, 0.5 * tonePotMax);
        c.addResistor (hw, hr, 2.2e3);
        c.addCapacitor (hr, gnd, 15.0e-9);

        // C21 1u -> R44 10k -> VR4 Level (50k log) -> C22 1u -> Q10 switch (R36 / R37 1M) -> C23 1u -> Q9' 2SC2459
        // follower (R40 100k bias, R41 10k emitter) -> C24 1u -> R43 1k -> out (R42 100k).
        const auto c21 = c.addNode(), lt = c.addNode(), lvw = c.addNode(), s22 = c.addNode(), s22b = c.addNode(),
                   b9 = c.addNode(), e9 = c.addNode(), c24 = c.addNode(), out = c.addNode();
        c.addCapacitor (o, c21, 1.0e-6);
        c.addResistor (c21, lt, 10.0e3);
        ch.rLevelTop = c.addResistor (lt, lvw, 0.5 * levelPotMax);
        ch.rLevelBot = c.addResistor (lvw, gnd, 0.5 * levelPotMax);
        c.addCapacitor (lvw, s22, 1.0e-6);
        c.addResistor (s22, v4, 1.0e6);
        c.addResistor (s22, s22b, switchOnResistance);
        c.addResistor (s22b, v4, 1.0e6);
        c.addCapacitor (s22b, b9, 1.0e-6);
        c.addResistor (b9, v4, 100.0e3);
        c.addFollower (b9, e9, 0.6);
        c.addResistor (e9, gnd, 10.0e3);
        c.addCapacitor (e9, c24, 1.0e-6);
        c.addResistor (c24, out, 1.0e3);
        c.addResistor (out, gnd, 100.0e3);
        ch.nOut = out;

        for (auto n : { clean, bw, n29, p, mi, o, lw, lg, lb, hw, hr, s22, s22b, b9 })
            c.setInitialGuess (n, vb);
        c.setInitialGuess (le, vb - 0.6);
        c.setInitialGuess (e9, vb - 0.6);
        for (auto n : { drive, c21, lt, lvw, c24, out })
            c.setInitialGuess (n, 0.0);
    }
}

void ODB3StyleOverdriveProcessor::updatePots (float levelValue, float highValue, float lowValue, float balanceValue,
                                              float gainValue)
{
    // Gain: VR1a / VR1b, 250k log, ganged -- more resistance = more gain in both stages.
    const double g = gainPotMax * pots::audio ((double) gainValue);
    // Balance up = toward the drive path.
    const double bal = juce::jlimit (0.0, 1.0, (double) balanceValue);
    // Low / High up = boost: the wiper toward the (-) side.
    const double lo = juce::jlimit (0.0, 1.0, (double) lowValue);
    const double hi = juce::jlimit (0.0, 1.0, (double) highValue);
    const double lv = levelPotMax * pots::audio ((double) levelValue);

    for (auto& ch : channels)
    {
        ch.b.setResistance (ch.rGain1, juce::jmax (potMin, g));
        ch.c.setResistance (ch.rGain2, juce::jmax (potMin, g));
        ch.d.setResistance (ch.rBalTop, juce::jmax (potMin, balancePotMax * bal));
        ch.d.setResistance (ch.rBalBot, juce::jmax (potMin, balancePotMax * (1.0 - bal)));
        ch.d.setResistance (ch.rLowPlus, juce::jmax (potMin, tonePotMax * lo));
        ch.d.setResistance (ch.rLowMinus, juce::jmax (potMin, tonePotMax * (1.0 - lo)));
        ch.d.setResistance (ch.rHighPlus, juce::jmax (potMin, tonePotMax * hi));
        ch.d.setResistance (ch.rHighMinus, juce::jmax (potMin, tonePotMax * (1.0 - hi)));
        ch.d.setResistance (ch.rLevelTop, juce::jmax (potMin, levelPotMax - lv));
        ch.d.setResistance (ch.rLevelBot, juce::jmax (potMin, lv));
    }
}

bool ODB3StyleOverdriveProcessor::solve (Channel& ch, double x)
{
    ch.a.setSource (ch.srcIn, x);
    bool ok = ch.a.solveSample();
    const double g3 = ch.a.voltage (ch.nG3);
    ch.b.setSource (ch.srcB, g3);
    ok = ch.b.solveSample() && ok;
    ch.c.setSource (ch.srcCPlus, g3);
    ok = ch.c.solveSample() && ok;
    ch.d.setSource (ch.srcDClean, ch.c.voltage (ch.nClean));
    ch.d.setSource (ch.srcDDrive, ch.b.voltage (ch.nBn));
    ok = ch.d.solveSample() && ok;
    return ok;
}

void ODB3StyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    auto setup = [&] (juce::SmoothedValue<float>& sv, juce::AudioParameterFloat* p)
    {
        sv.reset (newSampleRate, 0.02);
        sv.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedLevel, level);
    setup (smoothedHigh, high);
    setup (smoothedLow, low);
    setup (smoothedBalance, balance);
    setup (smoothedGain, gain);
    updatePots (level->get(), high->get(), low->get(), balance->get(), gain->get());

    // Each block in signal order, fed the DC its driving node has upstream.
    for (auto& ch : channels)
    {
        ch.a.prepare (newSampleRate);
        const double g3 = ch.a.voltage (ch.nG3);
        ch.b.setSource (ch.srcB, g3);
        ch.b.prepare (newSampleRate);
        ch.c.setSource (ch.srcCPlus, g3);
        ch.c.prepare (newSampleRate);
        ch.d.setSource (ch.srcDClean, ch.c.voltage (ch.nClean));
        ch.d.setSource (ch.srcDDrive, ch.b.voltage (ch.nBn));
        ch.d.prepare (newSampleRate);
        ch.a.saveDynamicState (ch.aRest);
        ch.b.saveDynamicState (ch.bRest);
        ch.c.saveDynamicState (ch.cRest);
        ch.d.saveDynamicState (ch.dRest);
    }

    controlCounter = 0;
    solveCount = 0;
    solveFailures = 0;
    channelsSynced = true;
    channel1Stale = false;
    identicalRun = 0;
}

void ODB3StyleOverdriveProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.a.restoreDynamicState (ch.aRest);
        ch.b.restoreDynamicState (ch.bRest);
        ch.c.restoreDynamicState (ch.cRest);
        ch.d.restoreDynamicState (ch.dRest);
        ch.failStreak = 0;
    }
}

void ODB3StyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    // Dual-mono shortcut, same rules as the BD-2 (identical input only means identical output with identical state).
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

    smoothedLevel.setTargetValue (level->get());
    smoothedHigh.setTargetValue (high->get());
    smoothedLow.setTargetValue (low->get());
    smoothedBalance.setTargetValue (balance->get());
    smoothedGain.setTargetValue (gain->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float lv = smoothedLevel.getNextValue();
        const float hi = smoothedHigh.getNextValue();
        const float lo = smoothedLow.getNextValue();
        const float ba = smoothedBalance.getNextValue();
        const float ga = smoothedGain.getNextValue();
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (lv, hi, lo, ba, ga);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);
            const double x = std::isfinite (data[i]) ? (double) data[i] : 0.0;
            const bool ok = solve (ch, x);
            if (ok)
                ch.failStreak = 0;
            else if (++ch.failStreak >= 4)
            {
                ch.a.restoreDynamicState (ch.aRest);
                ch.b.restoreDynamicState (ch.bRest);
                ch.c.restoreDynamicState (ch.cRest);
                ch.d.restoreDynamicState (ch.dRest);
                ch.failStreak = 0;
            }
            const double y = ch.d.voltage (ch.nOut);
            data[i] = std::isfinite (y) ? (float) y : 0.0f;
            if (chIdx == 0)
            {
                ++solveCount;
                if (! ok)
                    ++solveFailures;
            }
        }
    }

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

ODB3StyleOverdriveProcessor::DebugBiasPoint ODB3StyleOverdriveProcessor::getDebugBiasPoint() const noexcept
{
    return { (float) channels[0].a.voltage (channels[0].nG1), (float) channels[0].a.voltage (channels[0].nS1), (float) vcc };
}

double ODB3StyleOverdriveProcessor::debugStage1Out() const noexcept { return channels[0].b.voltage (channels[0].nO1); }
double ODB3StyleOverdriveProcessor::debugStage2Out() const noexcept { return channels[0].b.voltage (channels[0].nO2); }

void ODB3StyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
