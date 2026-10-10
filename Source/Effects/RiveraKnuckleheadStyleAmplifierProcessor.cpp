#include "RiveraKnuckleheadStyleAmplifierProcessor.h"
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

    /** TWO 6L6GC as one push-pull pair (the real amp runs four): Koren's own 6L6GC set with the pair-doubling the
        Twin Reverb model uses (kg1/2 halved, grid current doubled, arc resistance halved). */
    KorenPentode::Parameters pentode6L6Pair()
    {
        KorenPentode::Parameters p;
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // K100 rails: solid-state bridge (printed "Rect.S 800V PIV"), ~460 V plates, ~445 V screens, TP4 318 V phase
    // inverter, TP12 350 V -> ~250 V preamp droppers.
    constexpr double railPlatesNominal = 460.0;
    constexpr double rectifierR = 40.0;
    constexpr double reservoir = 100.0e-6;
    constexpr double screenResistor = 500.0;
    constexpr double piFraction = 318.0 / 445.0;
    constexpr double preFraction = 250.0 / 445.0;
    constexpr double idlePlateCurrent = 0.070;   // per pair
    constexpr double preampAndPiCurrent = 0.009;
    constexpr double biasNominal = -47.0;        // TP41 mark for 6L6GC
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

    // ---- reduced-order power stage: calibration data, KNK_POWERCAL in the test file ----
    // Plate rail vs. the preamp-signal drive PEAK, measured on the full reference model (lead channel, Gain 1,
    // Master 1, Power Drive max, 16 ohm). Stiff solid-state bridge: modest sag.
    constexpr int bmSagPoints = 9;
    constexpr double bmSagDrive[bmSagPoints] = { 0.02, 0.08, 0.25, 0.70, 1.80, 4.50, 9.00, 15.0, 22.0 };
    constexpr double bmSagRail[bmSagPoints] = { 460.0, 459.0, 457.0, 452.0, 443.0, 430.0, 418.0, 410.0, 405.0 };

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

RiveraKnuckleheadStyleAmplifierProcessor::RiveraKnuckleheadStyleAmplifierProcessor()
{
    const juce::String px ("knk_");
    auto make = [&] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (px + id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto channel = std::make_unique<juce::AudioParameterFloat> (
        px + "channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("Clean") : juce::String ("Lead"); }));
    auto volume = make ("volume", "Volume", 0.5f);
    auto gain = make ("gain", "Gain", 0.5f);
    auto treble = make ("treble", "Treble", 0.5f);
    auto middle = make ("middle", "Middle", 0.5f);
    auto bass = make ("bass", "Bass", 0.5f);
    auto master = make ("master", "Master", 0.8f);
    auto presence = make ("presence", "Presence", 0.5f);
    auto focus = make ("focus", "Focus", 0.5f);
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
    volumeParam = volume.get();
    gainParam = gain.get();
    trebleParam = treble.get();
    middleParam = middle.get();
    bassParam = bass.get();
    masterParam = master.get();
    presenceParam = presence.get();
    focusParam = focus.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();
    outputParam = output.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("rivera_knucklehead", "Rivera Knucklehead-Style Amplifier", "|", std::move (channel));
    group->addChild (std::move (volume));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (master));
    group->addChild (std::move (presence));
    group->addChild (std::move (focus));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("rivera_knucklehead_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void RiveraKnuckleheadStyleAmplifierProcessor::buildChannel (Channel& ch)
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

    // ================================================================ preamp: CH1 clean path and CH2 lead path as two
    // netlists -- only the selected channel is solved per sample (the channel switch is a relay, so the
    // dormant circuit is simply not computed until selected; it re-settles from its saved state).
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, screenNominal * preFraction);

        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double plateR, double rk, double ck,
                          NodalCircuit::Node& plate, NodalCircuit::Node& cathode, double plateGuess, double cathodeGuess)
        {
            const auto g = c.addNode();
            plate = c.addNode();
            cathode = c.addNode();
            c.addResistor (gIn, g, stopper);
            if (leak > 0.0)
                c.addResistor (g, gnd, leak);
            c.addTriode (plate, g, cathode, triode12AX7());
            c.addCapacitor (g, plate, cgp);
            c.addResistor (rail, plate, plateR);
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
        };

        // Rivera TMB (anode-driven, no cathode follower): 270 pF treble cap, 33k slope, .047 bass + .1 mid caps,
        // 250k audio treble, 250k linear bass, linear mid (50k CH1 / 25k CH2). Returns the treble wiper.
        auto toneStack = [&] (NodalCircuit::Node drive, double midMax,
                              int& rTrebleTop, int& rTrebleBot, int& rBass, int& rMid)
        {
            const auto tt = c.addNode(), tw = c.addNode(), tb = c.addNode(), x = c.addNode(), mt = c.addNode();
            c.addCapacitor (drive, tt, 270.0e-12);
            rTrebleTop = c.addResistor (tt, tw, 125.0e3);
            rTrebleBot = c.addResistor (tw, tb, 125.0e3);
            c.addResistor (drive, x, 33.0e3);
            c.addCapacitor (x, tb, 47.0e-9);
            c.addCapacitor (x, mt, 100.0e-9);
            rBass = c.addResistor (tb, mt, 125.0e3);
            rMid = c.addResistor (mt, gnd, midMax * 0.5);
            return tw;
        };

        // ---- CH1 (clean): V1A -> Volume -> CH1 EQ -> V4A recovery -> Master
        stage (in, 68.0e3, 1.0e6, 47.0e3, 1.5e3, 22.0e-6, ch.pPlateC1, ch.pKC1, 170.0, 1.4); // R103 47K printed
        {
            const auto d = c.addNode(), w = c.addNode();
            c.addCapacitor (ch.pPlateC1, d, 22.0e-9);
            ch.rVolTop = c.addResistor (d, w, 900.0e3);   // VR101 1MEG
            ch.rVolBot = c.addResistor (w, gnd, 100.0e3);
            const auto tw = toneStack (w, 50.0e3, ch.rTrebleTopC, ch.rTrebleBotC, ch.rBassC, ch.rMidC);
            // V4A recovery: 22k plate (R133), 1k5 + 22 uF cathode (R135/C124)
            const auto g4 = c.addNode();
            c.addResistor (tw, g4, 68.0e3);
            c.addResistor (g4, gnd, 1.0e6);
            const auto c4p = c.addNode(), c4k = c.addNode(), mc = c.addNode(), mw = c.addNode();
            c.addTriode (c4p, g4, c4k, triode12AX7());
            c.addCapacitor (g4, c4p, cgp);
            c.addResistor (rail, c4p, 22.0e3);
            c.addResistor (c4k, gnd, 1.5e3);
            c.addCapacitor (c4k, gnd, 22.0e-6);
            c.setInitialGuess (c4p, 220.0);
            c.setInitialGuess (c4k, 1.5);
            ch.pPlateC4 = c4p;
            ch.pKC4 = c4k;
            c.addCapacitor (c4p, mc, 22.0e-9);
            ch.rMasterCleanTop = c.addResistor (mc, mw, 800.0e3); // VR105 1MEG master
            ch.rMasterCleanBot = c.addResistor (mw, gnd, 200.0e3);
            ch.pOutClean = mw;
        }

    }

    // ---- CH2 (lead): V1B -> Gain -> CH2 EQ -> V2A -> V2B -> Master -- its own netlist
    {
        auto& c = ch.preL;
        const auto in = c.addNode();
        ch.pSrcInL = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRailL = c.addSource (rail, screenNominal * preFraction);

        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double plateR, double rk, double ck,
                          NodalCircuit::Node& plate, NodalCircuit::Node& cathode, double plateGuess, double cathodeGuess)
        {
            const auto g = c.addNode();
            plate = c.addNode();
            cathode = c.addNode();
            c.addResistor (gIn, g, stopper);
            if (leak > 0.0)
                c.addResistor (g, gnd, leak);
            c.addTriode (plate, g, cathode, triode12AX7());
            c.addCapacitor (g, plate, cgp);
            c.addResistor (rail, plate, plateR);
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
        };

        auto toneStack = [&] (NodalCircuit::Node drive, double midMax,
                              int& rTrebleTop, int& rTrebleBot, int& rBass, int& rMid)
        {
            const auto tt = c.addNode(), tw = c.addNode(), tb = c.addNode(), x = c.addNode(), mt = c.addNode();
            c.addCapacitor (drive, tt, 270.0e-12);
            rTrebleTop = c.addResistor (tt, tw, 125.0e3);
            rTrebleBot = c.addResistor (tw, tb, 125.0e3);
            c.addResistor (drive, x, 33.0e3);
            c.addCapacitor (x, tb, 47.0e-9);
            c.addCapacitor (x, mt, 100.0e-9);
            rBass = c.addResistor (tb, mt, 125.0e3);
            rMid = c.addResistor (mt, gnd, midMax * 0.5);
            return tw;
        };

        stage (in, 68.0e3, 1.0e6, 100.0e3, 1.5e3, 22.0e-6, ch.pPlateL1, ch.pKL1, 180.0, 1.4);
        {
            const auto d = c.addNode(), w = c.addNode();
            c.addCapacitor (ch.pPlateL1, d, 22.0e-9);
            ch.rGainTop = c.addResistor (d, w, 900.0e3);  // CH2 gain 1M
            ch.rGainBot = c.addResistor (w, gnd, 100.0e3);
            const auto tw = toneStack (w, 25.0e3, ch.rTrebleTopL, ch.rTrebleBotL, ch.rBassL, ch.rMidL);
            const auto c2 = c.addNode();
            c.addResistor (tw, c2, 1.0e6);
            // V2A: 1k5 + 22 uF, 100k
            stage (c2, 68.0e3, 0.0, 100.0e3, 1.5e3, 22.0e-6, ch.pPlateL2, ch.pKL2, 200.0, 1.6);
            const auto c3 = c.addNode();
            c.addCapacitor (ch.pPlateL2, c3, 22.0e-9);
            // V2B: 1k5 + 1 uF, 100k (the colder clipper stage)
            stage (c3, 1.0, 1.0e6, 100.0e3, 1.5e3, 1.0e-6, ch.pPlateL3, ch.pKL3, 220.0, 1.8);
            const auto mc = c.addNode(), mw = c.addNode();
            c.addCapacitor (ch.pPlateL3, mc, 22.0e-9);
            ch.rMasterLeadTop = c.addResistor (mc, mw, 200.0e3);  // VR110 250K master
            ch.rMasterLeadBot = c.addResistor (mw, gnd, 50.0e3);
            ch.pOutLead = mw;
        }
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

        // LTP: 82k5 / 100k plates (R205/R206), 470k grid leaks (R202/R204) to the tail mid node, 680R (R203)
        // cathode->mid, 8k8 (R209/R210) mid->feedback node, .1 uF (C203) feedback->ground-side grid.
        ch.wPiK = c.addNode();
        ch.wPiPlateA = c.addNode();
        ch.wPiPlateB = c.addNode();
        const auto gB = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wNfb = fp;
        c.addResistor (ch.wPiGrid, nm, 470.0e3);
        c.addResistor (gB, nm, 470.0e3);
        c.addTriode (ch.wPiPlateA, ch.wPiGrid, ch.wPiK, triode12AX7());
        c.addTriode (ch.wPiPlateB, gB, ch.wPiK, triode12AX7());
        c.addCapacitor (ch.wPiGrid, ch.wPiPlateA, cgp);
        c.addCapacitor (gB, ch.wPiPlateB, cgp);
        c.addResistor (piRail, ch.wPiPlateA, 82.5e3);
        c.addResistor (piRail, ch.wPiPlateB, 100.0e3);
        c.addResistor (ch.wPiK, nm, 680.0);
        c.addResistor (nm, fp, 8.8e3);
        c.addCapacitor (fp, gB, 0.1e-6);
        c.setInitialGuess (ch.wPiPlateA, 240.0);
        c.setInitialGuess (ch.wPiPlateB, 235.0);
        c.setInitialGuess (ch.wPiK, 33.0);
        c.setInitialGuess (nm, 30.0);
        c.setInitialGuess (ch.wPiGrid, 30.0);
        c.setInitialGuess (gB, 30.0);
        c.setInitialGuess (fp, 0.0);

        // .022 couplings (C204/C205), 150k grid leaks to bias (R207/R208), 1k stoppers.
        ch.wBias = c.addNode();
        c.addResistor (biasSrc, ch.wBias, 1.0e3);
        c.addCapacitor (ch.wBias, gnd, 10.0e-6);
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 22.0e-9);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 22.0e-9);
        c.addResistor (ch.wGridA, ch.wBias, 150.0e3);
        c.addResistor (ch.wGridB, ch.wBias, 150.0e3);
        const auto gAs = c.addNode(), gBs = c.addNode();
        c.addResistor (ch.wGridA, gAs, 1.0e3);
        c.addResistor (ch.wGridB, gBs, 1.0e3);
        c.setInitialGuess (ch.wBias, biasNominal);
        c.setInitialGuess (ch.wGridA, biasNominal);
        c.setInitialGuess (ch.wGridB, biasNominal);
        c.setInitialGuess (gAs, biasNominal);
        c.setInitialGuess (gBs, biasNominal);

        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        ch.penA = c.addPentode (ch.wPP1, gAs, gnd, pentode6L6Pair(), screenNominal);
        ch.penB = c.addPentode (ch.wPP2, gBs, gnd, pentode6L6Pair(), screenNominal);
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

        // Global NFB: 47k (R201) from the speaker terminal into the PI tail's feedback node. Presence (VR112 25k +
        // C203 .1 uF) shunts feedback at HF; Focus (VR111 250k + 2.2 uF) shunts it at LF. Knob up = less R = more
        // feedback lifted = more of that band in the output.
        c.addResistor (ch.wOut, fp, 47.0e3);
        {
            const auto pr = c.addNode();
            ch.rPresence = c.addResistor (fp, pr, 15.0e3);
            c.addCapacitor (pr, gnd, 0.1e-6);
            const auto fo = c.addNode();
            ch.rFocus = c.addResistor (fp, fo, 60.0e3);
            c.addCapacitor (fo, gnd, 2.2e-6);
        }
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
    }
}

void RiveraKnuckleheadStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * pots::audio (k.treble));
    const double bass = juce::jmax (1.0, 250.0e3 * k.bass);
    const double midC = juce::jmax (1.0, 50.0e3 * k.middle);
    const double midL = juce::jmax (1.0, 25.0e3 * k.middle);
    const double masterBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.master));
    const double masterLBottom = juce::jmax (1.0, 250.0e3 * pots::audio (k.master));
    // Presence/Focus knobs up = less series R = more feedback shunted = more of that band at the output.
    const double presenceR = 200.0 + 25.0e3 * (1.0 - k.presence);
    const double focusR = 1.0e3 + 249.0e3 * (1.0 - k.focus);
    const double biasVolts = biasNominal - biasSpan * (1.0 - 2.0 * k.bias); // knob up = hotter (less negative)
    const double rectifier = rectifierR * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        ch.preL.setResistance (ch.rGainTop, juce::jmax (1.0, 1.0e6 - gainBottom));
        ch.preL.setResistance (ch.rGainBot, gainBottom);
        ch.pre.setResistance (ch.rTrebleTopC, juce::jmax (1.0, 250.0e3 - trebleBottom));
        ch.pre.setResistance (ch.rTrebleBotC, trebleBottom);
        ch.pre.setResistance (ch.rBassC, bass);
        ch.pre.setResistance (ch.rMidC, midC);
        ch.preL.setResistance (ch.rTrebleTopL, juce::jmax (1.0, 250.0e3 - trebleBottom));
        ch.preL.setResistance (ch.rTrebleBotL, trebleBottom);
        ch.preL.setResistance (ch.rBassL, bass);
        ch.preL.setResistance (ch.rMidL, midL);
        ch.pre.setResistance (ch.rMasterCleanTop, juce::jmax (1.0, 1.0e6 - masterBottom));
        ch.pre.setResistance (ch.rMasterCleanBot, masterBottom);
        ch.preL.setResistance (ch.rMasterLeadTop, juce::jmax (1.0, 250.0e3 - masterLBottom));
        ch.preL.setResistance (ch.rMasterLeadBot, masterLBottom);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresence, presenceR);
            ch.power.setResistance (ch.rFocus, focusR);
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

void RiveraKnuckleheadStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void RiveraKnuckleheadStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void RiveraKnuckleheadStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.preL.restoreDynamicState (ch.preLRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void RiveraKnuckleheadStyleAmplifierProcessor::updateSupply (Channel& ch) const
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
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
    ch.preL.setSource (ch.pSrcRailL, ch.preRail);
}

double RiveraKnuckleheadStyleAmplifierProcessor::preampOutput (const Channel& ch, int channel) const noexcept
{
    return channel == 0 ? ch.pre.voltage (ch.pOutClean) - ch.outDcClean
                        : ch.preL.voltage (ch.pOutLead) - ch.outDcLead;
}

double RiveraKnuckleheadStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
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
    const double focus = juce::jlimit (0.0, 1.0, (double) lastKnobs.focus);
    ch.bmRail = bmSagRail[0] - feel * (1.3 - 0.6 * focus) * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));

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
    ch.bmOutput = ch.bmToneState + hfGain * (raw - ch.bmToneState) + (focus - 0.5) * 1.6 * ch.bmLowState;
    return ch.bmOutput;
}

double RiveraKnuckleheadStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::cleanPlate1: return ch.pre.voltage (ch.pPlateC1);
        case Probe::cleanCath1: return ch.pre.voltage (ch.pKC1);
        case Probe::leadPlate1: return ch.preL.voltage (ch.pPlateL1);
        case Probe::leadCath1: return ch.preL.voltage (ch.pKL1);
        case Probe::leadPlate2: return ch.preL.voltage (ch.pPlateL2);
        case Probe::leadPlate3: return ch.preL.voltage (ch.pPlateL3);
        case Probe::cleanOut: return ch.pre.voltage (ch.pOutClean);
        case Probe::leadOut: return ch.preL.voltage (ch.pOutLead);
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

double RiveraKnuckleheadStyleAmplifierProcessor::plateCurrentA() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].penA);
}

double RiveraKnuckleheadStyleAmplifierProcessor::plateCurrentB() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].penB);
}

void RiveraKnuckleheadStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    resistiveLoadForced = false;
    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
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
    setup (smoothedFocus, focusParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);
    setup (smoothedOutput, outputParam, 0.02);

    appliedSpeaker = -1;
    updatePots ({ volumeParam->get(), gainParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(),
                  masterParam->get(), presenceParam->get(), focusParam->get(), powerParam->get(), biasParam->get(),
                  tubeFeelParam->get(), juce::roundToInt (speakerParam->get()), juce::roundToInt (channelParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        double ipRun = 2.0 * idlePlateCurrent, isRun = 0.02;
        for (int pass = 0; pass < 10; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -(isRun + preampAndPiCurrent));
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierR * (ipRun + isRun + preampAndPiCurrent));
            passOk = ch.supply.prepare (supplyRate);
            const double plates = ch.supply.voltage (ch.sA), screens = ch.supply.voltage (ch.sB);
            ch.piRail = screens * piFraction;
            ch.preRail = screens * preFraction;

            ch.pre.setSource (ch.pSrcRail, ch.preRail);
            ch.preL.setSource (ch.pSrcRailL, ch.preRail);
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            passOk = ch.preL.prepare (newSampleRate) && passOk;
            ch.pre.solveSample();
            ch.preL.solveSample();

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
        ch.outDcClean = ch.pre.voltage (ch.pOutClean);
        ch.outDcLead = ch.preL.voltage (ch.pOutLead);
        ch.pre.saveDynamicState (ch.preRest);
        ch.preL.saveDynamicState (ch.preLRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void RiveraKnuckleheadStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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
    smoothedFocus.setTargetValue (focusParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int channelChoice = juce::roundToInt (channelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float gn = smoothedGain.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMiddle.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float ms = smoothedMaster.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float fc = smoothedFocus.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();
        const float ou = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, gn, tr, mi, ba, ms, pr, fc, pw, bi, fe, speakerChoice, channelChoice });
        }

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
            bool okPre = true;
            if (channelChoice == 0)
            {
                ch.pre.setSource (ch.pSrcIn, x);
                okPre = ch.pre.solveSample();
            }
            else
            {
                ch.preL.setSource (ch.pSrcInL, x);
                okPre = ch.preL.solveSample();
            }

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
                    ch.pre.saveDynamicState (ch.preRest);
                    ch.preL.saveDynamicState (ch.preLRest);
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
                out = speakerVolts * (reducedOrder ? outputScale : fullOutputScale) * outGain * speakerGain;
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

void RiveraKnuckleheadStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
