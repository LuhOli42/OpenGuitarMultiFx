#include "TrainwreckExpressStyleAmplifierProcessor.h"
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

    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set, same as every other amp here

    /** ONE EL34 (each output tube has its own grid and plate here, a real push-pull pair): the Super Lead's fitted EL34 set
        (SuperLeadStyleAmplifierProcessor.cpp, pentodeEL34Pair()) with the pair-doubling undone -- mu 8.11, Ex 1.50, Kg1 1201,
        Kg2 3720, Kp 100, Kvb 24. That set was fitted to a plexi's operating region (~35 mA at -55 V / 480 V); the Express
        idles its EL34s at lower plate voltage and less negative grid, inside the same curve family. */
    KorenPentode::Parameters pentodeEL34()
    {
        KorenPentode::Parameters p;
        p.mu = 8.11;
        p.ex = 1.50;
        p.kg1 = 1201.0;
        p.kg2 = 3720.0;
        p.kp = 100.0;
        p.kvb = 24.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.999;
    constexpr double primaryHalfResistance = 60.0;
    constexpr double secondaryResistance = 0.2;
    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2; // the transformer's 16 ohm tap

    constexpr double screenResistor = 1.0e3;       // plate node -> screen node dropper (docs: "1k" on the Express drawings)
    constexpr double preampAndPiCurrent = 0.006;   // A: three preamp triodes + the PI, drawn from the screen node (estimated)
}

/** Everything that differs between the Express and the Concorde (docs/circuits/TrainwreckExpress.md, KometConcorde.md: documented vs estimated). */
struct TrainwreckExpressStyleAmplifierProcessor::Spec
{
    const char* groupId;
    const char* groupName;
    const char* prefix;
    bool concorde;
    double railPlates;      // idle plate rail
    double rectifierR;      // diodes + transformer winding
    double reservoir;       // first filter cap
    double piFraction;      // PI rail / screen node
    double preFraction;     // preamp rail / screen node
    double otPrimary;       // plate-to-plate ohms on the 16 ohm tap
    double primaryHalfL;    // H per half
    double biasNominal;     // grid volts with the trimmer at noon
    double biasSpan;        // +/- volts over the Bias knob
    double idlePlateCurrent;
};

const TrainwreckExpressStyleAmplifierProcessor::Spec& TrainwreckExpressStyleAmplifierProcessor::spec() const noexcept
{
    // Express: rails from the Express drawings (395 / 380 / 295 / 282 / 267 V) and builders' field readings (EL34 plates
    // ~369-389 V); ~5k primary. Bias: Fischer sets a real Express to -30 V; the fitted EL34 set below (the Super Lead's, whose
    // cutoff sits lower than a real tube's -- a plexi is -55 V there) reaches a comparable ~40 mA idle at about -41 V, so noon
    // is -41 V (estimated operating-point match, not a voltage match).
    static const Spec express { "trainwreck_express", "Trainwreck Express-Style Amplifier", "twx_", false,
                                395.0, 60.0, 80.0e-6, 295.0 / 380.0, 275.0 / 380.0, 5000.0, 6.0, -41.0, 6.0, 0.040 };
    // Concorde: 4k8 primary and diode rectification reported by an Amp Garage builder; rails ESTIMATED (diode rectifier ->
    // higher than the Express); bias ESTIMATED for the same ~40 mA idle at those rails.
    static const Spec concorde { "komet_concorde", "Komet Concorde-Style Amplifier", "kcd_", true,
                                 460.0, 60.0, 100.0e-6, 335.0 / 445.0, 300.0 / 445.0, 4800.0, 6.0, -51.0, 7.0, 0.040 };
    return model == Model::concorde ? concorde : express;
}

TrainwreckExpressStyleAmplifierProcessor::TrainwreckExpressStyleAmplifierProcessor()
    : TrainwreckExpressStyleAmplifierProcessor (Model::express)
{
}

TrainwreckExpressStyleAmplifierProcessor::TrainwreckExpressStyleAmplifierProcessor (Model m)
    : model (m)
{
    const auto& s = spec();
    const juce::String px (s.prefix);
    auto make = [&] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (px + id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("volume", "Volume", 0.5f);
    auto treble = make ("treble", "Treble", 0.5f);
    auto middle = make ("middle", "Middle", 0.5f);
    auto bass = make ("bass", "Bass", 0.5f);
    auto presence = make ("presence", "Presence", 0.5f);
    auto output = make ("output", "Output", 0.5f);
    auto power = make ("power", "Power Drive", 1.0f);
    auto bias = make ("bias", "Bias", 0.5f);
    auto feel = make ("tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        px + "speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    volumeParam = volume.get();
    trebleParam = treble.get();
    middleParam = middle.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (s.groupId, s.groupName, "|", std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    if (s.concorde)
    {
        auto hiCut = make ("hicut", "Hi-Cut", 0.3f);
        auto touch = std::make_unique<juce::AudioParameterFloat> (
            px + "touch", "Touch", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return juce::roundToInt (v) == 0 ? juce::String ("Fast") : juce::String ("Gradual"); }));
        hiCutParam = hiCut.get();
        touchParam = touch.get();
        group->addChild (std::move (hiCut));
        group->addChild (std::move (touch));
    }
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> (juce::String (s.groupId) + "_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void TrainwreckExpressStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const auto& s = spec();
    const double screenNominal = s.railPlates - screenResistor * (2.0 * 0.006 + preampAndPiCurrent);

    // ================================================================ supply: diodes -> reservoir (plates) -> 1k -> screens
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.srcVoc = c.addSource (vo, s.railPlates);
        ch.rRect = c.addResistor (vo, ch.sA, s.rectifierR);
        c.addCapacitor (ch.sA, gnd, s.reservoir);
        c.addResistor (ch.sA, ch.sB, screenResistor);
        c.addCapacitor (ch.sB, gnd, 40.0e-6); // the 20 uF screen cap plus the downstream filter caps, lumped
        ch.iA = c.addCurrentSource (ch.sA, -2.0 * s.idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -preampAndPiCurrent);
        c.setInitialGuess (ch.sA, s.railPlates);
        c.setInitialGuess (ch.sB, screenNominal);
    }

    // ================================================================ preamp
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, screenNominal * s.preFraction);

        // A plain 12AX7 gain stage: grid stopper, grid leak, plate load, cathode resistor (+ optional bypass).
        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double rk, double ck,
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
            c.addResistor (rail, plate, 100.0e3);
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
        };

        // Anode-driven Trainwreck stack (Marshall TMB topology, no cathode follower): 500 pF treble cap, 100k slope,
        // .022 + .022 bass / mid caps, 250k audio treble, 250k linear bass, 25k linear middle. Returns the treble wiper.
        auto toneStack = [&] (NodalCircuit::Node drive)
        {
            const auto tt = c.addNode(), tw = c.addNode(), tb = c.addNode(), x = c.addNode(), mt = c.addNode();
            c.addCapacitor (drive, tt, 500.0e-12);
            ch.rTrebleTop = c.addResistor (tt, tw, 125.0e3);
            ch.rTrebleBot = c.addResistor (tw, tb, 125.0e3);
            c.addResistor (drive, x, 100.0e3);
            c.addCapacitor (x, tb, 22.0e-9);
            c.addCapacitor (x, mt, 22.0e-9);
            ch.rBass = c.addResistor (tb, mt, 125.0e3);
            ch.rMid = c.addResistor (mt, gnd, 12.5e3);
            return tw;
        };

        // The single 1M audio Volume pot (top / bottom halves of the track).
        auto volumePot = [&] (NodalCircuit::Node top)
        {
            const auto w = c.addNode();
            ch.rVolTop = c.addResistor (top, w, 500.0e3);
            ch.rVolBot = c.addResistor (w, gnd, 500.0e3);
            return w;
        };

        // First stage: 68k stopper, 1M input leak, 100k / 1k5 + 22 uF (documented on both families).
        stage (in, 68.0e3, 1.0e6, 1.5e3, 22.0e-6, ch.pPlate1, ch.pK1, 180.0, 1.4);

        if (! s.concorde)
        {
            // Express: V1B -> .022 -> Volume -> V1A (2k7, partial bypass) -> .022 -> V2A (10k unbypassed: the cold stage)
            // -> stack.
            const auto v1Out = c.addNode();
            c.addCapacitor (ch.pPlate1, v1Out, 22.0e-9);
            const auto w = volumePot (v1Out);
            stage (w, 68.0e3, 0.0, 2.7e3, 0.68e-6, ch.pPlate2, ch.pK2, 200.0, 1.9);
            const auto c2 = c.addNode();
            c.addCapacitor (ch.pPlate2, c2, 22.0e-9);
            stage (c2, 1.0, 1.0e6, 10.0e3, 0.0, ch.pPlate3, ch.pK3, 250.0, 3.0);
            ch.pOut = toneStack (ch.pPlate3);
            c.addResistor (ch.pOut, gnd, 1.0e6); // the PI grid leak, seen from the stack
        }
        else
        {
            // Concorde: V1 -> anode-driven stack -> Volume -> recovery stage -> cold stage -> V2B cathode follower (100k to
            // ground, direct-coupled from the cold stage's plate).
            const auto tw = toneStack (ch.pPlate1);
            const auto w = volumePot (tw);
            stage (w, 68.0e3, 0.0, 2.7e3, 0.68e-6, ch.pPlate2, ch.pK2, 200.0, 1.9);
            const auto c2 = c.addNode();
            c.addCapacitor (ch.pPlate2, c2, 22.0e-9);
            stage (c2, 1.0, 1.0e6, 10.0e3, 0.0, ch.pPlate3, ch.pK3, 260.0, 3.0);
            ch.pFollower = c.addNode();
            c.addTriode (rail, ch.pPlate3, ch.pFollower, triode12AX7());
            c.addResistor (ch.pFollower, gnd, 100.0e3);
            c.setInitialGuess (ch.pFollower, 262.0);
            ch.pOut = ch.pFollower;
        }
    }

    // ================================================================ power: LTP PI + 2x EL34, fixed bias, NO global NFB
    {
        auto& c = ch.power;
        const auto rail = c.addNode(), piRail = c.addNode(), biasSrc = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, s.railPlates);
        ch.wSrcPi = c.addSource (piRail, screenNominal * s.piFraction);
        ch.wSrcBias = c.addSource (biasSrc, s.biasNominal * 1.15);
        ch.wSrcIn = c.addSource (in, 0.0);

        ch.wPiGrid = c.addNode();
        if (s.concorde)
        {
            // The cathode follower's .022 uF / 220k into the PI entrance (Amp Garage builder's description).
            const auto e = c.addNode();
            c.addCapacitor (in, e, 22.0e-9);
            c.addResistor (e, gnd, 220.0e3);
            c.addCapacitor (e, ch.wPiGrid, 0.1e-6);
        }
        else
        {
            c.addCapacitor (in, ch.wPiGrid, 0.1e-6);
        }

        // Long-tailed pair: 82k (driven side) / 100k plates, 470 bias + 22k tail, 1M grid leaks to the tail tap, the
        // other grid AC-grounded through 0.1 uF.
        ch.wPiK = c.addNode();
        ch.wPiPlateA = c.addNode();
        ch.wPiPlateB = c.addNode();
        const auto tap = c.addNode(), gB = c.addNode();
        c.addResistor (ch.wPiGrid, tap, 1.0e6);
        c.addResistor (gB, tap, 1.0e6);
        c.addCapacitor (gB, gnd, 0.1e-6);
        c.addResistor (ch.wPiK, tap, 470.0);
        c.addResistor (tap, gnd, 22.0e3);
        c.addTriode (ch.wPiPlateA, ch.wPiGrid, ch.wPiK, triode12AX7());
        c.addTriode (ch.wPiPlateB, gB, ch.wPiK, triode12AX7());
        c.addCapacitor (ch.wPiGrid, ch.wPiPlateA, cgp);
        c.addCapacitor (gB, ch.wPiPlateB, cgp);
        c.addResistor (piRail, ch.wPiPlateA, 82.0e3);
        c.addResistor (piRail, ch.wPiPlateB, 100.0e3);
        c.setInitialGuess (ch.wPiPlateA, 220.0);
        c.setInitialGuess (ch.wPiPlateB, 225.0);
        c.setInitialGuess (ch.wPiK, 40.0);
        c.setInitialGuess (tap, 39.0);
        c.setInitialGuess (ch.wPiGrid, 39.0);
        c.setInitialGuess (gB, 39.0);

        ch.rHiCut = -1;
        if (s.concorde)
        {
            // Hi-Cut: a variable R + 1 nF across the PI outputs, "in the power amp" (Komet's manual); values estimated.
            const auto hc = c.addNode();
            ch.rHiCut = c.addResistor (ch.wPiPlateA, hc, 1.0e6);
            c.addCapacitor (hc, ch.wPiPlateB, 1.0e-9);
        }

        // .022 uF couplings, 220k grid leaks into the bias node (15k from the bias supply, 10 uF), 1k5 grid stoppers.
        ch.wBias = c.addNode();
        c.addResistor (biasSrc, ch.wBias, 15.0e3);
        c.addCapacitor (ch.wBias, gnd, 10.0e-6);
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 22.0e-9);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 22.0e-9);
        c.addResistor (ch.wGridA, ch.wBias, 220.0e3);
        c.addResistor (ch.wGridB, ch.wBias, 220.0e3);
        const auto gAs = c.addNode(), gBs = c.addNode();
        c.addResistor (ch.wGridA, gAs, 1.5e3);
        c.addResistor (ch.wGridB, gBs, 1.5e3);
        c.setInitialGuess (ch.wBias, s.biasNominal);
        c.setInitialGuess (ch.wGridA, s.biasNominal);
        c.setInitialGuess (ch.wGridB, s.biasNominal);
        c.setInitialGuess (gAs, s.biasNominal);
        c.setInitialGuess (gBs, s.biasNominal);

        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        ch.penA = c.addPentode (ch.wPP1, gAs, gnd, pentodeEL34(), screenNominal);
        ch.penB = c.addPentode (ch.wPP2, gBs, gnd, pentodeEL34(), screenNominal);
        c.setInitialGuess (ch.wPP1, s.railPlates - 5.0);
        c.setInitialGuess (ch.wPP2, s.railPlates - 5.0);

        // Winding capacitance and core / copper losses (same treatment as the AC15 / Super Lead models).
        c.addCapacitor (ch.wPP1, ch.wPP2, 400.0e-12);
        c.addResistor (ch.wPP1, ch.wPP2, 4.0 * s.otPrimary);
        c.addCapacitor (ch.wPP1, gnd, 400.0e-12);
        c.addCapacitor (ch.wPP2, gnd, 400.0e-12);
        {
            const auto sn = c.addNode();
            c.addResistor (ch.wPP1, sn, 2.0e3);
            c.addCapacitor (sn, ch.wPP2, 2.0e-9);
        }

        // Presence: a variable R + 10 nF across the primary (no NFB loop to put it in; see the doc -- estimated).
        {
            const auto pn = c.addNode();
            ch.rPresence = c.addResistor (ch.wPP1, pn, 20.0e3);
            c.addCapacitor (pn, ch.wPP2, 10.0e-9);
        }

        // Output transformer: centre tap on the plate rail, 16 ohm tap. No feedback is taken from the secondary, so its
        // winding sense only sets the output's absolute polarity, not stability.
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance);
        const double turns = std::sqrt (s.otPrimary / speakerNominal[matchedSpeaker]) / 2.0;
        const double lh = s.primaryHalfL;
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
        c.setInitialGuess (a1, s.railPlates);
        c.setInitialGuess (a2, s.railPlates);
    }
}

void TrainwreckExpressStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const auto& s = spec();
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * pots::audio (k.treble));
    const double bass = juce::jmax (1.0, 250.0e3 * k.bass);
    const double mid = juce::jmax (1.0, 25.0e3 * k.middle);
    const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    // Presence knob up = more series R = less HF shunted off the primary.
    const double presenceR = 300.0 + 60.0e3 * k.presence * k.presence;
    // Hi-Cut knob up = less series R = more HF shunted between the PI outputs.
    const double hiCutR = 2.0e3 + 1.0e6 * pots::audio (1.0 - k.hiCut);
    const double biasVolts = s.biasNominal - s.biasSpan * (1.0 - 2.0 * k.bias); // knob up = hotter (less negative)
    const double rectifier = s.rectifierR * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 250.0e3 - trebleBottom));
        ch.pre.setResistance (ch.rTrebleBot, trebleBottom);
        ch.pre.setResistance (ch.rBass, bass);
        ch.pre.setResistance (ch.rMid, mid);
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        ch.power.setResistance (ch.rPresence, presenceR);
        if (ch.rHiCut >= 0)
            ch.power.setResistance (ch.rHiCut, hiCutR);
        // the bias supply sits behind 15k into a 220k || 220k grid-leak pair that draws no DC: source volts = grid volts
        ch.power.setSource (ch.wSrcBias, biasVolts);
        ch.supply.setResistance (ch.rRect, rectifier);
    }
    if (appliedSpeaker != k.speaker && ! resistiveLoadForced)
    {
        for (auto& ch : channels)
            applySpeaker (ch, k.speaker);
        appliedSpeaker = k.speaker;
    }

    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void TrainwreckExpressStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    if (resistiveLoadForced)
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

void TrainwreckExpressStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
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

void TrainwreckExpressStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void TrainwreckExpressStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const auto& s = spec();
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (ch.sumCount > 0)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.5, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.sumScreen / n + preampAndPiCurrent));
    }
    ch.supply.solveSample();
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const double plates = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sA));
    const double screens = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sB));
    ch.power.setSource (ch.wSrcRail, plates);
    ch.power.setPentodeScreen (ch.penA, screens);
    ch.power.setPentodeScreen (ch.penB, screens);
    // the 9k1 / 20 uF-class decoupling chain to the PI and preamp taps
    constexpr double decouplingTau = 9.1e3 * 20.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double a = 1.0 - std::exp (-dt / decouplingTau);
    ch.piRail += a * (screens * s.piFraction - ch.piRail);
    const double b = 1.0 - std::exp (-dt / (2.0 * decouplingTau)); // the preamp sits one more RC section down the chain
    ch.preRail += b * (screens * s.preFraction - ch.preRail);
    ch.power.setSource (ch.wSrcPi, ch.piRail);
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
}

double TrainwreckExpressStyleAmplifierProcessor::preampOutput (const Channel& ch) const noexcept
{
    return ch.pre.voltage (ch.pOut) - ch.outDc;
}

double TrainwreckExpressStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::stage1Plate: return ch.pre.voltage (ch.pPlate1);
        case Probe::stage1Cathode: return ch.pre.voltage (ch.pK1);
        case Probe::stage2Plate: return ch.pre.voltage (ch.pPlate2);
        case Probe::stage2Cathode: return ch.pre.voltage (ch.pK2);
        case Probe::stage3Plate: return ch.pre.voltage (ch.pPlate3);
        case Probe::stage3Cathode: return ch.pre.voltage (ch.pK3);
        case Probe::follower: return ch.pFollower != 0 ? ch.pre.voltage (ch.pFollower) : 0.0;
        case Probe::toneOut: return ch.pre.voltage (ch.pOut);
        case Probe::piGrid: return ch.power.voltage (ch.wPiGrid);
        case Probe::piPlateA: return ch.power.voltage (ch.wPiPlateA);
        case Probe::piPlateB: return ch.power.voltage (ch.wPiPlateB);
        case Probe::piCathode: return ch.power.voltage (ch.wPiK);
        case Probe::powerGridA: return ch.power.voltage (ch.wGridA);
        case Probe::powerGridB: return ch.power.voltage (ch.wGridB);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::speaker: return ch.power.voltage (ch.wOut);
        case Probe::biasNode: return ch.power.voltage (ch.wBias);
    }
    return 0.0;
}

double TrainwreckExpressStyleAmplifierProcessor::plateCurrentA() const noexcept
{
    return channels[0].power.pentodePlateCurrent (channels[0].penA);
}

double TrainwreckExpressStyleAmplifierProcessor::plateCurrentB() const noexcept
{
    return channels[0].power.pentodePlateCurrent (channels[0].penB);
}

void TrainwreckExpressStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;
    const auto& s = spec();

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
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMiddle, middleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedHiCut, hiCutParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    appliedSpeaker = -1;
    updatePots ({ volumeParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(), presenceParam->get(),
                  hiCutParam != nullptr ? hiCutParam->get() : 0.0f, powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        double ipRun = 2.0 * s.idlePlateCurrent, isRun = 0.01;
        for (int pass = 0; pass < 10; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -(isRun + preampAndPiCurrent));
            ch.supply.setSource (ch.srcVoc, s.railPlates + s.rectifierR * (ipRun + isRun + preampAndPiCurrent));
            passOk = ch.supply.prepare (supplyRate);
            const double plates = ch.supply.voltage (ch.sA), screens = ch.supply.voltage (ch.sB);
            ch.piRail = screens * s.piFraction;
            ch.preRail = screens * s.preFraction;

            ch.pre.setSource (ch.pSrcRail, ch.preRail);
            passOk = ch.pre.prepare (newSampleRate) && passOk;

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
        dcOk = passOk && dcOk;
        ch.outDc = ch.pre.voltage (ch.pOut);
        ch.pre.saveDynamicState (ch.preRest);
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

void TrainwreckExpressStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    if (hiCutParam != nullptr)
        smoothedHiCut.setTargetValue (hiCutParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    // Touch (Concorde): Gradual eases the first grid's drive (estimated as ~-9 dB, see the doc).
    const double inputGain = (touchParam != nullptr && juce::roundToInt (touchParam->get()) == 1) ? 0.35 : 1.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMiddle.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float hc = smoothedHiCut.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, tr, mi, ba, pr, hc, pw, bi, fe, speakerChoice });
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit (inputGain * (double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();

            ch.power.setSource (ch.wSrcIn, masterGain * preampOutput (ch));
            const bool okPower = ch.power.solveSample();
            double ipA, ipB, isA, isB;
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
            ch.sumPlate += ipA + ipB;
            ch.sumScreen += isA + isB;
            ++ch.sumCount;
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

            const double speakerVolts = ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 120.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;

            if (ok)
            {
                ch.failStreak = 0;
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.pre.saveDynamicState (ch.preRest);
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
                out = speakerVolts * outputScale * outGain * speakerGain;
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

void TrainwreckExpressStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
