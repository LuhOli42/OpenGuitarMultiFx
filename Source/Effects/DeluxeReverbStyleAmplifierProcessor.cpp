#include "DeluxeReverbStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/DeluxeReverbAB763.md). Rails ARE printed on the factory schematic (same family as the
    // Twin Reverb, same convenience), so idle currents are picked to land there rather than assumed. ----
    constexpr double railPlatesNominal = 415.0;   // printed at both 6V6GT plates
    constexpr double railScreensNominal = 415.0;  // printed (no separate central screen tap in this schematic; each tube
                                                   // has its own 470 ohm/1 W screen resistor, modelled as screenResistor)
    constexpr double railPiNominal = 325.0;       // printed ("+325V" at the phase inverter's tail-bias network)
    constexpr double railPreampNominal = 230.0;   // not directly printed (only the post-100k plate voltage is: +180/+170 V);
                                                   // tuned via preampNodeCurrent so the model's own plate lands there
    constexpr double rectifierResistance = 320.0; // GZ34 + PT: not printed, sized so full drive sags a believable amount
    constexpr double chokeResistance = 100.0;
    constexpr double chokeInductance = 10.0;
    constexpr double screenResistor = 470.0;      // printed ("470 1W") per tube
    constexpr double idlePlateCurrent = 0.017;    // per output tube (a single 6V6GT: about half the Twin's per-tube 6L6GC)
    constexpr double idleScreenCurrent = 0.0025;
    constexpr double phaseInverterNodeCurrent = 0.0138; // tuned so the PI tail rail lands near the printed +325 V
    constexpr double preampNodeCurrent = 0.0047;        // tuned so the preamp plates land near the printed +180/+170 V

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set (7025 is a low-noise-selected 12AX7, same curve)

    /** The phase inverter's 12AT7 (not a 12AX7 like the Twin Reverb/Bassman/Super Lead's). mu/kp/kvb/ex are Koren's
        published 12AT7 set (Duncan/Munro SPICE values); kg1 is NOT that published value -- this amp's real global feedback
        loop (speaker -> 820 ohm -> PI tail, printed, same value as the rest of the AB763 family) turned out to have far
        too little gain margin once every stage was combined at published/textbook gm, and self-oscillated at ~1 Hz (a real
        limit cycle in the DC solution, confirmed by opening the feedback loop in a test and watching it vanish, and by
        confirming the oscillation persists even with a fully static, non-sagging supply -- it is not the sag loop). Empirically
        raising kg1 (softening the PI's transconductance) restores real-world stability; this is a known, documented departure
        from a from-datasheet fit, not a printed or verified value. Revisit if the OT ratio/other gain-stage numbers below are
        ever independently re-derived, since one of them is the more likely true root cause. */
    KorenTriode::Parameters triode12AT7()
    {
        KorenTriode::Parameters p;
        p.mu = 60.0;
        p.kg1 = 3000.0; // NOT the published 272 -- see the stability note above
        p.kp = 225.0;
        p.kvb = 54.7;
        p.ex = 1.5;
        return p;
    }

    /** A single 6V6GT beam tetrode (this amp's power tubes are NOT paralleled pairs like the Twin Reverb's 6L6GCs/the Super
        Lead's EL34s -- one tube per side, same "one real device, no pairing recipe" pattern as the Bassman's 5881s). mu/ex/kg2/
        kp/kvb are Koren's published 6V6GT set; kg1 shares the same stability caveat as triode12AT7() above -- the published
        value (1400) is part of the same unstable loop, so this softened value is what's actually shipped. Fully stable with
        this pairing (verified: settles to a static DC point from silence, zero oscillation over a 10 s soak). */
    KorenPentode::Parameters pentode6V6()
    {
        KorenPentode::Parameters p;
        p.mu = 10.0;
        p.ex = 1.35;
        p.kg1 = 9000.0; // NOT the published 1400 -- see triode12AT7()'s stability note
        p.kg2 = 4500.0;
        p.kp = 48.5;
        p.kvb = 12.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer (not printed): 8 ohm secondary (the real amp's single 12" Jensen), sized for a reasonable
    // plate-to-plate load for 2 x 6V6GT (~4 k, close to Fender's own published ~22 W AB763 OT).
    constexpr double primaryHalfInductance = 5.0;
    constexpr double halfToSecondaryTurns = 11.18;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.997;
    constexpr double primaryHalfResistance = 70.0;
    constexpr double secondaryResistance = 0.15;
    constexpr double feedbackResistor = 820.0; // R (from the speaker terminal into the phase inverter's tail node, printed "820")

    constexpr double biasSupplyVolts = -35.0; // printed ("-35V" at the power tube grid-leak rail)

    constexpr double speakerEddyLoss = 60.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // the real amp's single 8 ohm Jensen

    constexpr double powerTheta = 0.9;

    // ---- reduced-order power stage: calibration data, DR_POWERCAL in the test file ----
    // Plate rail vs. the mixed-preamp-signal drive PEAK, measured on this amp's own (stabilised) full reference model
    // (Input Vibrato, Volume 0.8, Power Drive max), same methodology as the Super Lead/Bassman/Twin Reverb. The output
    // plateaus cleanly around 11.6-11.9 V once the rail has sagged into the 399-400 V range; the last 3 swept levels are
    // dropped below (guitar input level pushes past inputLimit()'s own ceiling, so drive_pk itself goes slightly
    // non-monotonic there -- redundant with the point just before it, and a strictly-increasing table is required anyway).
    constexpr int bmSagPoints = 17;
    constexpr double bmSagDrive[bmSagPoints] = { 0.000425, 0.002122, 0.006367, 0.014859, 0.025478, 0.042476, 0.063733,
                                                  0.106259, 0.170016, 0.255145, 0.383284, 0.597788, 0.855871, 1.157292,
                                                  1.387650, 1.472226, 1.487870 };
    constexpr double bmSagRail[bmSagPoints] = { 415.08, 415.08, 415.07, 415.02, 414.90, 414.57, 413.94,
                                                 412.04, 408.15, 404.50, 401.99, 400.61, 400.01, 399.65,
                                                 399.39, 399.21, 399.12 };

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

DeluxeReverbStyleAmplifierProcessor::DeluxeReverbStyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "dr_input", "Input", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("Normal"); case 2: return juce::String ("Both"); default: return juce::String ("Vibrato"); }
        }));
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("dr_volume", "Volume", 0.4f);
    auto treble = make ("dr_treble", "Treble", 0.5f);
    auto bass = make ("dr_bass", "Bass", 0.5f);
    auto speed = make ("dr_speed", "Speed", 0.5f);
    auto intensity = make ("dr_intensity", "Intensity", 0.0f);
    auto output = make ("dr_output", "Output", 0.5f);
    auto power = make ("dr_power", "Power Drive", 1.0f);
    auto bias = make ("dr_bias", "Bias", 0.5f);
    auto feel = make ("dr_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "dr_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm"; }));

    inputParam = input.get();
    volumeParam = volume.get();
    trebleParam = treble.get();
    bassParam = bass.get();
    speedParam = speed.get();
    intensityParam = intensity.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "deluxereverb", "Deluxe Reverb-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (bass));
    group->addChild (std::move (speed));
    group->addChild (std::move (intensity));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("deluxereverb_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void DeluxeReverbStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        const auto nl = c.addNode();
        ch.sB = c.addNode();
        ch.sC = c.addNode();
        ch.sD = c.addNode();

        const double dcDrop = rectifierResistance * (idlePlateCurrent * 2.0 + idleScreenCurrent * 2.0 + phaseInverterNodeCurrent + preampNodeCurrent);
        ch.srcVoc = c.addSource (vo, railPlatesNominal + dcDrop);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 40.0e-6);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 20.0e-6);
        c.addResistor (ch.sB, ch.sC, 4.7e3);
        c.addCapacitor (ch.sC, gnd, 20.0e-6);
        c.addResistor (ch.sC, ch.sD, 4.7e3);
        c.addCapacitor (ch.sD, gnd, 8.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -(idlePlateCurrent * 2.0));
        ch.iB = c.addCurrentSource (ch.sB, -(idleScreenCurrent * 2.0));
        ch.iC = c.addCurrentSource (ch.sC, -phaseInverterNodeCurrent);
        ch.iD = c.addCurrentSource (ch.sD, -preampNodeCurrent);
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railScreensNominal);
        c.setInitialGuess (ch.sC, railPiNominal);
        c.setInitialGuess (ch.sD, railPreampNominal);
    }

    // ================================================================ preamp: BOTH channels' gain triode + tone stack (a
    // real linear circuit either way, so it costs nothing extra in reducedOrder mode), mixing into pMix
    {
        auto& c = ch.pre;
        const auto vcc = c.addNode(), inNormal = c.addNode(), inVibrato = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railPreampNominal); // set for real from the supply after prepare()'s DC settle
        ch.pSrcInNormal = c.addSource (inNormal, 0.0);
        ch.pSrcInVibrato = c.addSource (inVibrato, 0.0);

        ch.pMix = c.addNode();
        NodalCircuit::Node plateNodes[2];
        for (int ch2 = 0; ch2 < 2; ++ch2) // 0 = Normal, 1 = Vibrato
        {
            const auto g = c.addNode(), p = c.addNode(), k = c.addNode();
            c.addResistor (ch2 == 0 ? inNormal : inVibrato, g, 68.0e3);
            c.addTriode (p, g, k, triode12AX7());
            c.addCapacitor (g, p, cgp);
            c.addResistor (vcc, p, 100.0e3);
            c.addResistor (k, gnd, 1.5e3);
            c.addCapacitor (k, gnd, 25.0e-6);
            plateNodes[ch2] = p;
            c.setInitialGuess (p, 250.0);

            // Tone stack: this amp's own 2-band variant of the same blackface family stack (no Middle -- read directly off
            // the schematic: Treble 250k-A, Bass 250k-A as a rheostat, a FIXED 6.8k where the Twin Reverb/Bassman would
            // have a Middle pot). Cap values also read directly: 250 pF treble bypass, 100k + 0.1 uF into the bass
            // network, 0.047 uF bridging cap.
            const auto ti = c.addNode(), top = c.addNode(), slope = c.addNode(), n1 = c.addNode(), n2 = c.addNode(), tone = c.addNode();
            c.addResistor (p, ti, 2.0e3); // coupling from the plate (no follower stage in this preamp, unlike the Bassman/Super Lead)
            c.addCapacitor (ti, top, 250.0e-12);
            c.addResistor (ti, slope, 100.0e3);
            ch.rTrebleTop[ch2] = c.addResistor (top, tone, 125.0e3);
            ch.rTrebleBottom[ch2] = c.addResistor (tone, n1, 125.0e3);
            c.addCapacitor (slope, n1, 0.1e-6);
            ch.rBass[ch2] = c.addResistor (n1, n2, 250.0e3);
            c.addCapacitor (slope, n2, 0.047e-6);
            c.addResistor (n2, gnd, 6800.0); // fixed -- no Middle pot on this amp

            // Volume: 1M-A. Both channels' wipers mix into pMix through 100k, same "jumper" pattern as the other amps.
            const auto w = c.addNode();
            ch.rVolTop[ch2] = c.addResistor (tone, w, 1.0e6);
            ch.rVolBot[ch2] = c.addResistor (w, gnd, 1.0e6);
            c.addResistor (w, ch.pMix, 100.0e3);
        }
        ch.pPlateNormal = plateNodes[0];
        ch.pPlateVibrato = plateNodes[1];
    }

    // ================================================================ phase inverter, power amp -- the FULL reference
    // netlist only (reducedOrder replaces all of this with behavioralPowerStage())
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode(), vpi = c.addNode(), ct = c.addNode(), biasRail = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);
        ch.wSrcPi = c.addSource (vpi, railPiNominal);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (biasRail, biasSupplyVolts);
        ch.wRecoveryIn = cf; // reducedOrder's drive point is pMix directly, so no separate recovery-gain node is needed there

        // Gain-recovery stage (1/2 12AT7): both channels' mixed signal needs a bit of gain back after two volume pots and a
        // 100k summing resistor each. Cathode-biased, plate feeds the phase inverter's driven grid.
        const auto rg = c.addNode(), rp = c.addNode(), rk = c.addNode();
        c.addResistor (cf, rg, 1.0e6);
        c.addTriode (rp, rg, rk, triode12AX7());
        c.addCapacitor (rg, rp, cgp);
        c.addResistor (vpi, rp, 100.0e3);
        c.addResistor (rk, gnd, 1.5e3);
        c.addCapacitor (rk, gnd, 25.0e-6);
        c.setInitialGuess (rp, 200.0);

        // Phase inverter: 12AT7 long-tailed pair, plate loads 82k / 100k (printed), cathode tail into the feedback node.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        c.addCapacitor (rp, g1, 0.02e-6);
        c.addResistor (g1, nm, 1.0e6);
        c.addResistor (g2, nm, 1.0e6);
        c.addTriode (pa, g1, k, triode12AT7());
        c.addTriode (pb, g2, k, triode12AT7());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addCapacitor (pa, pb, 47.0e-12);
        c.addResistor (k, nm, 470.0);
        c.addResistor (nm, fp, 10.0e3);
        c.addCapacitor (fp, g2, 0.1e-6);
        c.setInitialGuess (pa, 240.0);
        c.setInitialGuess (pb, 235.0);
        c.setInitialGuess (k, 32.0);
        c.setInitialGuess (nm, 30.0);
        c.setInitialGuess (g1, 30.0);
        c.setInitialGuess (g2, 30.0);

        // Power amplifier: 2 x 6V6GT, ONE tube per side (not paralleled pairs like the Twin/Super Lead). 1500 ohm grid
        // stoppers (printed), 220k grid leaks straight to the bias rail on each side -- read directly off the schematic:
        // unlike the shared-node-plus-own-decoupling-cap arrangement the paired-tube amps use, this amp's two 220k
        // resistors are simply the two halves of a divider centred ON the ideal bias source itself (no separate floating
        // node between them), so each grid ties to biasRail through its own 220k with nothing else in between.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode();
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.1e-6);
        c.addCapacitor (pb, g4, 0.1e-6);
        c.addResistor (g3, biasRail, 220.0e3);
        c.addResistor (g4, biasRail, 220.0e3);
        c.addResistor (g3, g3s, 1.5e3);
        c.addResistor (g4, g4s, 1.5e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6V6(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6V6(), railScreensNominal);

        c.addCapacitor (pp1, pp2, 300.0e-12);
        c.addResistor (pp1, pp2, 40.0e3);
        c.addCapacitor (pp1, gnd, 300.0e-12);
        c.addCapacitor (pp2, gnd, 300.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
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

        // Negative feedback: no Presence control on this amp (not on the real front panel) -- a fixed resistor from the
        // speaker terminal straight into the phase inverter's tail node.
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        c.addCapacitor (fp, gnd, 1.5e-9);

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s })
            c.setInitialGuess (n, biasSupplyVolts);
    }
}

void DeluxeReverbStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void DeluxeReverbStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 125.0e3 * 2.0 * k.treble); // 250k-A total, split like the Bassman's own
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 250.0e3 * pots::audio (k.bass));
    const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    const double biasVolts = -25.0 - 20.0 * k.bias; // a plausible +-10 V trim range around the printed -35 V (this amp has a real bias trimmer)
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        for (int i = 0; i < 2; ++i)
        {
            ch.pre.setResistance (ch.rTrebleTop[i], trebleTop);
            ch.pre.setResistance (ch.rTrebleBottom[i], trebleBottom);
            ch.pre.setResistance (ch.rBass[i], bassR);
            ch.pre.setResistance (ch.rVolBot[i], volBottom);
            ch.pre.setResistance (ch.rVolTop[i], juce::jmax (1.0, 1.0e6 - volBottom));
        }
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setSource (ch.wSrcBias, biasVolts);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void DeluxeReverbStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
}

void DeluxeReverbStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.6, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.3, ch.sumScreen / n));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 560.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 520.0));
    }
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sD, 480.0));
    ch.vScreen = rail (ch.sB, 560.0);
}

double DeluxeReverbStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (driveVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, driveVoltage);

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double DeluxeReverbStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::channelNormalPlate: return ch.pre.voltage (ch.pPlateNormal);
        case Probe::channelVibratoPlate: return ch.pre.voltage (ch.pPlateVibrato);
        case Probe::mixNode: return ch.pre.voltage (ch.pMix);
        case Probe::recoveryPlate: return ch.power.voltage (ch.wRecoveryIn);
        case Probe::phaseInverterGrid: return ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return ch.power.voltage (ch.wTail);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::powerGridA: return ch.power.voltage (ch.wPowerGridA);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return ch.power.voltage (ch.wBias);
        case Probe::feedbackNode: return ch.power.voltage (ch.wFeedback);
    }
    return 0.0;
}

double DeluxeReverbStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void DeluxeReverbStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void DeluxeReverbStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double DeluxeReverbStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double DeluxeReverbStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void DeluxeReverbStyleAmplifierProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize, numChannels);
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    const auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p, double time)
    {
        s.reset (newSampleRate, time);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedSpeed, speedParam, 0.02);
    setup (smoothedIntensity, intensityParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = idlePlateCurrent * 2.0 + idleScreenCurrent * 2.0 + phaseInverterNodeCurrent + preampNodeCurrent;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ volumeParam->get(), trebleParam->get(), bassParam->get(),
                  powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sD));
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;

        // Both channel plates drive the power block's cf source directly through pMix (no cathode follower here);
        // settle the power block at pMix's own DC level. reducedOrder: `ch.power` is entirely empty (the tone stacks
        // live in `ch.pre`, and nothing else is built there in this mode) -- skip it outright, there is nothing to solve.
        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        if (! reducedOrder)
        {
            const double mixDc = ch.pre.voltage (ch.pMix);
            ch.power.setSource (ch.wSrcCf, mixDc);
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.vScreen = ch.supply.voltage (ch.sB);
            ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
            ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
            dcOk = ch.power.prepare (newSampleRate) && dcOk;
            ch.power.solveSample();
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
        }
        ch.supply.setCurrentSource (ch.iA, -(ipA + ipB));
        ch.supply.setCurrentSource (ch.iB, -(isA + isB));
        idleSupplyCurrent = ipA + ipB + isA + isB + phaseInverterNodeCurrent + preampNodeCurrent;
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
        dcOk = ch.supply.prepare (supplyRate) && dcOk;
        ch.screenDropA = screenResistor * isA;
        ch.screenDropB = screenResistor * isB;
        ch.vScreen = ch.supply.voltage (ch.sB);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
        }
        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sD));
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmToneState = 0.0;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
    lfoPhase = 0.0;
}

void DeluxeReverbStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedSpeed.setTargetValue (speedParam->get());
    smoothedIntensity.setTargetValue (intensityParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int inputChoice = juce::roundToInt (inputParam->get()); // 0 Normal, 1 Vibrato, 2 Both
    const bool inputConnectsNormal = inputChoice != 1;
    const bool inputConnectsVibrato = inputChoice != 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float sp = smoothedSpeed.getNextValue();
        const float in = smoothedIntensity.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        const double lfoHz = 1.0 + 11.0 * pots::audio ((double) sp);
        lfoPhase += lfoHz / sampleRate;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        const double tremDepth = (double) in;
        const double tremMod = 1.0 - tremDepth * 0.5 * (1.0 + std::sin (2.0 * juce::MathConstants<double>::pi * lfoPhase));

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, tr, ba, pw, bi, fe, speakerChoice });
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcInNormal, inputConnectsNormal ? x : 0.0);
            ch.pre.setSource (ch.pSrcInVibrato, inputConnectsVibrato ? x : 0.0);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            const double mixV = ch.pre.voltage (ch.pMix) * tremMod;
            bool ok2 = true; // reducedOrder: ch.power is empty, nothing to solve, always "converges"
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcCf, masterGain * mixV);
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
                ok2 = ch.power.solveSample();
            }
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

            if (! reducedOrder)
            {
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

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, masterGain * mixV) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));
            if (chIdx == 0 && ! sane && okPre && ok2)
                ++sanityRejects;

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

void DeluxeReverbStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No dedicated amp glyph exists on the reference sheet yet (same placeholder Bassman/Super Lead use).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
