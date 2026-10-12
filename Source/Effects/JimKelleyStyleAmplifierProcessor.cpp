#include "JimKelleyStyleAmplifierProcessor.h"
#include "FastMath.h"
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

    // ---- supply (docs/circuits/JimKelley.md): the published single-channel schematic marks ~477 V at the
    // 6V6GTA plates, ~375 V at the screens/PI node and ~245 V at the preamp rail. Kelley's power supply is
    // solid-state-rectified and deliberately stiff (he designed for power-tube distortion, not sag). ----
    constexpr double railPlatesNominal = 477.0;   // OT centre tap, marked on the schematic
    constexpr double rectifierResistance = 40.0;  // SS rectifier + PT -- much stiffer than a tube amp
    constexpr double screenResistor = 2.0e3;      // the real amp's single shared 2k/10W screen feed
    constexpr double idlePlateCurrent = 0.015;    // per output tube (4x 6V6GTA, fixed bias ~-50 V)
    constexpr double idleScreenCurrent = 0.0035;
    constexpr double piAndPreampCurrent = 0.005;  // V4a + V4b + three 12AX7 halves, drawn from the screen node
    constexpr double piFraction = 0.89;           // PI/mixer rail / screen node -> ~375 V marked
    constexpr double preFraction = 0.58;          // preamp rail / screen node -> ~245 V marked
    double idleSupplyCurrent = 0.0;

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set

    /** One 6V6GTA beam tetrode per push-pull side standing in for the real amp's paralleled pair -- the same
        Koren 6V6GT fit the Deluxe Reverb model uses; supply feedback counts the real four tubes. */
    KorenPentode::Parameters pentode6V6()
    {
        KorenPentode::Parameters p;
        p.mu = 10.0;
        p.ex = 1.35;
        p.kg1 = 9000.0; // NOT the published 1400 -- the Deluxe Reverb's stability fix
        p.kg2 = 4500.0;
        p.kp = 48.5;
        p.kvb = 12.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    // Output transformer: 4.3k plate-to-plate (published OT spec for the 60 W quad) on the 8 ohm tap.
    constexpr double primaryHalfInductance = 8.0;
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.999;
    constexpr double primaryHalfResistance = 80.0;
    constexpr double secondaryResistance = 0.1;
    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // the transformer's 8 ohm tap

    // Output protection, same reasoning as the other amps' outputLimit.
    constexpr double outputKnee = 0.20, outputSpan = 0.08;
    inline double outputLimit (double v) noexcept
    {
        const double a = std::abs (v);
        return a <= outputKnee ? v : std::copysign (outputKnee + outputSpan * std::tanh ((a - outputKnee) / outputSpan), v);
    }

    // ---- reduced-order sag table (drive peak volts at pOut -> plate rail volts). Seeded from the shared
    // amp table shape, rescaled to this amp's 477 V rail; a POWERCAL pass in its test file re-measures it. ----
    constexpr int bmSagPoints = 19;
    constexpr double bmSagDrive[bmSagPoints] = { 0.002, 0.005, 0.010, 0.020, 0.032, 0.052, 0.078, 0.128, 0.20,
                                                 0.32, 0.50, 0.80, 1.20, 1.80, 2.60, 3.60, 4.80, 6.20, 8.00 };
    constexpr double bmSagRail[bmSagPoints] = { 477.0, 477.0, 477.0, 476.5, 476.0, 475.0, 473.5, 471.5, 469.0,
                                                465.5, 461.5, 457.0, 452.0, 447.0, 442.0, 437.0, 432.5, 428.5, 425.0 };
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
// Alternates this amp's page-2 Cab selector offers -- option ORDER must match between makeCabParam()
// (names) and the tails' setCabChoices() (profiles). The historic pairing is the Electro-Voice EVM-12L
// in the open-back 1x12 combo -- the stock voice; the alternates cover the other likely loads.
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::ev112, &tubeamp::cabs::open112, &tubeamp::cabs::greenback412 };
}

// ---- behavioural spring tank: three allpass delays + a damped feedback comb (the "drip" of a long
// spring without a tank netlist; send level comes from the preamp tap, return level from the knob). ----
void JimKelleyStyleAmplifierProcessor::Channel::Spring::prepare (double sampleRate)
{
    const auto mk = [&] (double ms) { return std::vector<double> ((size_t) juce::jmax (1, (int) (ms * 0.001 * sampleRate)), 0.0); };
    buf1 = mk (29.7);
    buf2 = mk (37.1);
    buf3 = mk (41.1);
    clear();
}

void JimKelleyStyleAmplifierProcessor::Channel::Spring::clear()
{
    std::fill (buf1.begin(), buf1.end(), 0.0);
    std::fill (buf2.begin(), buf2.end(), 0.0);
    std::fill (buf3.begin(), buf3.end(), 0.0);
    pos = 0;
    damp = 0.0;
}

double JimKelleyStyleAmplifierProcessor::Channel::Spring::process (double x) noexcept
{
    if (buf1.empty())
        return 0.0;
    const int n1 = (int) buf1.size(), n2 = (int) buf2.size(), n3 = (int) buf3.size();
    // buf3 is the feedback comb; buf1/buf2 are series allpasses reading off it.
    const double comb = buf3[(size_t) (pos % n3)];
    const double d1 = buf1[(size_t) (pos % n1)];
    const double a1 = -0.5 * x + d1;       // allpass 1: y = -g*x + d
    buf1[(size_t) (pos % n1)] = x + 0.5 * d1;
    double d2 = buf2[(size_t) (pos % n2)];
    const double a2 = -0.5 * a1 + d2;
    buf2[(size_t) (pos % n2)] = a1 + 0.5 * d2;
    // damped feedback into the comb (~3 kHz tank damping, decay like a medium spring)
    damp += 0.25 * (a2 - damp);
    buf3[(size_t) (pos % n3)] = x + 0.42 * damp;
    ++pos;
    return 0.55 * a2 + 0.45 * comb;
}

JimKelleyStyleAmplifierProcessor::JimKelleyStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto pull = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return juce::roundToInt (v) == 0 ? juce::String ("Off") : juce::String ("On"); }));
    };
    auto gain = make ("jk_gain", "Gain", 0.5f);
    auto volume = make ("jk_volume", "Volume", 0.5f);
    auto treble = make ("jk_treble", "Treble", 0.5f);
    auto bass = make ("jk_bass", "Bass", 0.5f);
    auto reverb = make ("jk_reverb", "Reverb", 0.0f);
    auto presence = pull ("jk_presence", "Presence");
    auto bright = pull ("jk_bright", "Bright");
    auto midBoost = pull ("jk_midboost", "Mid Boost");
    auto powerSel = std::make_unique<juce::AudioParameterFloat> (
        "jk_power_sel", "Power Select", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("60W") : juce::String ("30W"); }));
    auto output = make ("jk_output", "Output", 0.5f);
    auto power = make ("jk_power", "Power Drive", 1.0f);
    auto bias = make ("jk_bias", "Bias", 0.5f);
    auto feel = make ("jk_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "jk_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm"; }));

    gainParam = gain.get();
    volumeParam = volume.get();
    trebleParam = treble.get();
    bassParam = bass.get();
    reverbParam = reverb.get();
    presenceParam = presence.get();
    brightParam = bright.get();
    midBoostParam = midBoost.get();
    powerSelParam = powerSel.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "jimkelley", "Jim Kelley", "|", std::move (gain));
    group->addChild (std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (bass));
    group->addChild (std::move (reverb));
    group->addChild (std::move (presence));
    group->addChild (std::move (bright));
    group->addChild (std::move (midBoost));
    group->addChild (std::move (powerSel));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("jimkelley_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("jimkelley_cab", "1x12 EV", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void JimKelleyStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply: rectifier -> reservoir (plates) -> shared 2k -> screens
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 40.0e-6);
        c.addResistor (ch.sA, ch.sB, screenResistor);
        c.addCapacitor (ch.sB, gnd, 40.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -4.0 * idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -(4.0 * idleScreenCurrent + piAndPreampCurrent));
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, railPlatesNominal - 55.0);
    }

    // ================================================================ preamp: V1a -> Gain -> V1b (1N914 clamps)
    //  -> Baxandall stack -> V3a -> Volume -> pOut (docs/circuits/JimKelley.md).
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, railPlatesNominal * preFraction * 0.8);

        // Shared stage recipe (same one the other amps use: one solve for all stages keeps the iterate on a
        // single attractor).
        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double rk, double ck,
                          double rp, NodalCircuit::Node& plate, NodalCircuit::Node& cathode,
                          double plateGuess, double cathodeGuess)
        {
            const auto g = c.addNode();
            plate = c.addNode();
            cathode = c.addNode();
            c.addResistor (gIn, g, stopper);
            if (leak > 0.0)
                c.addResistor (g, gnd, leak);
            c.addTriode (plate, g, cathode, triode12AX7());
            c.addCapacitor (g, plate, cgp);
            c.addResistor (rail, plate, rp);
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
            return g;
        };

        // V1a input stage: 68k stopper, 1M input leak, 150k plate / 1k5 unbypassed cathode (the schematic's
        // values -- the stage is deliberately low-gain/high-headroom, Kelley's "keep the amp clean" voicing).
        stage (in, 68.0e3, 1.0e6, 1.5e3, 0.0, 150.0e3, ch.pPlate1, ch.pK1, 125.0, 1.4);

        // 10 nF coupling -> the front-panel GAIN pot (1M audio). The drawing omits this cap; the published
        // errata (Amp Garage) confirm it -- a DC-coupled gain track would sit the wiper at plate voltage.
        const auto gTop = c.addNode(), gWip = c.addNode();
        c.addCapacitor (ch.pPlate1, gTop, 10.0e-9);
        ch.rGainTop = c.addResistor (gTop, gWip, 0.5e6);
        ch.rGainBot = c.addResistor (gWip, gnd, 0.5e6);
        // Mid Boost (Bass-pot pull): 470 pF across the Gain pot's top leg -- per the owner's manual.
        ch.capMidBoost = c.addCapacitor (gTop, gWip, 1.0e-15);

        // V1b: the clip stage. Its grid carries the anti-parallel 1N914 pairs (~1.2 V soft clip -- Kelley's
        // anti-blocking-distortion fix; Amp Garage describes the pair). 100k plate, 1k5 + 10uF cathode.
        const auto g2 = stage (gWip, 68.0e3, 0.0, 1.5e3, 10.0e-6, 100.0e3, ch.pPlate2, ch.pK2, 160.0, 1.6);
        // Two series 1N914s each way between grid and ground (~1.2 V knee, per the schematic discussion).
        const auto d1 = c.addNode(), d2 = c.addNode();
        c.addDiode (g2, d1, 2.5e-9, 45.0e-3);
        c.addDiode (d1, gnd, 2.5e-9, 45.0e-3);
        c.addDiode (gnd, d2, 2.5e-9, 45.0e-3);
        c.addDiode (d2, g2, 2.5e-9, 45.0e-3);

        // Baxandall-style shelving stack (values off the published schematic): 22nF coupling -> 100k series
        // feed -> treble leg (.01uF -> TREBLE 500k-LIN -> gnd) and bass leg (.001uF -> BASS 500k-LIN ->
        // .0047uF shelf cap -> gnd); the joined wipers run through the 120k link into V3a's 2.2M grid leak.
        const auto tIn = c.addNode(), nTop = c.addNode(), trTop = c.addNode(), tSum = c.addNode(),
                   bTop = c.addNode(), bMid = c.addNode();
        c.addCapacitor (ch.pPlate2, tIn, 22.0e-9); // plate DC block (the stack stays ground-referenced)
        c.addResistor (tIn, nTop, 100.0e3);        // 100k series feed into the stack
        c.addCapacitor (nTop, trTop, 10.0e-9);     // .01uF treble leg
        c.addCapacitor (nTop, bTop, 1.0e-9);       // .001uF bass leg feed
        ch.rTrebleTop = c.addResistor (trTop, tSum, 0.5e6 * 0.5);
        ch.rTrebleBot = c.addResistor (tSum, gnd, 0.5e6 * 0.5);
        ch.rBassTop = c.addResistor (bTop, tSum, 0.5e6 * 0.5);
        ch.rBassBot = c.addResistor (tSum, bMid, 0.5e6 * 0.5);
        c.addCapacitor (bMid, gnd, 4.7e-9);       // .0047uF bass shelf leg
        // Bright (Treble-pot pull): ~560pF across the treble pot's top leg -- the glassy-edge lift
        ch.capBright = c.addCapacitor (trTop, tSum, 1.0e-15);
        // 120k link -> 2.2M grid leak -> V3a. The drawing's .01uF at the wiper join is the series
        // coupling to that link -- drawn as a shunt it reads like a ground leg, but grounded it
        // strangles everything above ~800 Hz (the real stack passes the treble band like Fender's).
        const auto couple = c.addNode(), mix = c.addNode();
        c.addCapacitor (tSum, couple, 10.0e-9);
        c.addResistor (couple, mix, 120.0e3);
        c.addResistor (mix, gnd, 2.2e6);
        ch.pTone = tSum; // probe point

        // V3a recovery stage: 68k plate / 4k7 cathode (marked ~215V plate / ~2V cathode on 245V rail).
        stage (mix, 68.0e3, 0.0, 4.7e3, 0.0, 68.0e3, ch.pPlate3, ch.pK3, 216.0, 2.0);

        // .047 coupling -> Volume (the front-panel level into the mixer stage) -> pOut.
        const auto vTop = c.addNode();
        c.addCapacitor (ch.pPlate3, vTop, 47.0e-9);
        ch.pOut = c.addNode();
        ch.rVolTop = c.addResistor (vTop, ch.pOut, 0.5e6);
        ch.rVolBot = c.addResistor (ch.pOut, gnd, 0.5e6);
    }

    // ================================================================ power: V4a mixer (NFB + presence +
    //  reverb return) -> V4b cathodyne -> 4x6V6GTA fixed bias -> 4.3k OT -> speaker -> 100k NFB
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (0.9); // the shared amp pattern: power netlists run near backward-Euler
        const auto rail = c.addNode(), piRail = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        ch.wSrcPi = c.addSource (piRail, railPlatesNominal * piFraction * 0.9);
        ch.wSrcIn = c.addSource (in, 0.0);

        // V4a mixer/driver: .047 coupling in, 220k grid leak, reverb return through the real 470k mixer
        // resistor, 390k plate load to the PI rail, cathode 4k7 with the global-NFB tap + presence branch.
        const auto v4g = c.addNode();
        c.addCapacitor (in, v4g, 0.047e-6);
        c.addResistor (v4g, gnd, 220.0e3);
        ch.wRev = c.addNode();
        ch.wSrcRev = c.addSource (ch.wRev, 0.0);
        c.addResistor (ch.wRev, v4g, 470.0e3);
        ch.wMixPlate = c.addNode();
        ch.wMixK = c.addNode();
        c.addTriode (ch.wMixPlate, v4g, ch.wMixK, triode12AX7());
        c.addCapacitor (v4g, ch.wMixPlate, cgp);
        c.addResistor (piRail, ch.wMixPlate, 390.0e3);
        c.addResistor (ch.wMixK, gnd, 4.7e3);
        // Presence (Gain-pot pull): .1uF + 1k5 shunt in the NFB tail at V4a's cathode -- Kelley's presence
        // lives in the feedback loop, not the preamp. The resistor leg is switchable.
        const auto presN = c.addNode();
        c.addCapacitor (ch.wMixK, presN, 0.1e-6);
        ch.rPres = c.addResistor (presN, gnd, 1.0e9); // Presence OFF at build; updatePots applies the knob
        c.addResistor (presN, ch.wMixK, 100.0e3); // DC anchor: a node on only a 1e9 drain never settles in relaxToDc
        c.setInitialGuess (v4g, 0.0);
        c.setInitialGuess (ch.wMixPlate, 209.0);
        c.setInitialGuess (ch.wMixK, 2.0);

        // V4b cathodyne (split-load) PI: .047 coupling from V4a's plate, the grid leak is the 3M6-to-rail /
        // 1M5-to-ground divider that sits the grid at ~110 V -- matching the schematic's 265 V plate mark.
        ch.wPiGrid = c.addNode();
        ch.wPiPlate = c.addNode();
        ch.wPiK = c.addNode();
        c.addCapacitor (ch.wMixPlate, ch.wPiGrid, 0.047e-6);
        c.addResistor (ch.wPiGrid, piRail, 3.6e6);
        c.addResistor (ch.wPiGrid, gnd, 1.5e6);
        c.addTriode (ch.wPiPlate, ch.wPiGrid, ch.wPiK, triode12AX7());
        c.addResistor (piRail, ch.wPiPlate, 68.0e3);
        c.addResistor (ch.wPiK, gnd, 68.0e3);
        c.setInitialGuess (ch.wPiGrid, 108.0);
        c.setInitialGuess (ch.wPiPlate, 265.0);
        c.setInitialGuess (ch.wPiK, 110.0);

        // .047 couplings -> 270k grid leaks into the -50 V fixed-bias node, 1k5 stoppers, 6V6GTA pairs.
        // The bias supply is modelled the way the other fixed-bias amps do it: an ideal source behind
        // a dropper feeding an R-C node (the real supply's filter), with the Bias knob as the trim
        // resistor loading it -- a bigger trim leaves the rail more negative.
        const auto biasSrc = c.addNode();
        ch.wSrcBias = c.addSource (biasSrc, -80.0);
        c.addResistor (biasSrc, ch.wBias = c.addNode(), 15.0e3);
        c.addCapacitor (ch.wBias, gnd, 10.0e-6);
        ch.rBiasTrim = c.addResistor (ch.wBias, gnd, 25.0e3);
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        const auto g3s = c.addNode(), g4s = c.addNode();
        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        c.addCapacitor (ch.wPiPlate, ch.wGridA, 0.047e-6);
        c.addCapacitor (ch.wPiK, ch.wGridB, 0.047e-6);
        c.addResistor (ch.wGridA, ch.wBias, 270.0e3);
        c.addResistor (ch.wGridB, ch.wBias, 270.0e3);
        c.addResistor (ch.wGridA, g3s, 1.5e3);
        c.addResistor (ch.wGridB, g4s, 1.5e3);
        const double screenIdle = railPlatesNominal - screenResistor * (4.0 * idleScreenCurrent + piAndPreampCurrent);
        ch.penA = c.addPentode (ch.wPP1, g3s, gnd, pentode6V6(), screenIdle);
        ch.penB = c.addPentode (ch.wPP2, g4s, gnd, pentode6V6(), screenIdle);
        c.setInitialGuess (ch.wPP1, 470.0);
        c.setInitialGuess (ch.wPP2, 470.0);
        c.setInitialGuess (ch.wGridA, -50.0);
        c.setInitialGuess (ch.wGridB, -50.0);
        c.setInitialGuess (g3s, -50.0);
        c.setInitialGuess (g4s, -50.0);
        c.setInitialGuess (ch.wBias, -50.0);

        // Output transformer: 4.3k centre-tapped primary on the plate rail, 8 ohm tap. The 100k off the
        // secondary back into V4a's cathode is the global negative-feedback wire. Winding capacitance and
        // the copper/core loss network follow the SuperLead template -- they damp the primary resonance
        // the speaker's inductance otherwise rings into the NFB loop.
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addCapacitor (ch.wPP1, ch.wPP2, 400.0e-12);
        c.addResistor (ch.wPP1, ch.wPP2, 20.0e3);
        {
            const auto sn = c.addNode();
            c.addResistor (ch.wPP1, sn, 2.0e3);
            c.addCapacitor (sn, ch.wPP2, 3.0e-9);
        }
        c.addCapacitor (ch.wPP1, gnd, 400.0e-12);
        c.addCapacitor (ch.wPP2, gnd, 400.0e-12);
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance);
        const double turns = std::sqrt (4300.0 / speakerNominal[matchedSpeaker]) / 2.0;
        const double lh = primaryHalfInductance;
        const double ls = lh / (turns * turns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, ch.wPP1 }, { a2, ch.wPP2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        ch.wOut = c.addNode();
        c.addResistor (sw, ch.wOut, secondaryResistance);
        ch.rNfb = c.addResistor (ch.wOut, ch.wMixK, 100.0e3);
        // Stray capacitance in the feedback path (SuperLead template: without this pole the loop's gain
        // does not fall before the model's Nyquist and an inductive load destabilises the closed loop).
        c.addCapacitor (ch.wMixK, gnd, 1.5e-9);
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
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
    }
}

void JimKelleyStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void JimKelleyStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Fixed-bias supply: -50 V nominal (the marked bias rail), service-tweak range -25 V .. -75 V.
    // Bias knob -> trimmer loading the -80 V bias supply behind its 15k dropper: the node's
    // idle level is -80 * t / (15k + t). Knob 0..1 sweeps -30 V (hot) .. -70 V (cold), -50 V at noon.
    const double biasMag = 30.0 + 40.0 * k.bias;
    const double biasTrim = 15.0e3 * biasMag / (80.0 - biasMag);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);

    // Switchable elements (cache the applied state -- setCapacitance/setResistance on an unchanged value
    // still costs a rebuild of that element's stamp).
    const double brightC = k.bright ? 560.0e-12 : 1.0e-15;
    const double midC = k.midBoost ? 470.0e-12 : 1.0e-15;
    const double presR = k.presence ? 1.5e3 : 1.0e9;

    for (auto& ch : channels)
    {
        const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
        const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
        const double trebleTop = juce::jmax (1.0, 500.0e3 * (1.0 - k.treble)); // LIN pot
        const double bassTop = juce::jmax (1.0, 500.0e3 * (1.0 - k.bass));
        ch.pre.setResistance (ch.rGainTop, juce::jmax (1.0, 1.0e6 - gainBottom));
        ch.pre.setResistance (ch.rGainBot, gainBottom);
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        ch.pre.setResistance (ch.rTrebleTop, trebleTop);
        ch.pre.setResistance (ch.rTrebleBot, juce::jmax (1.0, 500.0e3 - trebleTop));
        ch.pre.setResistance (ch.rBassTop, bassTop);
        ch.pre.setResistance (ch.rBassBot, juce::jmax (1.0, 500.0e3 - bassTop));
        if (k.bright != appliedBright)
            ch.pre.setCapacitance (ch.capBright, brightC);
        if (k.midBoost != appliedMidBoost)
            ch.pre.setCapacitance (ch.capMidBoost, midC);
        if (! reducedOrder)
        {
            if (k.presence != appliedPresence)
                ch.power.setResistance (ch.rPres, presR);
            ch.power.setResistance (ch.rBiasTrim, biasTrim);
            // Power Select 30 W: one 6V6 pair's screens drop to 0 V (the real switch unplugs a pair).
            if (k.powerSel == 1)
                ch.power.setPentodeScreen (ch.penB, 0.0);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    appliedBright = k.bright;
    appliedMidBoost = k.midBoost;
    appliedPresence = k.presence;
    appliedPowerSel = k.powerSel;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void JimKelleyStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void JimKelleyStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    const double measDt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    if (ch.sumCount > 0)
    {
        // Same ~200 ms reservoir response the other amps use -- bounded sag, no motorboating.
        const double mk = 1.0 - std::exp (-measDt / 0.2);
        ch.fbPlate += mk * (ch.sumPlate / n - ch.fbPlate);
        ch.fbScreen += mk * (ch.sumScreen / n - ch.fbScreen);
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.5, ch.fbPlate));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.fbScreen + piAndPreampCurrent));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const double plates = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sA));
    const double screens = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sB));
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcRail, plates);
        ch.power.setPentodeScreen (ch.penA, screens);
        if (lastKnobs.powerSel == 0) // 60 W: both pairs fed; 30 W keeps penB's screens at 0 V
            ch.power.setPentodeScreen (ch.penB, screens);
        ch.power.setSource (ch.wSrcPi, ch.piRail);
    }
    constexpr double decouplingTau = 9.1e3 * 20.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double a = 1.0 - std::exp (-dt / decouplingTau);
    ch.piRail += a * (screens * piFraction - ch.piRail);
    // LF tracker on the preamp output tap (~32 Hz corner): removes the supply-sag swell before the power
    // stage, same load-bearing role as in the Carr Rambler model.
    ch.outDc += (1.0 - std::exp (-dt / 0.005)) * (ch.pre.voltage (ch.pOut) - ch.outDc);
    const double b = 1.0 - std::exp (-dt / (2.0 * decouplingTau));
    ch.preRail += b * (screens * preFraction - ch.preRail);
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
}

double JimKelleyStyleAmplifierProcessor::preampOutput (const Channel& ch) const noexcept
{
    return ch.pre.voltage (ch.pOut) - ch.outDc;
}

double JimKelleyStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (driveVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    const double feel = juce::jlimit (0.0, 1.0, (double) lastKnobs.tubeFeel);
    const double biasTrim = juce::jlimit (0.0, 1.0, (double) lastKnobs.bias);
    ch.bmRail = bmSagRail[0] - feel * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));
    if (lastKnobs.powerSel == 1)
        ch.bmRail *= 0.7; // 30 W: one pair disengaged -- less swing, earlier onset

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double envScale = juce::jlimit (0.0, 1.0, ch.bmEnvelope / juce::jmax (1.0e-9, k));
    const double asym = (bmAsym * envScale + 0.3 * (biasTrim - 0.5)) * bmYmax;
    const double drive = driveVoltage + asym;
    const auto knee = [&] (double x) { return bmYmax * x / std::pow (1.0 + std::pow (x, bmKneeN), 1.0 / bmKneeN); };
    const auto f = [&] (double x) { const double ux = std::abs (x) / juce::jmax (1.0e-9, k);
                                    return std::copysign (knee (ux), x); };
    const double raw = juce::jlimit (-bmYmax, bmYmax, f (drive) - f (asym)) * ch.bmRail;

    const double dcCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmDcHz / juce::jmax (1.0, sampleRate));
    ch.bmDcState += dcCoeff * (raw - ch.bmDcState);
    const double blocked = raw - ch.bmDcState;

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (blocked - ch.bmToneState);
    ch.bmOutput = ch.bmTail.process ((ch.bmToneState + bmShelfHfGain * (blocked - ch.bmToneState)) * bmLevelTrim);
    return ch.bmOutput;
}

double JimKelleyStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::stage1Plate: return ch.pre.voltage (ch.pPlate1);
        case Probe::stage1Cathode: return ch.pre.voltage (ch.pK1);
        case Probe::clipPlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::clipCathode: return ch.pre.voltage (ch.pK2);
        case Probe::toneOut: return ch.pre.voltage (ch.pTone);
        case Probe::recPlate: return ch.pre.voltage (ch.pPlate3);
        case Probe::recCathode: return ch.pre.voltage (ch.pK3);
        case Probe::pOut: return preampOutput (ch);
        case Probe::mixPlate: return ch.power.voltage (ch.wMixPlate);
        case Probe::mixCathode: return ch.power.voltage (ch.wMixK);
        case Probe::piPlate: return ch.power.voltage (ch.wPiPlate);
        case Probe::piCathode: return ch.power.voltage (ch.wPiK);
        case Probe::powerGridA: return ch.power.voltage (ch.wGridA);
        case Probe::powerGridB: return ch.power.voltage (ch.wGridB);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::biasNode: return ch.power.voltage (ch.wBias);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::reverbRet: return ch.power.voltage (ch.wRev);
    }
    return 0.0;
}

void JimKelleyStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

double JimKelleyStyleAmplifierProcessor::plateCurrentA() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, s = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, s);
    return a;
}

double JimKelleyStyleAmplifierProcessor::plateCurrentB() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double b = 0.0, s = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penB, b, s);
    return b;
}

void JimKelleyStyleAmplifierProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize, numChannels);
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto bmVoicing = voicings::fenderClean;
        bmVoicing.asym = 0.0; // even-order content already lives in the knee's f(drive)-f(asym) law
        // Voiced for the EVM-12L pairing: tight, high-power, more extended top than an open-back alnico.
        bmVoicing.otHz = 11000.0;
        bmVoicing.cabLpHz = 6200.0;
        bmVoicing.cabAirHz = 8600.0;
        bmVoicing.cabResHz = 90.0;
        bmVoicing.cabResDb = 1.0;
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
        ch.spring.prepare (newSampleRate);
    }

    const auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p, double time)
    {
        s.reset (newSampleRate, time);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedReverb, reverbParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = idlePlateCurrent * 4.0 + idleScreenCurrent * 4.0 + piAndPreampCurrent;
    appliedSpeaker = matchedSpeaker;
    // -1 forces the first updatePots to apply every switch element (build defaults are the OFF state).
    appliedBright = appliedMidBoost = appliedPresence = appliedPowerSel = -1;
    updatePots ({ gainParam->get(), volumeParam->get(), trebleParam->get(), bassParam->get(), reverbParam->get(),
                  powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()),
                  juce::roundToInt (presenceParam->get()), juce::roundToInt (brightParam->get()),
                  juce::roundToInt (midBoostParam->get()), juce::roundToInt (powerSelParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * preFraction);
        const bool preOk = ch.pre.prepare (newSampleRate);
        dcOk = preOk && dcOk;
        ch.outDc = ch.pre.voltage (ch.pOut);
        ch.driveDc = 0.0;
        ch.driveDcCoeff = 1.0 - std::exp (-1.0 / (0.03 * newSampleRate)); // ~5 Hz coupling corner

        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcIn, 0.0);
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB) * piFraction);
            ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sA));
            const double scr = ch.supply.voltage (ch.sB);
            ch.power.setPentodeScreen (ch.penA, scr);
            ch.power.setPentodeScreen (ch.penB, scr);
            const bool powOk = ch.power.prepare (newSampleRate);
            dcOk = powOk && dcOk;
            ch.power.solveSample();
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
        }
        ch.supply.setCurrentSource (ch.iA, -(ipA + ipB));
        ch.supply.setCurrentSource (ch.iB, -(isA + isB + piAndPreampCurrent));
        idleSupplyCurrent = ipA + ipB + isA + isB + piAndPreampCurrent;
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
        dcOk = ch.supply.prepare (supplyRate) && dcOk;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sA));
            const double scr = ch.supply.voltage (ch.sB);
            ch.power.setPentodeScreen (ch.penA, scr);
            if (appliedPowerSel == 0)
                ch.power.setPentodeScreen (ch.penB, scr);
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB) * piFraction);
        }
        ch.piRail = ch.supply.voltage (ch.sB) * piFraction;
        ch.preRail = ch.supply.voltage (ch.sB) * preFraction;
        ch.pre.setSource (ch.pSrcRail, ch.preRail);
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmToneState = 0.0;
        ch.bmDcState = 0.0;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    recoveries = 0;
    shortcut.reset();
}

void JimKelleyStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedGain.setTargetValue (gainParam->get());
    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedReverb.setTargetValue (reverbParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int presenceChoice = juce::roundToInt (presenceParam->get());
    const int brightChoice = juce::roundToInt (brightParam->get());
    const int midBoostChoice = juce::roundToInt (midBoostParam->get());
    const int powerSelChoice = juce::roundToInt (powerSelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        Knobs knobs {};
        knobs.gain = smoothedGain.getNextValue();
        knobs.volume = smoothedVolume.getNextValue();
        knobs.treble = smoothedTreble.getNextValue();
        knobs.bass = smoothedBass.getNextValue();
        const float rv = smoothedReverb.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            knobs.reverb = rv;
            knobs.powerDrive = pw;
            knobs.bias = bi;
            knobs.tubeFeel = fe;
            knobs.speaker = speakerChoice;
            knobs.presence = presenceChoice;
            knobs.bright = brightChoice;
            knobs.midBoost = midBoostChoice;
            knobs.powerSel = powerSelChoice;
            updatePots (knobs);
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            const double preOut = preampOutput (ch);
            // Spring send taps the preamp output like the real amp's 12AT7 driver; the tank input is
            // normalised (pOut sits at roughly +-20 V full drive).
            const double springOut = rv > 0.0f ? ch.spring.process (preOut / 20.0) : 0.0;
            const double revReturn = rv * 30.0 * springOut; // scaled to volts at the 470k mixer leg

            const double driveRaw = masterGain * preOut;
            ch.driveDc += ch.driveDcCoeff * (driveRaw - ch.driveDc);
            const double drive = driveRaw - ch.driveDc;
            bool ok2 = true; // reducedOrder: ch.power is empty, nothing to solve
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, drive);
                ch.power.setSource (ch.wSrcRev, revReturn);
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
                // Each pentode element models a side's PARALLELED pair -- the supply sees four real tubes.
                const double mult = powerSelChoice == 1 ? 1.0 : 2.0;
                ch.sumPlate += mult * (ipA + ipB);
                ch.sumScreen += mult * (isA + isB);
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, drive + revReturn)
                                                     : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 200.0;
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
                out = speakerVolts * (reducedOrder ? outputScale : fullOutputScale) * outGain * speakerGain;
                if (ch.alignOutput)
                {
                    ch.declick = ch.lastEmitted - out;
                    ch.alignOutput = false;
                }
                out += ch.declick;
                ch.declick *= declickDecay;
                out = outputLimit (out);
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

void JimKelleyStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No dedicated amp glyph exists on the reference sheet yet (same placeholder the other amps use).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
