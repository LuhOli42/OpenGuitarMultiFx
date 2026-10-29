#include "SuperSonicStyleAmplifierProcessor.h"
#include "IconKit.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace tubeamp;

    // ---- supply (sheet 2): solid-state bridge -> B+ (plates, +420) -> L1 -> Z (screens, +418) -> R148 4.7K -> Y (PI, +376)
    // -> R150 10K -> X (V2, +317) -> R151 10K -> W (V1 / V4, +280). Every rail is printed. ----
    constexpr double railPlatesNominal = 420.0;
    constexpr double rectifierResistance = 60.0;   // bridge + PT winding: not printed
    constexpr double chokeResistance = 100.0, chokeInductance = 4.0;
    constexpr double screenResistor = 470.0;       // R142 / R143, one per tube
    constexpr double biasSupplyVolts = -43.7;      // TP9, the printed bias setting

    // ---- tubes: Koren's published sets, kg1 fitted to this drawing's printed operating point (the PI's cathodes at
    // 70.3 V, 24 mV across each 6V6's 1 ohm cathode resistor = 24 mA at -43.7 V bias) ----
    KorenTriode::Parameters triode12AX7() { return {}; }
    KorenTriode::Parameters triode12AT7()
    {
        KorenTriode::Parameters p;
        p.mu = 60.0;
        p.kg1 = 1700.0;  // published 272
        p.kp = 225.0;
        p.kvb = 54.7;
        p.ex = 1.5;
        return p;
    }
    KorenPentode::Parameters pentode6V6()
    {
        KorenPentode::Parameters p;
        p.mu = 10.0;
        p.ex = 1.35;
        p.kg1 = 1150.0;  // published 1460
        p.kg2 = 4500.0;
        p.kp = 48.5;
        p.kvb = 12.0;
        return p;
    }
    constexpr double cgp = 1.7e-12;

    // Output transformer (P/N 0041318000, not specified): 2 x 6V6GT into ~6.6K plate to plate, 8 ohm secondary.
    constexpr double primaryHalfInductance = 5.0;
    constexpr double halfToSecondaryTurns = 14.36;
    constexpr double couplingHalves = 0.9997, couplingSecondary = 0.997;
    constexpr double primaryHalfResistance = 90.0, secondaryResistance = 0.2;
    constexpr double feedbackResistor = 820.0;     // R132, speaker -> the PI's feedback node (R133 47R to ground)
    constexpr double speakerEddyLoss = 60.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1;
    constexpr double powerTheta = 0.9;

    // Reduced-order sag: the Deluxe Reverb's measured 6V6 table, scaled to this amp's 420 V rail.
    constexpr int bmSagPoints = 17;
    constexpr double bmSagDrive[bmSagPoints] = { 0.000425, 0.002122, 0.006367, 0.014859, 0.025478, 0.042476, 0.063733,
                                                  0.106259, 0.170016, 0.255145, 0.383284, 0.597788, 0.855871, 1.157292,
                                                  1.387650, 1.472226, 1.487870 };
    constexpr double bmSagRailDr[bmSagPoints] = { 415.08, 415.08, 415.07, 415.02, 414.90, 414.57, 413.94,
                                                   412.04, 408.15, 404.50, 401.99, 400.61, 400.01, 399.65,
                                                   399.39, 399.21, 399.12 };
    double sagRail (double drivePeak) noexcept
    {
        constexpr double scale = railPlatesNominal / 415.08;
        if (drivePeak <= bmSagDrive[0])
            return bmSagRailDr[0] * scale;
        if (drivePeak >= bmSagDrive[bmSagPoints - 1])
            return bmSagRailDr[bmSagPoints - 1] * scale;
        int i = 0;
        while (i < bmSagPoints - 2 && bmSagDrive[i + 1] < drivePeak)
            ++i;
        const double t = (drivePeak - bmSagDrive[i]) / (bmSagDrive[i + 1] - bmSagDrive[i]);
        return (bmSagRailDr[i] + t * (bmSagRailDr[i + 1] - bmSagRailDr[i])) * scale;
    }

    NodalCircuit::Node gainStage (NodalCircuit& c, NodalCircuit::Node grid, NodalCircuit::Node vcc, double rPlate, double rK,
                                  double cK, double plateGuess, const KorenTriode::Parameters& tube = triode12AX7())
    {
        const auto gnd = NodalCircuit::ground;
        const auto plate = c.addNode(), k = c.addNode();
        c.addTriode (plate, grid, k, tube);
        c.addCapacitor (grid, plate, cgp);
        c.addResistor (vcc, plate, rPlate);
        c.addResistor (k, gnd, rK);
        if (cK > 0.0)
            c.addCapacitor (k, gnd, cK);
        c.setInitialGuess (plate, plateGuess);
        c.setInitialGuess (k, 1.4);
        return plate;
    }

    // A relay contact: closed = 1 ohm, open = 1G.
    constexpr double closed = 1.0, open = 1.0e9;

    const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::jensen212, &tubeamp::cabs::greenback412, &tubeamp::cabs::v30_412 };
}

SuperSonicStyleAmplifierProcessor::SuperSonicStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto choice = [] (const char* id, const char* name, juce::StringArray names, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, (float) (names.size() - 1), 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [names] (float v, int) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; }));
    };
    auto channel = choice ("ssc_channel", "Channel", { "Vintage", "Burn" }, 0.0f);
    auto fat = choice ("ssc_fat", "Fat", { "Normal", "Fat" }, 0.0f);
    auto vVolume = make ("ssc_v_volume", "Vintage Volume", 0.5f);
    auto vTreble = make ("ssc_v_treble", "Vintage Treble", 0.5f);
    auto vBass = make ("ssc_v_bass", "Vintage Bass", 0.5f);
    auto gain1 = make ("ssc_gain1", "Gain 1", 0.5f);
    auto gain2 = make ("ssc_gain2", "Gain 2", 0.5f);
    auto bTreble = make ("ssc_b_treble", "Burn Treble", 0.5f);
    auto bBass = make ("ssc_b_bass", "Burn Bass", 0.5f);
    auto bMiddle = make ("ssc_b_middle", "Burn Middle", 0.5f);
    auto bVolume = make ("ssc_b_volume", "Burn Volume", 0.5f);
    auto reverb = make ("ssc_reverb", "Reverb", 0.2f);
    auto power = make ("ssc_power", "Power Drive", 1.0f);
    auto bias = make ("ssc_bias", "Bias", 0.5f);
    auto feel = make ("ssc_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ssc_speaker", 1 }, "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm"; }));
    auto output = make ("ssc_output", "Output", 0.5f);
    auto cab = tubeamp::makeCabParam ("ssc_cab", "1x12 Fender", cabAlts);

    channelParam = channel.get();   fatParam = fat.get();         vVolumeParam = vVolume.get();
    vTrebleParam = vTreble.get();   vBassParam = vBass.get();     gain1Param = gain1.get();
    gain2Param = gain2.get();       bTrebleParam = bTreble.get(); bBassParam = bBass.get();
    bMiddleParam = bMiddle.get();   bVolumeParam = bVolume.get(); reverbParam = reverb.get();
    powerParam = power.get();       biasParam = bias.get();       tubeFeelParam = feel.get();
    speakerParam = speaker.get();   outputParam = output.get();   cabParam = cab.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("supersonic22", "Super-Sonic 22", "|", std::move (channel));
    group->addChild (std::move (fat));
    group->addChild (std::move (vVolume));
    group->addChild (std::move (vTreble));
    group->addChild (std::move (vBass));
    group->addChild (std::move (gain1));
    group->addChild (std::move (gain2));
    group->addChild (std::move (bTreble));
    group->addChild (std::move (bBass));
    group->addChild (std::move (bMiddle));
    group->addChild (std::move (bVolume));
    group->addChild (std::move (reverb));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("supersonic22_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);

    // The tank: the spring engine at full wet, no dry (the amp's own mixer adds the dry).
    for (auto* p : spring.getParameters()->getParameters (true))
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
        {
            if (f->paramID == "spring_wet" || f->paramID == "spring_kill_dry")
                *f = 1.0f;
            else if (f->paramID == "spring_decay")
                *f = 0.6f;
            else if (f->paramID == "spring_tone")
                *f = 0.55f;
        }
}

/** Block A (sheet 1, zones D8-D5): input (R1 10K, R2 1M) -> V1A (R3 100K on W, R4 1.5K || C1 22u). Its plate drives:
    VINTAGE -- C4 250p / R8 100K / C5 .1 / C6 .047 into Treble 250K, Bass 250K and a fixed R11 6.8K, Volume R12 1M on the
    Treble wiper, C7 47p across the Volume's top leg with Fat off (K3-A), C3 220p from the plate to ground with Fat on
    (K3-B); BURN -- C2 .0022 (R5 1M) -> R6 15K -> Gain 1 250K with C15 470p (K1-B grounds it on Vintage). K1-A picks a
    wiper into R14 1.5K / R45 470K and V1B (R15 100K on W, R16 1.5K || C8 2.2u, plus C19 22u on Burn via K6-A). The load
    is block B's input as V1B sees it: C9 .22 / R32 4.7M, then Gain 2's network (Burn) or R20 47K into the Fat node
    held near ground by V2B's feedback (Vintage + Fat). */
void SuperSonicStyleAmplifierProcessor::buildPreA (Channel& ch) const
{
    auto& c = ch.preA;
    const auto gnd = NodalCircuit::ground;
    const auto w = c.addNode(), in = c.addNode();
    ch.aSrcW = c.addSource (w, 280.0);
    ch.aSrcIn = c.addSource (in, 0.0);

    const auto g1 = c.addNode();
    c.addResistor (in, g1, 10.0e3);                       // R1
    c.addResistor (in, gnd, 1.0e6);                       // R2
    ch.aPlateV1a = gainStage (c, g1, w, 100.0e3, 1.5e3, 22.0e-6, 180.0);   // R3, R4, C1
    const auto p = ch.aPlateV1a;

    // Fat: C3 220p from the plate to ground (K3-B).
    const auto c3 = c.addNode();
    c.addCapacitor (p, c3, 220.0e-12);                    // C3
    c.addResistor (c3, gnd, 1.0e6);                       // R7
    ch.rFatC3 = c.addResistor (c3, gnd, open);

    // Vintage stack.
    const auto t1 = c.addNode(), tw = c.addNode(), tb = c.addNode(), n = c.addNode(), m = c.addNode();
    c.addCapacitor (p, t1, 250.0e-12);                    // C4
    ch.rVtTop = c.addResistor (t1, tw, 125.0e3);          // R9 TREBLE 250K
    ch.rVtBot = c.addResistor (tw, tb, 125.0e3);
    c.addResistor (p, n, 100.0e3);                        // R8
    c.addCapacitor (n, tb, 0.1e-6);                       // C5
    ch.rVBass = c.addResistor (tb, m, 125.0e3);           // R10 BASS 250K (rheostat)
    c.addCapacitor (n, m, 0.047e-6);                      // C6
    c.addResistor (m, gnd, 6.8e3);                        // R11
    const auto vw = c.addNode(), k3 = c.addNode();
    ch.rVvTop = c.addResistor (tw, vw, 500.0e3);          // R12 VOLUME 1M
    ch.rVvBot = c.addResistor (vw, gnd, 500.0e3);
    ch.rBrightSw = c.addResistor (tw, k3, closed);        // K3-A, Normal
    c.addCapacitor (k3, vw, 47.0e-12);                    // C7
    c.addResistor (k3, gnd, 10.0e6);                      // R13

    // Burn input network.
    const auto b1 = c.addNode(), b2 = c.addNode(), g1n = c.addNode(), gw = c.addNode();
    c.addCapacitor (p, b1, 2.2e-9);                       // C2
    c.addResistor (b1, gnd, 1.0e6);                       // R5
    ch.rBurnIn = c.addResistor (b1, b2, open);            // K1-B
    ch.rBurnInGnd = c.addResistor (b2, gnd, closed);
    c.addResistor (b2, g1n, 15.0e3);                      // R6
    c.addCapacitor (g1n, gnd, 470.0e-12);                 // C15
    ch.rG1Top = c.addResistor (g1n, gw, 125.0e3);         // R31 GAIN 1 250K
    ch.rG1Bot = c.addResistor (gw, gnd, 125.0e3);

    // K1-A, V1B.
    const auto k1 = c.addNode(), g2 = c.addNode();
    ch.rSelVintage = c.addResistor (vw, k1, closed);
    ch.rSelBurn = c.addResistor (gw, k1, open);
    c.addResistor (k1, gnd, 470.0e3);                     // R45
    c.addResistor (k1, g2, 1.5e3);                        // R14
    const auto k = c.addNode(), x6 = c.addNode();
    ch.aPlateV1b = c.addNode();
    c.addTriode (ch.aPlateV1b, g2, k, triode12AX7());
    c.addCapacitor (g2, ch.aPlateV1b, cgp);
    c.addResistor (w, ch.aPlateV1b, 100.0e3);             // R15
    c.addResistor (k, gnd, 1.5e3);                        // R16
    c.addCapacitor (k, gnd, 2.2e-6);                      // C8
    c.addCapacitor (k, x6, 22.0e-6);                      // C19
    c.addResistor (x6, gnd, 100.0e3);                     // R41
    ch.rV1bBypass = c.addResistor (x6, gnd, open);        // K6-A, Burn
    c.setInitialGuess (ch.aPlateV1b, 180.0);
    c.setInitialGuess (k, 1.3);

    // Block B's input, as V1B's load.
    const auto a9 = c.addNode(), l1 = c.addNode(), l2 = c.addNode(), lt = c.addNode(), lw = c.addNode(), lb = c.addNode(), lf = c.addNode();
    c.addCapacitor (ch.aPlateV1b, a9, 0.22e-6);           // C9
    c.addResistor (a9, gnd, 4.7e6);                       // R32
    ch.aLdBurn = c.addResistor (a9, l1, open);
    c.addCapacitor (l1, lt, 0.22e-6);                     // C16
    ch.aLdG2Top = c.addResistor (lt, lw, 125.0e3);
    ch.aLdG2Bot = c.addResistor (lw, lb, 125.0e3);
    c.addResistor (lb, gnd, 20.0e3);                      // R36
    c.addResistor (lw, gnd, 10.0e3);                      // R38
    ch.aLdVintage = c.addResistor (a9, l2, closed);
    ch.aLdFat = c.addResistor (l2, lf, open);
    c.addResistor (lf, gnd, 47.0e3 + 12.0e3);             // R20 into the Fat node (R19 120K || ~1/(gm-feedback) of V2B)
    c.addResistor (l2, gnd, 4.7e6);                       // R17
}

/** Block B (sheet 1, zones D5-D3), driven by V1B's plate: C9 .22 (R32 4.7M) -> K6-B. VINTAGE + NORMAL: K4-A -> C10 .022
    (R18 10M) -> R22 3.3M || C11 10p -> R23 270K -> K4-B -> V2B. VINTAGE + FAT: R20 47K -> the node with R19 120K and R24
    470K back from V2B's output -> C12 .01 (R21 4.7M) -> V2B: V2B in shunt feedback. BURN: C16 .22 -> Gain 2 (250K + R36
    20K) with C18 47p, R38 10K on the wiper, R37 110K, R39 1.5K -> V2A (R40 100K on X, R34 1.5K || C17 22u) -> C20 .047 ->
    R42 470K / R43 100K -> K7-A -> V2B (R26 100K on X, R27 1.5K || C13 2.2u, plus C21 22u on Burn via K7-B), grid leak R25
    470K. V2B -> C14 .22 (R109 1M) -> K2-A: VINTAGE -> R28 100K / R29 15K; BURN -> the stack (C22 150p, R46 120K, C23 .15,
    C24 .022; Treble 250K lin, Bass 250K, Middle 25K lin) -> R50 220K (C25 680p down) -> Volume 250K. K2-B picks the output
    (R30 1M). The load is block C's input: R75 3.3M and the reverb send C92 470p into R161 1M. */
void SuperSonicStyleAmplifierProcessor::buildPreB (Channel& ch) const
{
    auto& c = ch.preB;
    const auto gnd = NodalCircuit::ground;
    const auto w = c.addNode(), x = c.addNode(), in = c.addNode();
    ch.bSrcW = c.addSource (w, 280.0);
    ch.bSrcX = c.addSource (x, 317.0);
    ch.bSrcIn = c.addSource (in, 180.0);
    juce::ignoreUnused (w);

    const auto a9 = c.addNode(), k4 = c.addNode(), v2g = c.addNode();
    c.addCapacitor (in, a9, 0.22e-6);                     // C9
    c.addResistor (a9, gnd, 4.7e6);                       // R32
    ch.rK6Vintage = c.addResistor (a9, k4, closed);
    c.addResistor (k4, gnd, 4.7e6);                       // R17

    // Vintage, Normal.
    const auto pn = c.addNode(), q = c.addNode(), s = c.addNode();
    ch.rK4Normal = c.addResistor (k4, pn, closed);
    c.addResistor (pn, gnd, 10.0e6);                      // R18
    c.addCapacitor (pn, q, 0.022e-6);                     // C10
    c.addResistor (q, s, 3.3e6);                          // R22
    c.addCapacitor (q, s, 10.0e-12);                      // C11
    c.addResistor (s, gnd, 270.0e3);                      // R23
    ch.rK4bNormal = c.addResistor (s, v2g, closed);

    // Vintage, Fat: V2B in 470K shunt feedback (R24 from C14's output).
    const auto fa = c.addNode(), fm = c.addNode(), fn = c.addNode();
    ch.rK4Fat = c.addResistor (k4, fa, open);
    c.addResistor (fa, fm, 47.0e3);                       // R20
    c.addResistor (fm, gnd, 120.0e3);                     // R19
    c.addCapacitor (fm, fn, 0.01e-6);                     // C12
    c.addResistor (fn, gnd, 4.7e6);                       // R21
    ch.rK4bFat = c.addResistor (fn, v2g, open);

    // Burn: Gain 2 -> V2A.
    const auto bt = c.addNode(), gtop = c.addNode(), gw = c.addNode(), gb = c.addNode(), j = c.addNode(), g2a = c.addNode();
    ch.rK6Burn = c.addResistor (a9, bt, open);
    c.addCapacitor (bt, gtop, 0.22e-6);                   // C16
    ch.rG2Top = c.addResistor (gtop, gw, 125.0e3);        // R35 GAIN 2 250K
    ch.rG2Bot = c.addResistor (gw, gb, 125.0e3);
    c.addResistor (gb, gnd, 20.0e3);                      // R36
    c.addResistor (gw, gnd, 10.0e3);                      // R38
    c.addResistor (gw, j, 110.0e3);                       // R37
    c.addCapacitor (gtop, j, 47.0e-12);                   // C18
    c.addResistor (j, g2a, 1.5e3);                        // R39
    ch.bPlateV2a = gainStage (c, g2a, x, 100.0e3, 1.5e3, 22.0e-6, 190.0);   // R40, R34, C17
    const auto c20 = c.addNode(), r43 = c.addNode();
    c.addCapacitor (ch.bPlateV2a, c20, 0.047e-6);         // C20
    c.addResistor (c20, r43, 470.0e3);                    // R42
    c.addResistor (r43, gnd, 100.0e3);                    // R43
    ch.rK7Burn = c.addResistor (r43, v2g, open);          // K7-A; its other side is the K4-B line, open on Burn

    // V2B.
    c.addResistor (v2g, gnd, 470.0e3);                    // R25
    const auto k2 = c.addNode(), x7 = c.addNode();
    ch.bPlateV2b = c.addNode();
    c.addTriode (ch.bPlateV2b, v2g, k2, triode12AX7());
    c.addCapacitor (v2g, ch.bPlateV2b, cgp);
    c.addResistor (x, ch.bPlateV2b, 100.0e3);             // R26
    c.addResistor (k2, gnd, 1.5e3);                       // R27
    c.addCapacitor (k2, gnd, 2.2e-6);                     // C13
    c.addCapacitor (k2, x7, 22.0e-6);                     // C21
    c.addResistor (x7, gnd, 100.0e3);                     // R44
    ch.rV2bBypass = c.addResistor (x7, gnd, open);        // K7-B, Burn
    c.setInitialGuess (ch.bPlateV2b, 200.0);
    c.setInitialGuess (k2, 1.4);

    const auto vo = c.addNode();
    c.addCapacitor (ch.bPlateV2b, vo, 0.22e-6);           // C14
    c.addResistor (vo, gnd, 1.0e6);                       // R109
    c.addResistor (vo, fm, 470.0e3);                      // R24

    // K2-A / K2-B: Vintage divider or the Burn stack and Volume.
    ch.bOut = c.addNode();
    const auto vi = c.addNode(), od = c.addNode();
    ch.rK2aVintage = c.addResistor (vo, vi, closed);
    c.addResistor (vi, od, 100.0e3);                      // R28
    c.addResistor (od, gnd, 15.0e3);                      // R29
    ch.rK2bVintage = c.addResistor (od, ch.bOut, closed);

    const auto bi = c.addNode(), t1 = c.addNode(), tw = c.addNode(), tb = c.addNode(), nn = c.addNode(), mm = c.addNode();
    ch.rK2aBurn = c.addResistor (vo, bi, open);
    c.addCapacitor (bi, t1, 150.0e-12);                   // C22
    ch.rBtTop = c.addResistor (t1, tw, 125.0e3);          // R47 TREBLE 250K
    ch.rBtBot = c.addResistor (tw, tb, 125.0e3);
    c.addResistor (bi, nn, 120.0e3);                      // R46
    c.addCapacitor (nn, tb, 0.15e-6);                     // C23
    ch.rBBass = c.addResistor (tb, mm, 125.0e3);          // R48 BASS 250K
    c.addCapacitor (nn, mm, 0.022e-6);                    // C24
    ch.rBMid = c.addResistor (mm, gnd, 12.5e3);           // R49 MID 25K
    const auto bv = c.addNode(), bw = c.addNode();
    c.addResistor (tw, bv, 220.0e3);                      // R50
    c.addCapacitor (bv, gnd, 680.0e-12);                  // C25
    ch.rBvTop = c.addResistor (bv, bw, 125.0e3);          // R51 VOLUME 250K
    ch.rBvBot = c.addResistor (bw, gnd, 125.0e3);
    ch.rK2bBurn = c.addResistor (bw, ch.bOut, open);
    c.addResistor (ch.bOut, gnd, 1.0e6);                  // R30

    // Block C's input as the load: R75 3.3M into the mixer, C92 470p into R161 1M (reverb send).
    const auto sn = c.addNode();
    c.addResistor (ch.bOut, gnd, 3.3e6);                  // R75
    c.addCapacitor (ch.bOut, sn, 470.0e-12);              // C92
    c.addResistor (sn, gnd, 1.0e6);                       // R161
}

/** Block C (sheet 1, zone B4-B3): the dry channel output through R75 3.3M and the reverb return -- V4A's plate through
    C96 .0033 onto the Reverb pot (R166 100K lin), its wiper through R167 1M -- into the mixer (R76 220K) -> R126 10K ->
    V4B (R77 100K on W, R78 1.5K || C38 22u) -> C39 .1 -> R80 22K -> the power amp input (R79 1M, R127 1M). With the
    effects loop off (relay K5 at rest) that node is the power amp's input; the loop's op-amps are not in the path. */
void SuperSonicStyleAmplifierProcessor::buildPreC (Channel& ch) const
{
    auto& c = ch.preC;
    const auto gnd = NodalCircuit::ground;
    const auto w = c.addNode(), out = c.addNode(), rv = c.addNode();
    ch.cSrcW = c.addSource (w, 280.0);
    ch.cSrcOut = c.addSource (out, 0.0);
    ch.cSrcReverb = c.addSource (rv, 0.0);

    ch.cMix = c.addNode();
    const auto rt = c.addNode(), rw = c.addNode(), g = c.addNode();
    c.addResistor (out, ch.cMix, 3.3e6);                  // R75
    c.addCapacitor (rv, rt, 3.3e-9);                      // C96
    ch.rRevTop = c.addResistor (rt, rw, 50.0e3);          // R166 REVERB 100K
    ch.rRevBot = c.addResistor (rw, gnd, 50.0e3);
    c.addResistor (rw, ch.cMix, 1.0e6);                   // R167
    c.addResistor (ch.cMix, gnd, 220.0e3);                // R76
    c.addResistor (ch.cMix, g, 10.0e3);                   // R126
    ch.cPlateV4b = gainStage (c, g, w, 100.0e3, 1.5e3, 22.0e-6, 180.0);   // R77, R78, C38
    const auto c39 = c.addNode();
    ch.cPampIn = c.addNode();
    c.addCapacitor (ch.cPlateV4b, c39, 0.1e-6);           // C39
    c.addResistor (c39, ch.cPampIn, 22.0e3);              // R80
    c.addResistor (ch.cPampIn, gnd, 1.0e6);               // R79
    c.addResistor (ch.cPampIn, gnd, 1.0e6);               // R127
    // the PI's input as the load: C68 .01 into its 1M grid return
    const auto pl = c.addNode();
    c.addCapacitor (ch.cPampIn, pl, 0.01e-6);
    c.addResistor (pl, gnd, 1.0e6);
}

void SuperSonicStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode(), nl = c.addNode();
        ch.sA = c.addNode(); ch.sB = c.addNode(); ch.sC = c.addNode(); ch.sD = c.addNode(); ch.sE = c.addNode();
        ch.srcVoc = c.addSource (vo, railPlatesNominal + 5.0);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 47.0e-6);             // C78
        c.addResistor (ch.sA, nl, chokeResistance);       // L1
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 22.0e-6);             // C80
        c.addResistor (ch.sB, ch.sC, 4.7e3);              // R148
        c.addCapacitor (ch.sC, gnd, 22.0e-6);             // C82
        c.addResistor (ch.sC, ch.sD, 10.0e3);             // R150
        c.addCapacitor (ch.sD, gnd, 22.0e-6);             // C83
        c.addResistor (ch.sD, ch.sE, 10.0e3);             // R151
        c.addCapacitor (ch.sE, gnd, 22.0e-6);             // C84
        ch.iA = c.addCurrentSource (ch.sA, -0.048);
        ch.iB = c.addCurrentSource (ch.sB, -0.010);
        ch.iC = c.addCurrentSource (ch.sC, -0.005);
        ch.iD = c.addCurrentSource (ch.sD, -0.003);
        ch.iE = c.addCurrentSource (ch.sE, -0.004);
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 376.0);
        c.setInitialGuess (ch.sD, 317.0);
        c.setInitialGuess (ch.sE, 280.0);
    }

    buildPreA (ch);
    buildPreB (ch);
    buildPreC (ch);

    // ================================================================ PI and power stage (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto in = c.addNode(), vpi = c.addNode(), ct = c.addNode(), biasRail = c.addNode();
        ch.wSrcIn = c.addSource (in, 0.0);
        ch.wSrcPi = c.addSource (vpi, 376.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (biasRail, biasSupplyVolts);

        // V5 (12AT7) long-tailed pair: C68 .01 -> Power Drive (a 1M pot standing in for R129, so at full it IS R129) -> V5A;
        // cathodes -> R130 470R -> T -> R128 22K -> F -> R133 47R; R131 1M and C69 .1 on V5B's grid; R134 91K / R135 100K.
        const auto n0 = c.addNode(), g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(),
                   t = c.addNode(), f = c.addNode();
        ch.wGridA = g1; ch.wPlateA = pa; ch.wPlateB = pb; ch.wTail = t; ch.wFeedback = f;
        c.addCapacitor (in, n0, 0.01e-6);                 // C68
        ch.rDriveTop = c.addResistor (n0, g1, closed);
        ch.rDriveBot = c.addResistor (g1, t, 1.0e6);      // R129
        c.addResistor (g2, t, 1.0e6);                     // R131
        c.addCapacitor (g2, f, 0.1e-6);                   // C69
        c.addTriode (pa, g1, k, triode12AT7());
        c.addTriode (pb, g2, k, triode12AT7());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 91.0e3);                  // R134
        c.addResistor (vpi, pb, 100.0e3);                 // R135
        c.addResistor (k, t, 470.0);                      // R130
        c.addResistor (t, f, 22.0e3);                     // R128
        c.addResistor (f, gnd, 47.0);                     // R133
        c.setInitialGuess (pa, 230.0); c.setInitialGuess (pb, 225.0);
        c.setInitialGuess (k, 70.0); c.setInitialGuess (t, 69.0); c.setInitialGuess (g1, 69.0); c.setInitialGuess (g2, 69.0);

        // 2 x 6V6GT: C71 / C72 .1, R136 / R137 220K to the bias rail, R138 / R139 1.5K stoppers.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode();
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        ch.wPP1 = pp1; ch.wPP2 = pp2;
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.1e-6);
        c.addCapacitor (pb, g4, 0.1e-6);
        c.addResistor (g3, biasRail, 220.0e3);
        c.addResistor (g4, biasRail, 220.0e3);
        c.addResistor (g3, g3s, 1.5e3);
        c.addResistor (g4, g4s, 1.5e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6V6(), 418.0);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6V6(), 418.0);
        c.addCapacitor (pp1, pp2, 300.0e-12);
        c.addResistor (pp1, pp2, 60.0e3);
        c.addCapacitor (pp1, gnd, 300.0e-12);
        c.addCapacitor (pp2, gnd, 300.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance, ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh, mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        c.addResistor (sw, ch.wOut, secondaryResistance);
        {
            const auto sm = speakerModel (speakerNominal[matchedSpeaker]);
            const auto na = c.addNode(), nbb = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na, nbb } }, { sm.le });
            ch.rSpkEddy = c.addResistor (na, nbb, speakerEddyLoss);
            ch.rSpkRp = c.addResistor (nbb, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb, gnd, sm.cp);
        }
        ch.rFeedback = c.addResistor (ch.wOut, f, feedbackResistor);   // R132
        c.addCapacitor (f, gnd, 1.5e-9);
        for (auto n : { pp1, pp2, a1, a2 })
            c.setInitialGuess (n, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s })
            c.setInitialGuess (n, biasSupplyVolts);
    }
}

void SuperSonicStyleAmplifierProcessor::applySwitches (Channel& ch, bool burn, bool fat) const
{
    const auto sw = [] (bool on) { return on ? closed : open; };
    // K1 / K6 / K7 / K2: Vintage <-> Burn. K3 / K4: Normal <-> Fat.
    ch.preA.setResistance (ch.rBurnIn, sw (burn));
    ch.preA.setResistance (ch.rBurnInGnd, sw (! burn));
    ch.preA.setResistance (ch.rSelVintage, sw (! burn));
    ch.preA.setResistance (ch.rSelBurn, sw (burn));
    ch.preA.setResistance (ch.rV1bBypass, sw (burn));
    ch.preA.setResistance (ch.rBrightSw, sw (! fat));
    ch.preA.setResistance (ch.rFatC3, sw (fat));
    ch.preA.setResistance (ch.aLdBurn, sw (burn));
    ch.preA.setResistance (ch.aLdVintage, sw (! burn));
    ch.preA.setResistance (ch.aLdFat, sw (fat));

    ch.preB.setResistance (ch.rK6Vintage, sw (! burn));
    ch.preB.setResistance (ch.rK6Burn, sw (burn));
    ch.preB.setResistance (ch.rK4Normal, sw (! fat));
    ch.preB.setResistance (ch.rK4bNormal, sw (! fat && ! burn));
    ch.preB.setResistance (ch.rK4Fat, sw (fat));
    ch.preB.setResistance (ch.rK4bFat, sw (fat && ! burn));
    ch.preB.setResistance (ch.rK7Burn, sw (burn));
    ch.preB.setResistance (ch.rV2bBypass, sw (burn));
    ch.preB.setResistance (ch.rK2aVintage, sw (! burn));
    ch.preB.setResistance (ch.rK2bVintage, sw (! burn));
    ch.preB.setResistance (ch.rK2aBurn, sw (burn));
    ch.preB.setResistance (ch.rK2bBurn, sw (burn));
}

void SuperSonicStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const auto split = [] (double total, double bottomFraction, double& top, double& bottom)
    {
        bottom = juce::jmax (1.0, total * bottomFraction);
        top = juce::jmax (1.0, total - bottom);
    };
    double vtT, vtB, vvT, vvB, g1T, g1B, g2T, g2B, btT, btB, bvT, bvB, rvT, rvB, dT, dB;
    // tapers per the drawing: "30A" = 30 % audio, "15A" = 15 % audio, "B" = linear
    split (250.0e3, pots::law (k.vTreble, 0.30), vtT, vtB);
    split (1.0e6, pots::law (k.vVolume, 0.30), vvT, vvB);
    split (250.0e3, pots::audio (k.gain1), g1T, g1B);
    split (250.0e3, pots::audio (k.gain2), g2T, g2B);
    split (250.0e3, k.bTreble, btT, btB);
    split (250.0e3, pots::law (k.bVolume, 0.30), bvT, bvB);
    split (100.0e3, k.reverb, rvT, rvB);
    driveFraction = juce::jmax (0.002, pots::audio (k.powerDrive));
    split (1.0e6, driveFraction, dT, dB);
    const double vBass = juce::jmax (1.0, 250.0e3 * pots::audio (k.vBass));
    const double bBass = juce::jmax (1.0, 250.0e3 * pots::audio (k.bBass));
    const double bMid = juce::jmax (1.0, 25.0e3 * k.bMiddle);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));
    const double biasVolts = biasSupplyVolts - 16.0 * (k.bias - 0.5);

    for (auto& ch : channels)
    {
        auto& a = ch.preA;
        a.setResistance (ch.rVtTop, vtT); a.setResistance (ch.rVtBot, vtB); a.setResistance (ch.rVBass, vBass);
        a.setResistance (ch.rVvTop, vvT); a.setResistance (ch.rVvBot, vvB);
        a.setResistance (ch.rG1Top, g1T); a.setResistance (ch.rG1Bot, g1B);
        a.setResistance (ch.aLdG2Top, g2T); a.setResistance (ch.aLdG2Bot, g2B);
        auto& b = ch.preB;
        b.setResistance (ch.rG2Top, g2T); b.setResistance (ch.rG2Bot, g2B);
        b.setResistance (ch.rBtTop, btT); b.setResistance (ch.rBtBot, btB); b.setResistance (ch.rBBass, bBass);
        b.setResistance (ch.rBMid, bMid); b.setResistance (ch.rBvTop, bvT); b.setResistance (ch.rBvBot, bvB);
        ch.preC.setResistance (ch.rRevTop, rvT); ch.preC.setResistance (ch.rRevBot, rvB);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rDriveTop, dT);
            ch.power.setResistance (ch.rDriveBot, dB);
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setSource (ch.wSrcBias, biasVolts);
            if (k.speaker != appliedSpeaker)
                applySpeaker (ch, k.speaker);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void SuperSonicStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    if (reducedOrder)
        return;
    const double nominal = speakerNominal[juce::jlimit (0, 2, index)];
    const auto sm = speakerModel (nominal);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setResistance (ch.rSpkEddy, speakerEddyLoss * nominal / speakerNominal[matchedSpeaker]);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, { 1.0 / sm.le });
    ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 / sm.lp });
}

void SuperSonicStyleAmplifierProcessor::preparePreamp (Channel& ch, double rate, bool& ok) const
{
    const double w = ch.supply.voltage (ch.sE), x = ch.supply.voltage (ch.sD);
    ch.preA.setSource (ch.aSrcW, w);
    ok = ch.preA.prepare (rate) && ok;
    ch.preB.setSource (ch.bSrcW, w);
    ch.preB.setSource (ch.bSrcX, x);
    ch.preB.setSource (ch.bSrcIn, ch.preA.voltage (ch.aPlateV1b));
    ok = ch.preB.prepare (rate) && ok;
    ch.preC.setSource (ch.cSrcW, w);
    ch.preC.setSource (ch.cSrcOut, ch.preB.voltage (ch.bOut));
    ok = ch.preC.prepare (rate) && ok;
}

bool SuperSonicStyleAmplifierProcessor::solvePreamp (Channel& ch, double x, double reverbReturnVolts) const
{
    ch.preA.setSource (ch.aSrcIn, x);
    bool ok = ch.preA.solveSample();
    ch.preB.setSource (ch.bSrcIn, ch.preA.voltage (ch.aPlateV1b));
    ok = ch.preB.solveSample() && ok;
    ch.preC.setSource (ch.cSrcOut, ch.preB.voltage (ch.bOut));
    ch.preC.setSource (ch.cSrcReverb, reverbReturnVolts);
    ok = ch.preC.solveSample() && ok;
    return ok;
}

void SuperSonicStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! reducedOrder)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.6, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.sumScreen / n + 0.0033));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    const double w = rail (ch.sE, 400.0), x = rail (ch.sD, 400.0);
    ch.preA.setSource (ch.aSrcW, w);
    ch.preB.setSource (ch.bSrcW, w);
    ch.preB.setSource (ch.bSrcX, x);
    ch.preC.setSource (ch.cSrcW, w);
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 520.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 480.0));
    }
    ch.vScreen = rail (ch.sB, 520.0);
}

void SuperSonicStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.preA.restoreDynamicState (ch.restA);
    ch.preB.restoreDynamicState (ch.restB);
    ch.preC.restoreDynamicState (ch.restC);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
}

double SuperSonicStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double drive) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (drive);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    ch.bmEnvelope += (1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)))) * (absDrive - ch.bmEnvelope);
    const double feel = juce::jlimit (0.0, 1.0, lastKnobs.tubeFeel), bias = juce::jlimit (0.0, 1.0, lastKnobs.bias);
    const double rest = sagRail (0.0);
    ch.bmRail = rest - feel * (rest - sagRail (ch.bmEnvelope));
    const double k = ch.bmRail * bmYmax / bmGain0;
    const double envScale = juce::jlimit (0.0, 1.0, ch.bmEnvelope / juce::jmax (1.0e-9, k));
    const double shift = (bmAsym * envScale + 0.6 * (bias - 0.5)) * k;
    const auto sat = [k, rail = ch.bmRail] (double xx)
    {
        const double ux = std::abs (xx) / juce::jmax (1.0e-9, k);
        return std::copysign (bmYmax * ux / std::pow (1.0 + std::pow (ux, bmKneeN), 1.0 / bmKneeN) * rail, xx);
    };
    const double raw = juce::jlimit (-ch.bmRail * bmYmax, ch.bmRail * bmYmax, sat (drive + shift) - sat (shift));
    ch.bmDcState += (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmDcHz / juce::jmax (1.0, sampleRate))) * (raw - ch.bmDcState);
    const double blocked = raw - ch.bmDcState;
    ch.bmToneState += (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate))) * (blocked - ch.bmToneState);
    ch.bmOutput = ch.bmTail.process ((ch.bmToneState + bmShelfHfGain * (blocked - ch.bmToneState)) * bmLevelTrim);
    return ch.bmOutput;
}

double SuperSonicStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1aPlate: return ch.preA.voltage (ch.aPlateV1a);
        case Probe::v1bPlate: return ch.preA.voltage (ch.aPlateV1b);
        case Probe::v2aPlate: return ch.preB.voltage (ch.bPlateV2a);
        case Probe::v2bPlate: return ch.preB.voltage (ch.bPlateV2b);
        case Probe::channelOut: return ch.preB.voltage (ch.bOut);
        case Probe::mixNode: return ch.preC.voltage (ch.cMix);
        case Probe::v4bPlate: return ch.preC.voltage (ch.cPlateV4b);
        case Probe::powerAmpIn: return ch.preC.voltage (ch.cPampIn);
        case Probe::phaseInverterGrid: return ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return ch.power.voltage (ch.wTail);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::feedbackNode: return ch.power.voltage (ch.wFeedback);
    }
    return 0.0;
}

double SuperSonicStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    const auto& ch = channels[0];
    switch (block)
    {
        case 0: return ch.preA.averageIterations();
        case 1: return ch.preB.averageIterations();
        case 2: return ch.preC.averageIterations();
        default: return ch.power.averageIterations();
    }
}

void SuperSonicStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double SuperSonicStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

void SuperSonicStyleAmplifierProcessor::prepare (double newSampleRate, int maxBlockSize, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0 && maxBlockSize <= maxBlock)
        return;
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (maxBlock, maxBlockSize);

    spring.prepare (newSampleRate, maxBlock, 2);
    reverbSend.setSize (2, maxBlock);
    reverbReturn.setSize (2, maxBlock);
    reverbSend.clear();
    reverbReturn.clear();
    reverbReturnLength = 0;
    sendCoeff = 1.0 - std::exp (-1.0 / (470.0e-12 * 1.0e6 * newSampleRate)); // C92 into R161

    juce::AudioParameterFloat* const params[13] = { vVolumeParam, vTrebleParam, vBassParam, gain1Param, gain2Param, bTrebleParam,
                                                    bBassParam, bMiddleParam, bVolumeParam, reverbParam, powerParam, biasParam,
                                                    tubeFeelParam };
    for (int i = 0; i < 13; ++i)
    {
        smoothed[i].reset (newSampleRate, i >= 11 ? 0.05 : 0.02);
        smoothed[i].setCurrentAndTargetValue (params[i]->get());
    }

    const bool burn = channelParam->get() > 0.5f, fat = fatParam->get() > 0.5f;
    appliedChannel = burn ? 1 : 0;
    appliedFat = fat ? 1 : 0;
    appliedSpeaker = matchedSpeaker;
    idleSupplyCurrent = 0.07;
    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto voicing = voicings::fenderClean;
        voicing.cabLpHz = 8000.0;
        voicing.cabAirHz = 12000.0;
        voicing.asym = 0.0;
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (newSampleRate, voicing, 1.0 / outputScale);
        applySwitches (ch, burn, fat);
    }
    updatePots ({ vVolumeParam->get(), vTrebleParam->get(), vBassParam->get(), gain1Param->get(), gain2Param->get(),
                  bTrebleParam->get(), bBassParam->get(), bMiddleParam->get(), bVolumeParam->get(), reverbParam->get(),
                  powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool ok = true;
        double ipRun = 0.048, isRun = 0.007, iC = 0.005, iDRun = 0.002, iERun = 0.004;
        for (int pass = 0; pass < 10; ++pass)
        {
            ok = ch.supply.prepare (supplyRate);
            preparePreamp (ch, newSampleRate, ok);
            ch.vScreen = ch.supply.voltage (ch.sB);
            double ipA = 0.024, ipB = 0.024, isA = 0.0015, isB = 0.0015;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, ch.preC.voltage (ch.cPampIn));
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - screenResistor * isA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - screenResistor * isB);
                ok = ch.power.prepare (newSampleRate) && ok;
                ch.power.solveSample();
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                const double vPi = ch.supply.voltage (ch.sC);
                iC = (vPi - ch.power.voltage (ch.wPlateA)) / 91.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 100.0e3;
            }
            const double w = ch.supply.voltage (ch.sE), x = ch.supply.voltage (ch.sD);
            const double iE = (w - ch.preA.voltage (ch.aPlateV1a)) / 100.0e3 + (w - ch.preA.voltage (ch.aPlateV1b)) / 100.0e3
                            + (w - ch.preC.voltage (ch.cPlateV4b)) / 100.0e3 + 0.0012;     // + V4A, the reverb recovery
            const double iD = (x - ch.preB.voltage (ch.bPlateV2a)) / 100.0e3 + (x - ch.preB.voltage (ch.bPlateV2b)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) + 0.0033 - isRun);   // + the reverb driver (V3, 12AT7 on Z)
            iDRun += 0.5 * (iD - iDRun);
            iERun += 0.5 * (iE - iERun);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iC);
            ch.supply.setCurrentSource (ch.iD, -iDRun);
            ch.supply.setCurrentSource (ch.iE, -iERun);
            idleSupplyCurrent = ipRun + isRun + iC + iDRun + iERun;
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * feel * idleSupplyCurrent);
            ch.screenDropA = screenResistor * isA;
            ch.screenDropB = screenResistor * isB;
        }
        dcOk = ok && ch.supply.prepare (supplyRate) && dcOk;
        ch.vScreen = ch.supply.voltage (ch.sB);
        preparePreamp (ch, newSampleRate, dcOk);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcIn, ch.preC.voltage (ch.cPampIn));
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
            dcOk = ch.power.prepare (newSampleRate) && dcOk;
        }
        ch.preA.saveDynamicState (ch.restA);
        ch.preB.saveDynamicState (ch.restB);
        ch.preC.saveDynamicState (ch.restC);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.bmRail = sagRail (0.0);
        ch.piCoupling.prepare (newSampleRate, 0.01e-6, 1.0e6, ch.preC.voltage (ch.cPampIn));
    }

    controlCounter = 0;
    sampleCount = failureCount = 0;
    shortcut.reset();
}

void SuperSonicStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = juce::jmin (buffer.getNumSamples(), maxBlock);
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    juce::AudioParameterFloat* const params[13] = { vVolumeParam, vTrebleParam, vBassParam, gain1Param, gain2Param, bTrebleParam,
                                                    bBassParam, bMiddleParam, bVolumeParam, reverbParam, powerParam, biasParam,
                                                    tubeFeelParam };
    for (int i = 0; i < 13; ++i)
        smoothed[i].setTargetValue (params[i]->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int channelChoice = channelParam->get() > 0.5f ? 1 : 0, fatChoice = fatParam->get() > 0.5f ? 1 : 0;
    if (channelChoice != appliedChannel || fatChoice != appliedFat)
    {
        for (auto& ch : channels)
            applySwitches (ch, channelChoice == 1, fatChoice == 1);
        appliedChannel = channelChoice;
        appliedFat = fatChoice;
    }
    const double ou = outputParam->get();
    const double outGain = std::pow (10.0, (ou < 0.5 ? (ou - 0.5) * 60.0 : (ou - 0.5) * 24.0) / 20.0);
    // The tank's return reaches V4A's plate at ~0.19 of the send level (the drawing's 1 kHz test voltages: 88 mV at the
    // channel output, 17 mV at TP29); the spring engine itself is level-matched to its input.
    constexpr double returnGain = 0.19;

    for (int i = 0; i < numSamples; ++i)
    {
        float v[13];
        for (int p = 0; p < 13; ++p)
            v[p] = smoothed[p].getNextValue();
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], speakerChoice });
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);
            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            const double ret = i < reverbReturnLength ? returnGain * reverbReturn.getSample (chIdx, i) : 0.0;
            const bool okPre = solvePreamp (ch, x, ret);
            bool ok = okPre;

            const double out = ch.preB.voltage (ch.bOut);
            ch.sendHp += sendCoeff * (out - ch.sendHp);
            const float send = (float) (out - ch.sendHp);
            reverbSend.setSample (chIdx, i, send);
            if (solveChannels == 1)
                reverbSend.setSample (1, i, send);

            const double pampIn = ch.preC.voltage (ch.cPampIn);
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, pampIn);
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
                ok = ch.power.solveSample() && ok;
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.screenDropA += 0.3 * (screenResistor * isA - ch.screenDropA);
                ch.screenDropB += 0.3 * (screenResistor * isB - ch.screenDropB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, driveFraction * ch.piCoupling.process (pampIn))
                                                     : ch.power.voltage (ch.wOut);
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < 150.0;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));

            if (ok)
            {
                ch.failStreak = 0;
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.preA.saveDynamicState (ch.restA);
                    ch.preB.saveDynamicState (ch.restB);
                    ch.preC.saveDynamicState (ch.restC);
                    ch.power.saveDynamicState (ch.powerRest);
                    ch.supply.saveDynamicState (ch.supplyRest);
                }
            }
            else if (++ch.failStreak >= 4)
            {
                recover (ch);
                ch.restRefreshCounter = 0;
            }

            double o = ch.lastEmitted;
            if (sane)
            {
                o = speakerVolts * outputScale * outGain * speakerGain;
                if (ch.alignOutput)
                {
                    ch.declick = ch.lastEmitted - o;
                    ch.alignOutput = false;
                }
                o += ch.declick;
                ch.declick *= declickDecay;
            }
            ch.lastEmitted = o;
            data[i] = (float) o;
            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    // The tank, one block behind: this block's sends become the next block's return.
    juce::AudioBuffer<float> sendView (reverbSend.getArrayOfWritePointers(), 2, numSamples);
    spring.process (sendView);
    for (int c = 0; c < 2; ++c)
        reverbReturn.copyFrom (c, 0, sendView, c, 0, numSamples);
    reverbReturnLength = numSamples;

    shortcut.end (buffer);
}

void SuperSonicStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
