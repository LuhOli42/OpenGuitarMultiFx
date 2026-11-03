#include "DiezelStyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"
#include "TubeAmpCommon.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace tubeamp;

    KorenTriode::Parameters triode12AX7() { return {}; } // 12AX7A sections (V1-V5 on the drawing)

    /** KT77 (the EL34 family the Diezels run) as one push-pull pair -- the amp's four bottles run as two.
        Same EL34 Koren fit the Uberschall model uses (mu 8.11, Ex 1.50, kg1 1201, kg2 3720, kp 100, kvb 24)
        with the pair-doubling the Twin Reverb model uses (kg1/2 halved, grid current doubled, arc
        resistance halved). */
    KorenPentode::Parameters pentodeKT77Pair()
    {
        KorenPentode::Parameters p;
        p.mu = 8.11;
        p.ex = 1.5;
        p.kg1 = 1201.0;
        p.kg2 = 3720.0;
        p.kp = 100.0;
        p.kvb = 24.0;
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // VH4-class rails: solid-state bridge, ~430 V plates, ~420 V screens (470R dropper), ~310 V phase
    // inverter, ~270 V preamp droppers (service-note topology; a VH4 schematic is not public).
    constexpr double railPlatesNominal = 430.0;
    constexpr double rectifierR = 15.0;
    constexpr double reservoir = 100.0e-6;
    constexpr double screenResistor = 470.0;
    constexpr double piFraction = 310.0 / 430.0;
    constexpr double preFraction = 367.0 / 430.0;   // the drawing's +367 V preamp rail
    constexpr double preRailNominal = 367.0;
    constexpr double idlePlateCurrent = 0.085;   // per pair of KT77s
    constexpr double preampAndPiCurrent = 0.009;
    constexpr double biasNominal = -47.0;        // ~35 mA a tube at idle (Diezel service manual)
    constexpr double biasSpan = 8.0;

    constexpr double otPrimary = 1800.0;         // ~100 W 4x6L6GC, plate-to-plate
    constexpr double primaryHalfL = 4.0;
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.999;
    constexpr double primaryHalfResistance = 30.0;
    constexpr double secondaryResistance = 0.1;
    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2; // the 16 ohm tap

    // ---- reduced-order power stage: calibration data, DZL_POWERCAL in the test file ----
    // Plate rail vs. the preamp-signal drive PEAK, measured on the full reference model (lead channel, Gain 1,
    // Master 1, Power Drive max, 16 ohm). Stiff solid-state bridge: modest sag.
    constexpr int bmSagPoints = 9;
    constexpr double bmSagDrive[bmSagPoints] = { 0.02, 0.08, 0.25, 0.70, 1.80, 4.50, 9.00, 15.0, 22.0 };
    constexpr double bmSagRail[bmSagPoints] = { 430.0, 429.0, 427.0, 422.0, 413.0, 400.0, 388.0, 380.0, 375.0 };

    double sagRailLookup (double drivePeak) noexcept
    {
        if (drivePeak <= bmSagDrive[0])
            return bmSagRail[0];
        if (drivePeak >= bmSagDrive[bmSagPoints - 1])
            return bmSagRail[bmSagPoints - 1];
        int i = 0;
        while (i < bmSagPoints - 2 && bmSagDrive[i + 1] < drivePeak)
            ++i;
        const double t = (drivePeak - bmSagDrive[i]) / (bmSagDrive[i + 1] - bmSagDrive[i]);
        return bmSagRail[i] + t * (bmSagRail[i + 1] - bmSagRail[i]);
    }
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::greenback412, &tubeamp::cabs::v30_412, &tubeamp::cabs::alnico212 };
}

DiezelStyleAmplifierProcessor::DiezelStyleAmplifierProcessor()
{
    const juce::String px ("dzl_");
    auto make = [&] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (px + id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto channel = std::make_unique<juce::AudioParameterFloat> (
        px + "channel", "Channel", juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            constexpr const char* names[] = { "Clean", "Crunch", "Mega", "Lead" };
            return juce::String (names[juce::jlimit (0, 3, juce::roundToInt (v))]);
        }));
    auto gain = make ("gain", "Gain", 0.5f);
    auto treble = make ("treble", "Treble", 0.5f);
    auto middle = make ("middle", "Middle", 0.5f);
    auto bass = make ("bass", "Bass", 0.5f);
    auto volume = make ("volume", "Volume", 0.8f);
    auto master = make ("master", "Master", 0.5f); // 0.8 left the power stage pinned: the channel Volumes did nothing
    auto presence = make ("presence", "Presence", 0.5f);
    auto deep = make ("deep", "Deep", 0.5f);
    auto power = make ("power", "Power Drive", 1.0f);
    auto bias = make ("bias", "Bias", 0.5f);
    auto feel = make ("tube_feel", "Tube Feel", 1.0f);
    auto output = make ("output", "Output", 0.5f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        px + "speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    channelParam = channel.get();
    gainParam = gain.get();
    trebleParam = treble.get();
    middleParam = middle.get();
    bassParam = bass.get();
    volumeParam = volume.get();
    masterParam = master.get();
    presenceParam = presence.get();
    deepParam = deep.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();
    outputParam = output.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("diezel_vh4", "Diezel VH4", "|", std::move (channel));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (volume));
    group->addChild (std::move (master));
    group->addChild (std::move (presence));
    group->addChild (std::move (deep));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("diezel_vh4_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("diezel_vh4_cab", "4x12 Diezel", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);

    // the VH4 stores Gain/TMB/Volume per channel (its MIDI memory); the visible knobs carry per-channel values.
    knobMemory = std::make_unique<ChannelKnobMemory> (*channelParam,
        std::vector<juce::AudioParameterFloat*> { gainParam, trebleParam, middleParam, bassParam, volumeParam });
    // Every channel starts at noon (the rebuilt circuit's channel levels are matched by modeLevelMatchDb instead).
}

void DiezelStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const double screenNominal = railPlatesNominal - screenResistor * (2.0 * 0.012 + preampAndPiCurrent);

    // ================================================================ supply: SS bridge -> reservoir (plates) -> 500R -> screens
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierR);
        c.addCapacitor (ch.sA, gnd, reservoir);
        c.addResistor (ch.sA, ch.sB, screenResistor);
        c.addCapacitor (ch.sB, gnd, 68.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -2.0 * idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -preampAndPiCurrent);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, screenNominal);
    }

    // ================================================================ preamp: FOUR channel netlists (drawing "VH4 v1.0",
    // docs/circuits/Diezel.md). Every channel starts at V1a and ends at V3a -> the V3b cathode follower -> C20 into the
    // tone-stack bus TS; the relays between them are what makes the four channels:
    //   CH1  V1a -> the passive C3 / 470k-chain network -> CH1 Gain -> V3a
    //   CH2  V1a -> C8 10n / R9 3M3 || C9 680p -> CH2 Gain -> V2b -> C11 4.7n / R12 470k -> V3a
    //   CH3  V1a -> C13 1n / R14 220k -> CH3 Gain -> R21 47k -> V2a -> C18 4.7n -> V3a
    //   CH4  V1a -> C14 1n / R15 220k -> CH4 Gain -> V1b -> C16 2.2n / R19 470k + R20 220k -> R21 -> V2a -> V3a
    // The other channels' input networks stay on V1a's plate in every netlist (they are always wired there), as
    // simple equivalents; so do the four tone stacks on the follower (12.25k + 88n, four 10k + 39k + 22n legs).
    // Only the selected channel's netlist is solved per sample.
    for (int i = 0; i < 4; ++i)
    {
        auto& c = ch.pre[i];
        const auto rail = c.addNode(), in = c.addNode();
        ch.pSrcRail[i] = c.addSource (rail, preRailNominal);
        ch.pSrcIn[i] = c.addSource (in, 0.0);
        int stage = 0;
        const auto triode = [&] (NodalCircuit::Node grid, double rk, double ck)
        {
            const auto p = c.addNode(), k = c.addNode();
            c.addTriode (p, grid, k, triode12AX7());
            c.addCapacitor (grid, p, cgp);
            c.addResistor (rail, p, 120.0e3);
            c.addResistor (k, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (k, gnd, ck);
            c.setInitialGuess (p, 200.0);
            c.setInitialGuess (k, 1.5);
            ch.pPlate[i][(size_t) juce::jmin (stage, 3)] = p;
            ch.pK[i][(size_t) juce::jmin (stage, 3)] = k;
            ++stage;
            return p;
        };
        const auto load = [&] (NodalCircuit::Node from, double cap, double r)
        {
            const auto n = c.addNode();
            c.addCapacitor (from, n, cap);
            c.addResistor (n, gnd, r);
        };
        const auto pot = [&] (NodalCircuit::Node top, int& rTop, int& rBot)
        {
            const auto w = c.addNode();
            rTop = c.addResistor (top, w, 125.0e3);  // 250k Gain
            rBot = c.addResistor (w, gnd, 125.0e3);
            return w;
        };

        // V1a: In-1 -> R1 1M || C1 150p (the drawing's 100-200p) -> grid; R3 120k plate; R2 2k2 + C2 47u.
        c.addResistor (in, gnd, 1.0e6);
        c.addCapacitor (in, gnd, 150.0e-12);
        const auto p1 = triode (in, 2.2e3, 47.0e-6);
        if (i != 0) load (p1, 220.0e-9, 690.0e3);   // CH1: C3 into R4 + R7
        if (i != 1) load (p1, 10.0e-9, 207.0e3);    // CH2: C8 into R8 || (R9 + Gain)
        if (i != 2) load (p1, 1.0e-9, 470.0e3);     // CH3: C13 into R14 + Gain
        if (i != 3) load (p1, 1.0e-9, 350.0e3);     // CH4: C14 into R15 + Gain || R16

        // The V3a input: node1 (R24 1M) -> relay -> node2 (R25 1M) -> relay -> node3 -> R26 470k -> V3a.
        const auto n3 = c.addNode();
        NodalCircuit::Node v3aFeed = n3;
        if (i == 0)
        {
            // CH1: C3 220n -> A; A -R4- j1 -R5- j2 -R6- B, C4 2.2n / C5 220p / C6 220p across them, R7 220k j1 to
            // ground, C7 68p + R7' 10M across the chain (its switch open); B = CH1 Gain top, wiper -> node3.
            const auto aa = c.addNode(), j1 = c.addNode(), j2 = c.addNode(), bb = c.addNode(), c7n = c.addNode();
            c.addCapacitor (p1, aa, 220.0e-9);
            c.addResistor (aa, j1, 470.0e3);
            c.addResistor (j1, j2, 470.0e3);
            c.addResistor (j2, bb, 470.0e3);
            c.addCapacitor (aa, j1, 2.2e-9);
            c.addCapacitor (j1, j2, 220.0e-12);
            c.addCapacitor (j2, bb, 220.0e-12);
            c.addResistor (j1, gnd, 220.0e3);
            c.addCapacitor (aa, c7n, 68.0e-12);
            c.addResistor (c7n, bb, 10.0e6);
            const auto w = pot (bb, ch.rGainTop[i], ch.rGainBot[i]);
            c.addResistor (w, n3, 1.0);
        }
        else if (i == 1)
        {
            // CH2: C8 10n -> R8 220k; R9 3M3 || C9 680p -> CH2 Gain -> V2b (R11 120k, R10 680R + C10 1u) -> C11 4.7n ->
            // R12 470k (C12 470p + R13 10M across, switch open) -> R13 1M -> relay -> node2 (R25 1M) -> node3.
            const auto n8 = c.addNode(), gt = c.addNode(), n11 = c.addNode(), n12 = c.addNode(), c12n = c.addNode(), n2 = c.addNode();
            c.addCapacitor (p1, n8, 10.0e-9);
            c.addResistor (n8, gnd, 220.0e3);
            c.addResistor (n8, gt, 3.3e6);
            c.addCapacitor (n8, gt, 680.0e-12);
            const auto w = pot (gt, ch.rGainTop[i], ch.rGainBot[i]);
            const auto p2b = triode (w, 680.0, 1.0e-6);
            c.addCapacitor (p2b, n11, 4.7e-9);
            c.addResistor (n11, n12, 470.0e3);
            c.addCapacitor (n11, c12n, 470.0e-12);
            c.addResistor (c12n, n12, 10.0e6);
            c.addResistor (n12, gnd, 1.0e6);
            c.addResistor (n12, n2, 1.0);
            c.addResistor (n2, gnd, 1.0e6);
            c.addResistor (n2, n3, 1.0);
        }
        else
        {
            // CH3 / CH4 -> R21 47k -> V2a (R22 3M3 leak; R24 120k, R23 680R + C17 1u) -> C18 4.7n -> node1 (R24 1M) ->
            // node2 (R25 1M) -> node3.
            const auto r21in = c.addNode();
            if (i == 2)
            {
                const auto n13 = c.addNode(), gt = c.addNode();
                c.addCapacitor (p1, n13, 1.0e-9);
                c.addResistor (n13, gt, 220.0e3);
                const auto w = pot (gt, ch.rGainTop[i], ch.rGainBot[i]);
                c.addResistor (w, r21in, 1.0);
            }
            else
            {
                // CH4: C14 1n -> R15 220k -> CH4 Gain -> V1b (R16 220k leak; R18 120k, R17 3k3 + C15 1u) -> C16 2.2n ->
                // R19 470k / R20 220k -> R21.
                const auto n14 = c.addNode(), gt = c.addNode(), n16 = c.addNode();
                c.addCapacitor (p1, n14, 1.0e-9);
                c.addResistor (n14, gt, 220.0e3);
                const auto w = pot (gt, ch.rGainTop[i], ch.rGainBot[i]);
                c.addResistor (w, gnd, 220.0e3);
                const auto p1b = triode (w, 3.3e3, 1.0e-6);
                c.addCapacitor (p1b, n16, 2.2e-9);
                c.addResistor (n16, r21in, 470.0e3);
                c.addResistor (r21in, gnd, 220.0e3);
            }
            const auto g2a = c.addNode(), n1 = c.addNode(), n2 = c.addNode();
            c.addResistor (r21in, g2a, 47.0e3);
            c.addResistor (g2a, gnd, 3.3e6);
            const auto p2a = triode (g2a, 680.0, 1.0e-6);
            c.addCapacitor (p2a, n1, 4.7e-9);
            c.addResistor (n1, gnd, 1.0e6);
            c.addResistor (n1, n2, 1.0);
            c.addResistor (n2, gnd, 1.0e6);
            c.addResistor (n2, n3, 1.0);
        }

        // V3a (R28 120k, R27 680R + C19 2.2u) -> V3b cathode follower DC-coupled (plate on the rail, R29 120k) ->
        // C20 220n -> TS (R30 1M; C20' 10u + R31 470k) with the four tone stacks' input legs on it.
        const auto g3a = c.addNode();
        c.addResistor (v3aFeed, g3a, 470.0e3);
        const auto p3a = triode (g3a, 680.0, 2.2e-6);
        const auto k3b = c.addNode();
        c.addTriode (rail, p3a, k3b, triode12AX7());
        c.addCapacitor (p3a, rail, cgp);
        c.addResistor (k3b, gnd, 120.0e3);
        c.setInitialGuess (k3b, 200.0);
        ch.pPlate[i][3] = p3a;
        ch.pK[i][3] = k3b;
        ch.pOut[i] = c.addNode();
        c.addCapacitor (k3b, ch.pOut[i], 220.0e-9);
        c.addResistor (ch.pOut[i], gnd, 1.0e6);
        {
            const auto n31 = c.addNode(), sl = c.addNode();
            c.addCapacitor (ch.pOut[i], n31, 10.0e-6);
            c.addResistor (n31, gnd, 470.0e3);
            c.addResistor (ch.pOut[i], sl, 12.25e3);
            c.addCapacitor (sl, gnd, 88.0e-9);
        }
    }

    // ================================================================ the selected channel's tone stack, Volume, the
    // channel mixer and V4a / V4b -> Master (one netlist: the four stacks share values; the inactive three are the
    // load inside the preamp netlists). TS -> R 10k -> S; S -> C 560p -> Treble 220k (wiper -> R 470k -> Volume 470k);
    // Treble's bottom -> Bass 1M -> Middle 22k -> ground; S -> R 39k -> M -> 22n to the Bass wiper and 22n to the
    // Middle wiper. Volume wiper -> relay -> R35 56k (CH1) / R39 R43 R47 680k -> V4a (R 120k, 640R) -> V4b cathode
    // follower (R 120k) -> C 220n -> Master 1M -> the PI. The FX loop is not modelled: its unplugged return passes the
    // send through.
    {
        auto& c = ch.post;
        const auto rail = c.addNode(), ts = c.addNode();
        ch.qSrcRail = c.addSource (rail, preRailNominal);
        ch.qSrcIn = c.addSource (ts, 0.0);
        const auto sn = c.addNode(), tt = c.addNode(), tw = c.addNode(), tb = c.addNode(), bw = c.addNode(), bb = c.addNode(),
                   mw = c.addNode(), mm = c.addNode(), vt = c.addNode(), vw = c.addNode(), sum = c.addNode();
        c.addResistor (ts, sn, 10.0e3);
        c.addCapacitor (sn, tt, 560.0e-12);
        ch.rTrebleTop = c.addResistor (tt, tw, 110.0e3);
        ch.rTrebleBot = c.addResistor (tw, tb, 110.0e3);
        ch.rBassTop = c.addResistor (tb, bw, 500.0e3);
        ch.rBassBot = c.addResistor (bw, bb, 500.0e3);
        ch.rMidTop = c.addResistor (bb, mw, 11.0e3);
        ch.rMidBot = c.addResistor (mw, gnd, 11.0e3);
        c.addResistor (sn, mm, 39.0e3);
        c.addCapacitor (mm, bw, 22.0e-9);
        c.addCapacitor (mm, mw, 22.0e-9);
        c.addResistor (tw, vt, 470.0e3);
        ch.rVolTop = c.addResistor (vt, vw, 235.0e3);
        ch.rVolBot = c.addResistor (vw, gnd, 235.0e3);
        ch.rMix = c.addResistor (vw, sum, 680.0e3);
        const auto p4a = c.addNode(), k4a = c.addNode(), k4b = c.addNode(), mt = c.addNode();
        c.addTriode (p4a, sum, k4a, triode12AX7());
        c.addCapacitor (sum, p4a, cgp);
        c.addResistor (rail, p4a, 120.0e3);
        c.addResistor (k4a, gnd, 640.0);
        c.setInitialGuess (p4a, 200.0);
        c.setInitialGuess (k4a, 1.0);
        c.addTriode (rail, p4a, k4b, triode12AX7());
        c.addCapacitor (p4a, rail, cgp);
        c.addResistor (k4b, gnd, 120.0e3);
        c.setInitialGuess (k4b, 200.0);
        c.addCapacitor (k4b, mt, 220.0e-9);
        ch.qOut = c.addNode();
        ch.rMasterTop = c.addResistor (mt, ch.qOut, 500.0e3);
        ch.rMasterBot = c.addResistor (ch.qOut, gnd, 500.0e3);
        ch.qPlateV4a = p4a;
    }

    // ================================================================ power: V5 LTP -> 4x6L6GC (2 pairs), fixed bias, NFB + Presence/Focus
    // the FULL reference netlist only (reducedOrder replaces all of this with behavioralPowerStage())
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto rail = c.addNode(), piRail = c.addNode(), biasSrc = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        ch.wSrcPi = c.addSource (piRail, screenNominal * piFraction);
        ch.wSrcBias = c.addSource (biasSrc, biasNominal * 1.1);
        ch.wSrcIn = c.addSource (in, 0.0);

        ch.wPiGrid = c.addNode();
        c.addCapacitor (in, ch.wPiGrid, 22.0e-9);

        // PI (Caphead / WJD's VH4 output drawing, 2000-01 era): 12AX7 LTP, 120k / 120k plates; .022 from the Master into
        // grid 1; both grids 1M to the tail node T under the 680R cathode resistor; T -> 10k -> J -> 4.7k -> ground; grid 2
        // AC-grounded to J by .022; the NFB, the Presence and the Deep networks land on J.
        ch.wPiK = c.addNode();
        ch.wPiPlateA = c.addNode();
        ch.wPiPlateB = c.addNode();
        const auto gB = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wNfb = fp;
        c.addResistor (ch.wPiGrid, nm, 1.0e6);
        c.addResistor (gB, nm, 1.0e6);
        c.addTriode (ch.wPiPlateA, ch.wPiGrid, ch.wPiK, triode12AX7());
        c.addTriode (ch.wPiPlateB, gB, ch.wPiK, triode12AX7());
        c.addCapacitor (ch.wPiGrid, ch.wPiPlateA, cgp);
        c.addCapacitor (gB, ch.wPiPlateB, cgp);
        c.addResistor (piRail, ch.wPiPlateA, 120.0e3);
        c.addResistor (piRail, ch.wPiPlateB, 120.0e3);
        c.addResistor (ch.wPiK, nm, 680.0);
        c.addResistor (nm, fp, 10.0e3);
        c.addResistor (fp, gnd, 4.7e3);
        c.addCapacitor (gB, fp, 0.022e-6);
        c.setInitialGuess (ch.wPiPlateA, 240.0);
        c.setInitialGuess (ch.wPiPlateB, 240.0);
        c.setInitialGuess (ch.wPiK, 30.0);
        c.setInitialGuess (nm, 29.0);
        c.setInitialGuess (ch.wPiGrid, 29.0);
        c.setInitialGuess (gB, 29.0);
        c.setInitialGuess (fp, 8.0);

        // .22 couplings, 220k grid leaks to the bias supply, 10k stoppers per tube (5k for a merged pair).
        ch.wBias = c.addNode();
        c.addResistor (biasSrc, ch.wBias, 1.0e3);
        c.addCapacitor (ch.wBias, gnd, 10.0e-6);
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 0.22e-6);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 0.22e-6);
        c.addResistor (ch.wGridA, ch.wBias, 220.0e3);
        c.addResistor (ch.wGridB, ch.wBias, 220.0e3);
        const auto gAs = c.addNode(), gBs = c.addNode();
        c.addResistor (ch.wGridA, gAs, 5.0e3);
        c.addResistor (ch.wGridB, gBs, 5.0e3);
        c.setInitialGuess (ch.wBias, biasNominal);
        c.setInitialGuess (ch.wGridA, biasNominal);
        c.setInitialGuess (ch.wGridB, biasNominal);
        c.setInitialGuess (gAs, biasNominal);
        c.setInitialGuess (gBs, biasNominal);

        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        ch.penA = c.addPentode (ch.wPP1, gAs, gnd, pentodeKT77Pair(), screenNominal);
        ch.penB = c.addPentode (ch.wPP2, gBs, gnd, pentodeKT77Pair(), screenNominal);
        c.setInitialGuess (ch.wPP1, railPlatesNominal - 10.0);
        c.setInitialGuess (ch.wPP2, railPlatesNominal - 10.0);

        c.addCapacitor (ch.wPP1, ch.wPP2, 300.0e-12);
        c.addResistor (ch.wPP1, ch.wPP2, 4.0 * otPrimary);
        c.addCapacitor (ch.wPP1, gnd, 300.0e-12);
        c.addCapacitor (ch.wPP2, gnd, 300.0e-12);

        // Output transformer, centre tap on the plate rail, 16 ohm tap. The secondary's winding sense is the one the
        // Twin Reverb model uses -- the NFB sign depends on it and is verified by the amp staying stable.
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance);
        const double turns = std::sqrt (otPrimary / speakerNominal[matchedSpeaker]) / 2.0;
        const double lh = primaryHalfL;
        const double ls = lh / (turns * turns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, ch.wPP1 }, { a2, ch.wPP2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        ch.wOut = c.addNode();
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

        // NFB (drawing): the speaker -> 10k || 470p -> 4.7k -> J. Presence: J -> .1 -> 25k rheostat, an HF shunt on the
        // loop (more presence = less resistance = less treble fed back = brighter). Deep: J -> .1 -> 10k pot -> the
        // 1-3 H inductor (2 H here) -> ground, an LF shunt (more deep = less resistance = less bass fed back).
        {
            const auto f1 = c.addNode(), f2 = c.addNode();
            c.addResistor (ch.wOut, f1, 10.0e3);
            c.addCapacitor (ch.wOut, f1, 470.0e-12);
            c.addResistor (f1, f2, 4.7e3);
            c.addResistor (f2, fp, 1.0);
            const auto pr = c.addNode();
            c.addCapacitor (fp, pr, 0.1e-6);
            ch.rPresence = c.addResistor (pr, gnd, 12.5e3);
            const auto dc = c.addNode(), fo = c.addNode();
            c.addCapacitor (fp, dc, 0.1e-6);
            ch.rDeep = c.addResistor (dc, fo, 5.0e3);
            c.addCoupledInductors ({ { fo, gnd } }, { 2.0 });
        }
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
    }
}

void DiezelStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const auto split = [] (NodalCircuit& c, int top, int bot, double total, double fraction)
    {
        const double b = juce::jmax (1.0, total * fraction);
        c.setResistance (bot, b);
        c.setResistance (top, juce::jmax (1.0, total - b));
    };
    const double presenceR = juce::jmax (1.0, 25.0e3 * (1.0 - k.presence));
    const double deepR = juce::jmax (1.0, 10.0e3 * (1.0 - k.deep));
    const double biasVolts = biasNominal - biasSpan * (1.0 - 2.0 * k.bias);
    const double rectifier = rectifierR * (0.05 + 0.95 * k.tubeFeel);
    const int sel = juce::jlimit (0, 3, k.channel);

    for (auto& ch : channels)
    {
        // The visible knobs are the selected channel's (ChannelKnobMemory): its Gain in its own preamp netlist; the
        // shared stack / Volume netlist takes its EQ and Volume.
        split (ch.pre[sel], ch.rGainTop[sel], ch.rGainBot[sel], 250.0e3, pots::audio (k.gain));
        split (ch.post, ch.rTrebleTop, ch.rTrebleBot, 220.0e3, k.treble);
        split (ch.post, ch.rBassTop, ch.rBassBot, 1.0e6, pots::audio (k.bass));
        split (ch.post, ch.rMidTop, ch.rMidBot, 22.0e3, k.middle);
        split (ch.post, ch.rVolTop, ch.rVolBot, 470.0e3, pots::audio (k.volume));
        split (ch.post, ch.rMasterTop, ch.rMasterBot, 1.0e6, pots::audio (k.master));
        ch.post.setResistance (ch.rMix, sel == 0 ? 56.0e3 : 680.0e3);   // R35 / R39 R43 R47
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresence, presenceR);
            ch.power.setResistance (ch.rDeep, deepR);
            ch.power.setSource (ch.wSrcBias, biasVolts);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
    }
    if (appliedSpeaker != k.speaker && ! resistiveLoadForced && ! reducedOrder)
    {
        for (auto& ch : channels)
            applySpeaker (ch, k.speaker);
        appliedSpeaker = k.speaker;
    }

    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void DiezelStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    if (resistiveLoadForced || reducedOrder)
        return;
    const double nominal = speakerNominal[juce::jlimit (0, 2, index)];
    const auto sm = speakerModel (nominal);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setResistance (ch.rSpkEddy, speakerEddyLoss * nominal / speakerNominal[matchedSpeaker]);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void DiezelStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    if (reducedOrder)
        return;
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e-3);
        ch.power.setResistance (ch.rSpkEddy, 1.0e9);
        ch.power.setInductorInverse (ch.grpSpkLe, { 1.0e6 });
        ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 });
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-9);
    }
    appliedSpeaker = -2;
    resistiveLoadForced = true;
}

void DiezelStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    for (int i = 0; i < 4; ++i)
        ch.pre[i].restoreDynamicState (ch.preRest[i]);
    ch.post.restoreDynamicState (ch.postRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void DiezelStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (ch.sumCount > 0)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.8, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.2, ch.sumScreen / n + preampAndPiCurrent));
    }
    ch.supply.solveSample();
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const double plates = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sA));
    const double screens = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sB));
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcRail, plates);
        ch.power.setPentodeScreen (ch.penA, screens);
        ch.power.setPentodeScreen (ch.penB, screens);
    }
    constexpr double decouplingTau = 9.1e3 * 20.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double a = 1.0 - std::exp (-dt / decouplingTau);
    ch.piRail += a * (screens * piFraction - ch.piRail);
    const double b = 1.0 - std::exp (-dt / (2.0 * decouplingTau));
    ch.preRail += b * (screens * preFraction - ch.preRail);
    if (! reducedOrder)
        ch.power.setSource (ch.wSrcPi, ch.piRail);
    for (int i = 0; i < 4; ++i)
        ch.pre[i].setSource (ch.pSrcRail[i], ch.preRail);
    ch.post.setSource (ch.qSrcRail, ch.preRail);
}

double DiezelStyleAmplifierProcessor::preampOutput (const Channel& ch, int) const noexcept
{
    return ch.post.voltage (ch.qOut) - ch.qOutDc;
}

double DiezelStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 4.0, releaseMs = 30.0; // solid-state bridge: stiff, fast supply
    const double absDrive = std::abs (driveVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    // reducedOrder folds the power-only controls into the fit (same recipe as the
    // big-iron amps): tube feel scales sag depth, bias shifts the knee's operating
    // point, presence scales the HF shelf, focus tightens the low-end sag — all
    // centred on the shipped defaults so noon response is unchanged.
    const double feel = juce::jlimit (0.0, 1.0, (double) lastKnobs.tubeFeel);
    const double deep = juce::jlimit (0.0, 1.0, (double) lastKnobs.deep); // Deep on the LF-NFB leg
    ch.bmRail = bmSagRail[0] - feel * (1.3 - 0.6 * deep) * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));

    const double asym = 0.3 * (juce::jlimit (0.0, 1.0, (double) lastKnobs.bias) - 0.5) * bmYmax;
    const double drive = driveVoltage + asym;
    const double k = ch.bmRail * bmYmax / bmGain0;
    const auto knee = [&] (double x) { return bmYmax * x / std::pow (1.0 + std::pow (x, bmKneeN), 1.0 / bmKneeN); };
    // signed odd saturator: copysign(knee(|x|), x) — the unsigned knee difference would
    // reverse the waveform near zero for off-centre bias (review BUG_0001).
    const auto f = [&] (double x) { const double ux = std::abs (x) / juce::jmax (1.0e-9, k);
                                    return std::copysign (knee (ux), x); };
    const double raw = (f (drive) - f (drive - driveVoltage)) * ch.bmRail;

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    const double hfGain = bmShelfHfGain * (0.4 + 1.2 * juce::jlimit (0.0, 1.0, (double) lastKnobs.presence));
    // Focus also needs a level-independent path: it voices the low-frequency NFB even
    // when no sag is engaged, so it rides a ~120 Hz LP band, not the sag depth alone.
    const double lowCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 120.0 / juce::jmax (1.0, sampleRate));
    ch.bmLowState += lowCoeff * (raw - ch.bmLowState);
    ch.bmOutput = ch.bmTail.process (bmLevelTrim * (ch.bmToneState + hfGain * (raw - ch.bmToneState) + (deep - 0.5) * 1.6 * ch.bmLowState));
    return ch.bmOutput;
}

double DiezelStyleAmplifierProcessor::debugVoltage (Probe p, int stage) const noexcept
{
    const auto& ch = channels[0];
    const auto& cp = ch.pre[(size_t) debugChannel];
    switch (p)
    {
        case Probe::chanPlate: return cp.voltage (ch.pPlate[debugChannel][(size_t) juce::jlimit (0, 3, stage - 1)]);
        case Probe::chanOut: return cp.voltage (ch.pOut[debugChannel]);
        case Probe::piPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPiPlateA);
        case Probe::piPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPiPlateB);
        case Probe::piCathode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPiK);
        case Probe::powerGridA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridA);
        case Probe::powerGridB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridB);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wBias);
        case Probe::nfb: return reducedOrder ? 0.0 : ch.power.voltage (ch.wNfb);
    }
    return 0.0;
}

double DiezelStyleAmplifierProcessor::plateCurrentA() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].penA);
}

double DiezelStyleAmplifierProcessor::plateCurrentB() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].penB);
}

void DiezelStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    resistiveLoadForced = false;
    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto bmVoicing = voicings::highGain;
        bmVoicing.resDb = 1.0; // shelf path carries no fitted LF bump -- a hint of the real resonance back
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
    }

    auto setup = [&] (juce::SmoothedValue<float>& sv, juce::AudioParameterFloat* p, double seconds)
    {
        sv.reset (newSampleRate, seconds);
        sv.setCurrentAndTargetValue (p != nullptr ? p->get() : 0.0f);
    };
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMiddle, middleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedMaster, masterParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedDeep, deepParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);
    setup (smoothedOutput, outputParam, 0.02);

    appliedSpeaker = -1;
    // Every channel's Gain from its stored row (each preamp netlist owns its pot), then the visible channel on top.
    const int visible = juce::jlimit (0, 3, juce::roundToInt (channelParam->get()));
    for (int c = 0; c < 4; ++c)
        updatePots ({ knobMemory->slotValue (c, 0), trebleParam->get(), middleParam->get(), bassParam->get(), volumeParam->get(),
                      masterParam->get(), presenceParam->get(), deepParam->get(), powerParam->get(), biasParam->get(),
                      tubeFeelParam->get(), juce::roundToInt (speakerParam->get()), c });
    updatePots ({ gainParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(), volumeParam->get(),
                  masterParam->get(), presenceParam->get(), deepParam->get(), powerParam->get(), biasParam->get(),
                  tubeFeelParam->get(), juce::roundToInt (speakerParam->get()), visible });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        // reducedOrder never updates isRun from real pentode screens: seed it with the physical
        // estimate (4x KT77 idle screens ~= 3 mA each) instead of the full-mode starting guess so
        // the converged screen rail lands at the documented ~420 V, not ~10 V lower per extra mA.
        double ipRun = 2.0 * idlePlateCurrent, isRun = reducedOrder ? 0.012 : 0.02;
        for (int pass = 0; pass < 10; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -(isRun + preampAndPiCurrent));
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierR * (ipRun + isRun + preampAndPiCurrent));
            passOk = ch.supply.prepare (supplyRate);
            const double plates = ch.supply.voltage (ch.sA), screens = ch.supply.voltage (ch.sB);
            ch.piRail = screens * piFraction;
            ch.preRail = screens * preFraction;

            for (int i = 0; i < 4; ++i)
            {
                ch.pre[i].setSource (ch.pSrcRail[i], ch.preRail);
                passOk = ch.pre[i].prepare (newSampleRate) && passOk;
                ch.pre[i].solveSample();
            }
            ch.post.setSource (ch.qSrcRail, ch.preRail);
            ch.post.setSource (ch.qSrcIn, 0.0);
            passOk = ch.post.prepare (newSampleRate) && passOk;

            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, 0.0);
                ch.power.setSource (ch.wSrcRail, plates);
                ch.power.setSource (ch.wSrcPi, ch.piRail);
                ch.power.setPentodeScreen (ch.penA, screens);
                ch.power.setPentodeScreen (ch.penB, screens);
                passOk = ch.power.prepare (newSampleRate) && passOk;
                ch.power.solveSample();
                double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ipRun += 0.5 * ((ipA + ipB) - ipRun);
                isRun += 0.5 * ((isA + isB) - isRun);
            }
            else
            {
                ch.power.prepare (newSampleRate); // empty circuit in reducedOrder
            }
        }
        dcOk = passOk && dcOk;
        for (int i = 0; i < 4; ++i)
        {
            ch.outDc[i] = ch.pre[i].voltage (ch.pOut[i]);
            ch.pre[i].saveDynamicState (ch.preRest[i]);
        }
        ch.post.solveSample();
        ch.qOutDc = ch.post.voltage (ch.qOut);
        ch.post.saveDynamicState (ch.postRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.chanTrim = std::pow (10.0, channelLevelMatchDb[(size_t) visible] / 20.0);
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void DiezelStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedGain.setTargetValue (gainParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedMaster.setTargetValue (masterParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedDeep.setTargetValue (deepParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int channelChoice = juce::roundToInt (channelParam->get());
    // Per-channel level match (user request 2026-11-02: a channel change must not jump in level), eased over ~10 ms.
    const double chanTrimTarget = std::pow (10.0, channelLevelMatchDb[(size_t) juce::jlimit (0, 3, channelChoice)] / 20.0);
    const double chanTrimAlpha = 1.0 - std::exp (-1.0 / (0.01 * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float gn = smoothedGain.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMiddle.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float ms = smoothedMaster.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float fc = smoothedDeep.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();
        const float ou = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, vo, ms, pr, fc, pw, bi, fe, speakerChoice, channelChoice });
        }

        // Power Drive (page 2) scales the Master's output into the power section: unity at its full default.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            if (channelChoice != ch.appliedChannel)
            {
                ch.appliedChannel = channelChoice;
                ch.chanFade = 0.0; // ~4 ms ramp-in absorbs the level jump on a live channel switch
            }
            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            const int sel = juce::jlimit (0, 3, channelChoice);
            auto& cur = ch.pre[(size_t) sel];
            cur.setSource (ch.pSrcIn[sel], x);
            bool okPre = cur.solveSample();
            ch.post.setSource (ch.qSrcIn, cur.voltage (ch.pOut[sel]) - ch.outDc[sel]);
            okPre = ch.post.solveSample() && okPre;

            // reducedOrder: ch.power is empty, nothing to solve; the behavioural stage takes the drive directly.
            const double drive = masterGain * preampOutput (ch, channelChoice) * ch.chanFade;
            ch.chanFade = juce::jmin (1.0, ch.chanFade + 1.0 / (0.004 * sampleRate));
            bool okPower = true;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, drive);
                okPower = ch.power.solveSample();
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
            }
            bool ok = okPre && okPower;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += okPower ? 0 : 1;
            }

            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, drive) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 140.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;

            if (ok)
            {
                ch.failStreak = 0;
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.pre[sel].saveDynamicState (ch.preRest[sel]);
                    ch.post.saveDynamicState (ch.postRest);
                    ch.power.saveDynamicState (ch.powerRest);
                    ch.supply.saveDynamicState (ch.supplyRest);
                }
            }
            else if (++ch.failStreak >= 4)
            {
                recover (ch);
                ch.restRefreshCounter = 0;
            }

            double out = ch.lastEmitted;
            if (sane)
            {
                ch.chanTrim += chanTrimAlpha * (chanTrimTarget - ch.chanTrim);
                out = speakerVolts * (reducedOrder ? outputScale : fullOutputScale) * outGain * speakerGain * ch.chanTrim;
                if (ch.alignOutput)
                {
                    ch.declick = ch.lastEmitted - out;
                    ch.alignOutput = false;
                }
                out += ch.declick;
                ch.declick *= declickDecay;
            }
            ch.lastEmitted = out;
            data[i] = (float) out;

            if (chIdx == 0)
            {
                ++sampleCount;
                lastSampleOk = ok;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

std::unique_ptr<juce::XmlElement> DiezelStyleAmplifierProcessor::getState() const
{
    auto xml = EffectProcessor::getState();
    knobMemory->writeState (*xml);
    return xml;
}

void DiezelStyleAmplifierProcessor::setState (const juce::XmlElement& state)
{
    {
        const ChannelKnobMemory::ScopedSuspend a (*knobMemory);
        EffectProcessor::setState (state);
    }
    knobMemory->readState (state);
}

void DiezelStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
