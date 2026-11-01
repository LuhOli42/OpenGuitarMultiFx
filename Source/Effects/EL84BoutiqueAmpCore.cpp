#include "EL84BoutiqueAmpCore.h"

#include "IconKit.h"
#include "PotTaper.h"
#include "TubeAmpCommon.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double railNominal = 400.0;
    constexpr double speakerNominal[3] { 4.0, 8.0, 16.0 };
    constexpr double primaryHalfInductance = 4.0;
    constexpr double halfToSecondaryTurns = 7.906;
    constexpr double couplingHalves = 0.999;
    constexpr double couplingSecondary = 0.998;
    constexpr double primaryHalfResistance = 40.0;
    constexpr double speakerEddyLoss = 150.0;

    double boundedPot (double value, double maximum) noexcept
    {
        return juce::jmax (1.0, maximum * juce::jlimit (0.0, 1.0, value));
    }
}

namespace
{
// Alternates this amp family's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::greenback412, &tubeamp::cabs::v30_412,
                                              &tubeamp::cabs::jensen212 };
}

EL84BoutiqueAmpCore::EL84BoutiqueAmpCore (Model ampModel, TubeFits fits, bool& reduced, const char* name)
    : model (ampModel), tubes (fits), reducedOrder (reduced), displayName (name)
{
    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "el84_amp", displayName, "|");
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("el84_page2", "Page 2", "|");
    const auto add = [&] (const char* id, const char* name, float value)
    {
        return addFloat (*group, id, name, value);
    };

    if (model == Model::matchlessHC30)
    {
        auto channel = std::make_unique<juce::AudioParameterFloat> (
            "hc30_channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return juce::roundToInt (v) == 0 ? juce::String ("Ch1 12AX7") : juce::String ("Ch2 EF86"); }));
        channelParam = channel.get();
        group->addChild (std::move (channel));
        // audio-tapered level knobs: audioInverse() keeps each default's wiper fraction
        // where it sat before the pots became audio taper (rotation, not fraction, is stored).
        volume1Param = add ("hc30_volume1", "Volume 1", (float) pots::audioInverse (0.4));
        bass1Param = add ("hc30_bass1", "Bass 1", 0.5f);
        treble1Param = add ("hc30_treble1", "Treble 1", 0.5f);
        volume2Param = add ("hc30_volume2", "Volume 2", (float) pots::audioInverse (0.4));
        auto tone = std::make_unique<juce::AudioParameterFloat> (
            "hc30_tone2", "Tone 2", juce::NormalisableRange<float> (0.0f, 5.0f, 1.0f), 5.0f); // default: the full .01 position
        tone2Param = tone.get();
        group->addChild (std::move (tone));
        cutParam = add ("hc30_cut", "Cut", 0.5f);
        masterParam = add ("hc30_master", "Master", (float) pots::audioInverse (0.7));
    }
    else if (model == Model::hotCat30)
    {
        // Real front panel (Bad Cat Hot Cat 30 schematic): Gain ch has Gain/Edge/Bass/
        // Treble/Level/Master/Brilliance; Clean ch has its own Volume. There is no
        // Middle control and no shared EF86 -- both channels are 12AX7.
        auto channel = std::make_unique<juce::AudioParameterFloat> (
            "hotcat30_channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return juce::roundToInt (v) == 0 ? juce::String ("Gain") : juce::String ("Clean"); }));
        channelParam = channel.get();
        group->addChild (std::move (channel));
        gainParam = add ("hotcat30_gain", "Gain", (float) pots::audioInverse (0.5));
        edgeParam = add ("hotcat30_edge", "Edge", 1.0f);
        bassParam = add ("hotcat30_bass", "Bass", 0.5f);
        trebleParam = add ("hotcat30_treble", "Treble", 0.5f);
        levelParam = add ("hotcat30_level", "Level", 0.7f);
        volume1Param = add ("hotcat30_volume", "Clean Volume", (float) pots::audioInverse (0.5));
        brillianceParam = add ("hotcat30_brilliance", "Brilliance", 0.7f);
        masterParam = add ("hotcat30_master", "Master", (float) pots::audioInverse (0.7));
    }
    else if (model == Model::carmenGhia)
    {
        volume1Param = add ("cghia_volume", "Volume", (float) pots::audioInverse (0.4));
        toneParam = add ("cghia_tone", "Tone", 0.5f);
        masterParam = add ("cghia_master", "Master", 1.0f);
    }
    else
    {
        preampParam = add ("astro16_preamp", "Preamp", (float) pots::audioInverse (0.5));
        bassParam = add ("astro16_bass", "Bass", 0.5f);
        midParam = add ("astro16_mid", "Middle", 0.5f);
        trebleParam = add ("astro16_treble", "Treble", 0.5f);
        volume1Param = add ("astro16_volume", "Volume", (float) pots::audioInverse (0.5));
        presenceParam = add ("astro16_presence", "Presence", 0.5f);
    }

    powerParam = addFloat (*page2, model == Model::matchlessHC30 ? "hc30_power" :
                                   model == Model::hotCat30 ? "hotcat30_power" :
                                   model == Model::carmenGhia ? "cghia_power" : "astro16_power",
                           "Power Drive", 1.0f);
    biasParam = addFloat (*page2, model == Model::matchlessHC30 ? "hc30_bias" :
                                  model == Model::hotCat30 ? "hotcat30_bias" :
                                  model == Model::carmenGhia ? "cghia_bias" : "astro16_bias",
                          "Bias", 0.5f);
    feelParam = addFloat (*page2, model == Model::matchlessHC30 ? "hc30_tube_feel" :
                                  model == Model::hotCat30 ? "hotcat30_tube_feel" :
                                  model == Model::carmenGhia ? "cghia_tube_feel" : "astro16_tube_feel",
                          "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        model == Model::matchlessHC30 ? "hc30_speaker" :
        model == Model::hotCat30 ? "hotcat30_speaker" :
        model == Model::carmenGhia ? "cghia_speaker" : "astro16_speaker",
        "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 2.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            constexpr double ohms[] { 4.0, 8.0, 16.0 };
            return juce::String (ohms[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));
    speakerParam = speaker.get();
    page2->addChild (std::move (speaker));
    outputParam = addFloat (*page2, model == Model::matchlessHC30 ? "hc30_output" :
                                    model == Model::hotCat30 ? "hotcat30_output" :
                                    model == Model::carmenGhia ? "cghia_output" : "astro16_output",
                           "Output", 0.5f);
    auto cab = tubeamp::makeCabParam (
        model == Model::matchlessHC30 ? "hc30_cab" :
        model == Model::hotCat30 ? "hotcat30_cab" :
        model == Model::carmenGhia ? "cghia_cab" : "astro16_cab",
        model == Model::matchlessHC30 ? "2x12 Alnico" :
        model == Model::hotCat30 ? "2x12 Bad Cat" :
        model == Model::carmenGhia ? "1x12 Z" : "1x12 Soldano",
        cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

juce::AudioParameterFloat* EL84BoutiqueAmpCore::addFloat (juce::AudioProcessorParameterGroup& group,
                                                           const char* id, const char* name, float value)
{
    auto parameter = std::make_unique<juce::AudioParameterFloat> (id, name,
        juce::NormalisableRange<float> (0.0f, 1.0f), value);
    auto* result = parameter.get();
    group.addChild (std::move (parameter));
    return result;
}

juce::AudioParameterFloat* EL84BoutiqueAmpCore::findParam (const char* id) const
{
    for (auto* parameter : parameters->getParameters (true))
        if (auto* asFloat = dynamic_cast<juce::AudioParameterFloat*> (parameter))
            if (asFloat->paramID == id)
                return asFloat;
    return nullptr;
}

double EL84BoutiqueAmpCore::param (const juce::AudioParameterFloat* parameter, double fallback) const noexcept
{
    return parameter != nullptr ? (double) parameter->get() : fallback;
}

// One slot per continuous read site inside updatePots(). The slot's
// SmoothedValue is advanced by potTickSamples here because updatePots()
// runs exactly once per channel per control tick -- a knob jump then
// lands as ~30 small resistor edits over ~10 ms instead of one step,
// which is what de-clicks it.
double EL84BoutiqueAmpCore::smoothedParam (Channel& ch, int slot,
                                           const juce::AudioParameterFloat* parameter,
                                           double fallback) noexcept
{
    auto& s = ch.smoothedParams[(size_t) slot];
    const float v = parameter != nullptr ? parameter->get() : (float) fallback;
    s.setTargetValue (v);
    if (ch.smoothedParamsNeedSnap)
        s.setCurrentAndTargetValue (v);
    return (double) s.skip (potTickSamples);
}

void EL84BoutiqueAmpCore::buildPreamp (Channel& ch, NodalCircuit& c, bool channelTwo)
{
    const auto gnd = NodalCircuit::ground;
    c.setIntegrationTheta (0.7);
    const auto in = c.addNode();
    const int srcIn = c.addSource (in, 0.0);
    const auto rail = c.addNode();
    const int srcRail = c.addSource (rail, railNominal * 0.725);
    if (channelTwo) { ch.preInput2 = srcIn; ch.preRail2 = srcRail; }
    else { ch.preInput = srcIn; ch.preRail = srcRail; }

    const auto addTriode = [&] (NodalCircuit::Node input, double plateLoad, double cathodeR,
                                double cathodeC, double couplingC, bool bypass)
    {
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode(), out = c.addNode();
        c.addCapacitor (input, grid, couplingC);
        c.addResistor (grid, gnd, 1.0e6);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addResistor (rail, plate, plateLoad);
        c.addResistor (cathode, gnd, cathodeR);
        if (bypass && cathodeC > 0.0)
            c.addCapacitor (cathode, gnd, cathodeC);
        c.addCapacitor (plate, out, 0.022e-6);
        c.addResistor (out, gnd, 1.0e6);
        c.setInitialGuess (plate, railNominal * 0.55);
        c.setInitialGuess (cathode, 1.2);
        return std::pair { plate, out };
    };

    // DC-30 Ch2 EF86 (el34world trace): 68k/39k grid stoppers off the 1M input leak, 330k plate, screen through 2M2
    // (0.1 uF to ground: prepare()/process() track its voltage from the screen current), 2k2 + 25 uF cathode. The
    // plate drives the TONE selector's series caps directly.
    const auto addEF86 = [&] (NodalCircuit::Node input)
    {
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode();
        c.addResistor (input, grid, 68.0e3);
        ch.ef86Pen = c.addPentode (plate, grid, cathode, tubes.ef86, ch.ef86Screen);
        c.addResistor (rail, plate, 330.0e3);
        c.addResistor (cathode, gnd, 2.2e3);
        c.addCapacitor (cathode, gnd, 25.0e-6);
        c.setInitialGuess (plate, 180.0);
        c.setInitialGuess (cathode, 1.2);
        ch.prePlate = plate;
        return plate;
    };

    // Matchless-family interactive Bass/Treble pair: each leg gets its own
    // coupling cap into a divider pot whose wiper lands on the shared output
    // node (DC-30 ch1: 56pF/.022uF with 220k/1MA; Hot Cat 30: 22nF/22nF with
    // 1M/1M5 plus the 50pF bright cap and 10k ground leg).
    const auto matchlessTone = [&] (NodalCircuit::Node input, double cTreble, double cBass,
                                  double pTreble, double pBass, double rLoad, double cBright)
    {
        const auto trebleIn = c.addNode(), bassIn = c.addNode(), out = c.addNode();
        c.addCapacitor (input, trebleIn, cTreble);
        c.addCapacitor (input, bassIn, cBass);
        ch.rTrebleTop = c.addResistor (trebleIn, out, pTreble);
        ch.rTrebleBottom = c.addResistor (out, gnd, pTreble);
        ch.rBassTop = c.addResistor (bassIn, out, pBass);
        ch.rBassBottom = c.addResistor (out, gnd, pBass);
        if (rLoad > 0.0)
            c.addResistor (out, gnd, rLoad);
        if (cBright > 0.0)
            c.addCapacitor (input, out, cBright);
        return out;
    };

    if (model == Model::matchlessHC30 && ! channelTwo)
    {
        // DC-30/HC-30 channel 1 (factory schematic): parallel 12AX7 pair (shared 220k
        // plate, 820R + 25uF cathode) -> 560pF -> VOL 500kA -> 180pF -> V2a
        // (100k plate, 1k2 + 25uF) -> V2b cathode follower (47k) -> 56pF -> T/B.
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode(), wiper = c.addNode();
        c.addResistor (in, grid, 68.0e3);
        c.addResistor (grid, gnd, 1.0e6);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addResistor (rail, plate, 220.0e3);
        c.addResistor (cathode, gnd, 820.0);
        c.addCapacitor (cathode, gnd, 25.0e-6);
        c.setInitialGuess (plate, 200.0);
        c.setInitialGuess (cathode, 1.0);
        ch.prePlate = plate;

        const auto volTop = c.addNode();
        c.addCapacitor (plate, volTop, 560.0e-12);
        ch.preVolumeTop = c.addResistor (volTop, wiper, 500.0e3);
        ch.preVolumeBottom = c.addResistor (wiper, gnd, 500.0e3);

        const auto v2aGrid = c.addNode(), v2aCath = c.addNode(), v2aPlate = c.addNode();
        c.addCapacitor (wiper, v2aGrid, 180.0e-12);
        c.addResistor (v2aGrid, gnd, 1.0e6);
        c.addTriode (v2aPlate, v2aGrid, v2aCath, tubes.triode);
        c.addResistor (rail, v2aPlate, 100.0e3);
        c.addResistor (v2aCath, gnd, 1.2e3);
        c.addCapacitor (v2aCath, gnd, 25.0e-6);
        c.setInitialGuess (v2aPlate, 200.0);
        c.setInitialGuess (v2aCath, 1.2);

        // V2b cathode follower (56k) -> Fender-style Treble/Bass stack (DC-30 trace): 56 pF to TREBLE 1MA, 18k slope,
        // .022 to the treble-bottom / bass-top node, .022 to the BASS 1MA wiper (bass bottom to ground, no mid pot);
        // output from the treble wiper through 220k into 18k to ground (-> FX send -> PI grid A).
        const auto follower = c.addNode();
        c.addFollower (v2aPlate, follower, 1.5);
        c.addResistor (follower, gnd, 56.0e3);
        const auto trebleTop = c.addNode(), slope = c.addNode(), mid = c.addNode(), bassWiper = c.addNode(), trebleWiper = c.addNode();
        c.addCapacitor (follower, trebleTop, 56.0e-12);
        c.addResistor (follower, slope, 18.0e3);
        c.addCapacitor (slope, mid, 0.022e-6);
        c.addCapacitor (slope, bassWiper, 0.022e-6);
        ch.rTrebleTop = c.addResistor (trebleTop, trebleWiper, 1.0e6);
        ch.rTrebleBottom = c.addResistor (trebleWiper, mid, 1.0e6);
        ch.rBassTop = c.addResistor (mid, bassWiper, 1.0e6);
        ch.rBassBottom = c.addResistor (bassWiper, gnd, 1.0e6);
        const auto out = c.addNode();
        c.addResistor (trebleWiper, out, 220.0e3);
        c.addResistor (out, gnd, 18.0e3);
        ch.toneOut = out;
        ch.preOut = out;
        return;
    }

    if (model == Model::matchlessHC30)
    {
        // Channel 2 (DC-30 trace): EF86 plate -> TONE rotary = the SERIES coupling cap (360p / 56p / .0012 / .0022 /
        // .0047 / .01 + 180p, each with a 5M bleed) -> VOL 1MA (180 pF bright) -> wiper, 470k to ground -> PI grid B.
        const auto plate = addEF86 (in);
        const auto volTop = c.addNode(), volWiper = c.addNode();
        ch.toneCap = c.addCapacitor (plate, volTop, 10.18e-9);
        c.addResistor (volTop, gnd, 5.0e6);
        ch.preVolume2Top = c.addResistor (volTop, volWiper, 1.0e6);
        ch.preVolume2Bottom = c.addResistor (volWiper, gnd, 1.0e6);
        c.addCapacitor (volTop, volWiper, 180.0e-12);
        c.addResistor (volWiper, gnd, 470.0e3);
        ch.preOut2 = volWiper;
        return;
    }

    if (model == Model::hotCat30 && channelTwo)
    {
        // Clean channel (Bad Cat schematic): 68k -> V1b -> VOLUME 1M log + 180pF
        // bright -> G (the V4b side of the PI). 220k plate, 1k5 + 22uF, 1n5
        // bright cap across the plate load.
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode();
        c.addResistor (in, grid, 68.0e3);
        c.addResistor (grid, gnd, 1.0e6);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addResistor (rail, plate, 220.0e3);
        c.addCapacitor (plate, rail, 1.5e-9);
        c.addResistor (cathode, gnd, 1.5e3);
        c.addCapacitor (cathode, gnd, 22.0e-6);
        c.setInitialGuess (plate, 150.0);
        c.setInitialGuess (cathode, 1.1);
        ch.prePlate = plate;

        const auto volWiper = c.addNode(), out = c.addNode();
        ch.preVolumeTop = c.addResistor (plate, volWiper, 1.0e6);
        ch.preVolumeBottom = c.addResistor (volWiper, gnd, 1.0e6);
        c.addCapacitor (plate, volWiper, 180.0e-12);
        c.addResistor (volWiper, out, 1.0);
        c.addResistor (out, gnd, 1.0e6);
        ch.preOut2 = out;
        return;
    }

    if (model == Model::hotCat30)
    {
        // Gain channel (Bad Cat schematic): V1a (220k plate + 1n5 bright) ->
        // 470k -> node [1n5 to gnd; GAIN = 250k log shunt leg to ground] -> 470k
        // -> V2b (220k, 1k5 + 100n) -> 22n -> node (10n + 470k to gnd) -> EDGE
        // (1M log series rheostat) -> 150k + 360p to gnd -> V2a (100k, 1k5 +
        // 1uF) -> 10n -> LEVEL 1M lin -> 470k -> V3b (100k, 1k; 270k leak) ->
        // DC-coupled V3a follower (56k) -> T/B -> 220k -> MASTER.
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode();
        c.addResistor (in, grid, 68.0e3);
        c.addResistor (grid, gnd, 1.0e6);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addResistor (rail, plate, 220.0e3);
        c.addCapacitor (plate, rail, 1.5e-9);
        c.addResistor (cathode, gnd, 1.0e3);
        c.setInitialGuess (plate, 99.0);
        c.setInitialGuess (cathode, 1.0);
        ch.prePlate = plate;

        const auto gainNode = c.addNode(), v2bGrid = c.addNode();
        const auto v2bCath = c.addNode(), v2bPlate = c.addNode();
        c.addResistor (plate, gainNode, 470.0e3);
        c.addCapacitor (gainNode, gnd, 1.5e-9);
        ch.rGainShunt = c.addResistor (gainNode, gnd, 250.0e3);
        c.addResistor (gainNode, v2bGrid, 470.0e3);
        c.addTriode (v2bPlate, v2bGrid, v2bCath, tubes.triode);
        c.addResistor (rail, v2bPlate, 220.0e3);
        c.addResistor (v2bCath, gnd, 1.5e3);
        c.addCapacitor (v2bCath, gnd, 0.1e-6);
        c.setInitialGuess (v2bPlate, 145.0);
        c.setInitialGuess (v2bCath, 1.15);

        const auto nodeN = c.addNode(), nodeE = c.addNode();
        const auto v2aGrid = c.addNode(), v2aCath = c.addNode(), v2aPlate = c.addNode();
        c.addCapacitor (v2bPlate, nodeN, 22.0e-9);
        c.addCapacitor (nodeN, gnd, 10.0e-9);
        c.addResistor (nodeN, gnd, 470.0e3);
        ch.rEdge = c.addResistor (nodeN, nodeE, 1.0e6);
        c.addResistor (nodeE, gnd, 150.0e3);
        c.addCapacitor (nodeE, gnd, 360.0e-12);
        c.addResistor (nodeE, v2aGrid, 1.0);
        c.addTriode (v2aPlate, v2aGrid, v2aCath, tubes.triode);
        c.addResistor (rail, v2aPlate, 100.0e3);
        c.addResistor (v2aCath, gnd, 1.5e3);
        c.addCapacitor (v2aCath, gnd, 1.0e-6);
        c.setInitialGuess (v2aPlate, 225.0);
        c.setInitialGuess (v2aCath, 1.7);

        // LEVEL pot: 1M linear divider after the 10nF coupling (470k leak on
        // the top); wiper feeds V3b's grid through 470k (270k grid leak).
        const auto levelTop = c.addNode(), levelWiper = c.addNode();
        const auto v3bGrid = c.addNode(), v3bCath = c.addNode(), v3bPlate = c.addNode();
        c.addCapacitor (v2aPlate, levelTop, 10.0e-9);
        c.addResistor (levelTop, gnd, 470.0e3);
        ch.preVolume2Top = c.addResistor (levelTop, levelWiper, 1.0e6);
        ch.preVolume2Bottom = c.addResistor (levelWiper, gnd, 1.0e6);
        c.addResistor (levelWiper, v3bGrid, 470.0e3);
        c.addResistor (v3bGrid, gnd, 270.0e3);
        c.addTriode (v3bPlate, v3bGrid, v3bCath, tubes.triode);
        c.addResistor (rail, v3bPlate, 100.0e3);
        c.addResistor (v3bCath, gnd, 1.0e3);
        c.setInitialGuess (v3bPlate, 193.0);
        c.setInitialGuess (v3bCath, 1.2);

        // V3a: DC-coupled cathode follower -- grid sits on V3b's plate, plate
        // straight at the rail, cathode drives the T/B stack through 56k.
        const auto follower = c.addNode();
        c.addTriode (rail, v3bPlate, follower, tubes.triode);
        c.addResistor (follower, gnd, 56.0e3);
        c.setInitialGuess (follower, 194.0);
        ch.toneOut = matchlessTone (follower, 22.0e-9, 22.0e-9, 1.0e6, 1.5e6, 10.0e3, 50.0e-12);
        const auto out = c.addNode();
        c.addResistor (ch.toneOut, out, 220.0e3);
        c.addResistor (out, gnd, 1.0e6);
        ch.preOut = out;
        return;
    }

    if (model == Model::carmenGhia)
    {
        // Phoenix Project redraw rev 1.3 (docs/circuits/DrZCarmenGhia.md): R29 68k / R27 1M into V1a (R21 100k, R23 2k7 +
        // C13 .68 uF -- a partial bypass); C11 .022 into the tone network -- C15 250 pF across R24 + R25 150k, their
        // joint to ground through the TONE pot P2 (500k A) and C17 .01 -- whose far end is the VOLUME P1 (1M A); the
        // wiper drives V1b (R20 100k, R22 1k5 + C12 25 uF); C14 .01 out (R19 / C9 / NFB are in the power block).
        const auto g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        c.addResistor (in, gnd, 1.0e6);
        c.addResistor (in, g1, 68.0e3);
        c.addTriode (p1, g1, k1, tubes.triode);
        c.addCapacitor (g1, p1, 1.7e-12);
        c.addResistor (rail, p1, 100.0e3);
        c.addResistor (k1, gnd, 2.7e3);
        c.addCapacitor (k1, gnd, 0.68e-6);
        c.setInitialGuess (p1, 150.0);
        c.setInitialGuess (k1, 1.43);

        const auto x = c.addNode(), y = c.addNode(), j = c.addNode(), t = c.addNode(), w = c.addNode();
        c.addCapacitor (p1, x, 0.022e-6);
        c.addCapacitor (x, y, 250.0e-12);
        c.addResistor (x, j, 150.0e3);
        c.addResistor (j, y, 150.0e3);
        ch.rTonePot = c.addResistor (j, t, 250.0e3);
        c.addCapacitor (t, gnd, 0.01e-6);
        ch.preVolumeTop = c.addResistor (y, w, 500.0e3);
        ch.preVolumeBottom = c.addResistor (w, gnd, 500.0e3);
        c.setInitialGuess (x, 0.0);

        const auto p2 = c.addNode(), k2 = c.addNode(), o = c.addNode();
        c.addTriode (p2, w, k2, tubes.triode);
        c.addCapacitor (w, p2, 1.7e-12);
        c.addResistor (rail, p2, 100.0e3);
        c.addResistor (k2, gnd, 1.5e3);
        c.addCapacitor (k2, gnd, 25.0e-6);
        c.addCapacitor (p2, o, 0.01e-6);
        c.addResistor (o, gnd, 1.0e6);
        c.setInitialGuess (p2, 187.0);
        c.setInitialGuess (k2, 1.37);
        ch.prePlate = p1;
        ch.preOut = o;
        return;
    }

    // Soldano Astroverb 16, factory schematic (docs/circuits/SoldanoAstroverb16.md):
    //   jack -> 69k stopper (1M to ground) -> V1a (220k, 1k8 + 1uF) -> 22nF -> 1M -> PREAMP 1M pot -> V1b (100k, 2k2
    //   unbypassed) -> 22nF -> 1M || 22nF -> 220k -> V2a grid (330k leak) -> V2a (220k, 1k8 + 1uF) -> 22nF -> 680k /
    //   15k divider -> V2b, paralleled with V3a (the reverb-return mixer, its grid on the omitted reverb pot) on a shared
    //   100k plate and 1k + 100uF cathode -> DC-coupled V3b cathode follower (plate on the 272 V node) -> TMB.
    // The preamp stages hang on the 258 V node (33uF, one 10k below the 272 V node); the drawing's "1k" at V3b's
    // cathode cannot carry its 153 V and is read as the usual 100k (the forum notes this drawing has wrong values).
    // Replaces an estimated five-stage 220k cascade with a 10k/100uF "cold clipper" that is not in the amp: its
    // extra gain made the preamp motorboat through the supply and the solver storm (2026-10-24).
    {
        const auto railA = c.addNode();
        ch.preRailA = c.addSource (railA, railNominal * 0.725 * 272.0 / 258.0);
        const auto stage = [&] (NodalCircuit::Node grid, NodalCircuit::Node plateRail, double plateR, double cathodeR,
                                double cathodeC, double plateGuess, double cathodeGuess)
        {
            const auto cathode = c.addNode(), plate = c.addNode();
            c.addTriode (plate, grid, cathode, tubes.triode);
            c.addResistor (plateRail, plate, plateR);
            c.addResistor (cathode, gnd, cathodeR);
            if (cathodeC > 0.0)
                c.addCapacitor (cathode, gnd, cathodeC);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
            return plate;
        };

        const auto g1a = c.addNode();
        c.addResistor (in, gnd, 1.0e6);
        c.addResistor (in, g1a, 69.0e3);
        const auto p1a = stage (g1a, rail, 220.0e3, 1.8e3, 1.0e-6, 134.0, 1.0);

        const auto c1 = c.addNode(), potTop = c.addNode(), g1b = c.addNode();
        c.addCapacitor (p1a, c1, 22.0e-9);
        c.addResistor (c1, potTop, 1.0e6);
        ch.preVolumeTop = c.addResistor (potTop, g1b, 1.0e6);
        ch.preVolumeBottom = c.addResistor (g1b, gnd, 1.0e6);
        const auto p1b = stage (g1b, rail, 100.0e3, 2.2e3, 0.0, 182.0, 1.6);

        const auto c2 = c.addNode(), x2 = c.addNode(), g2a = c.addNode();
        c.addCapacitor (p1b, c2, 22.0e-9);
        c.addResistor (c2, x2, 1.0e6);
        c.addCapacitor (c2, x2, 22.0e-9);
        c.addResistor (x2, g2a, 220.0e3);
        c.addResistor (g2a, gnd, 330.0e3);
        const auto p2a = stage (g2a, rail, 220.0e3, 1.8e3, 1.0e-6, 187.0, 1.5);

        const auto c3 = c.addNode(), g2b = c.addNode(), g3a = c.addNode();
        c.addCapacitor (p2a, c3, 22.0e-9);
        c.addResistor (c3, g2b, 680.0e3);
        c.addResistor (g2b, gnd, 15.0e3);
        c.addResistor (g3a, gnd, 250.0e3); // V3a grid: the REVERB pot (reverb omitted, the return at rest)
        const auto mixPlate = c.addNode(), mixCathode = c.addNode();
        c.addTriode (mixPlate, g2b, mixCathode, tubes.triode);
        c.addTriode (mixPlate, g3a, mixCathode, tubes.triode);
        c.addResistor (rail, mixPlate, 100.0e3);
        c.addResistor (mixCathode, gnd, 1.0e3);
        c.addCapacitor (mixCathode, gnd, 100.0e-6);
        c.setInitialGuess (mixPlate, 152.0);
        c.setInitialGuess (mixCathode, 1.2);

        const auto follower = c.addNode();
        c.addTriode (railA, mixPlate, follower, tubes.triode);
        c.addResistor (follower, gnd, 100.0e3);
        c.setInitialGuess (follower, 153.0);
        ch.prePlate = p1a;
        ch.preOut = follower;
    }

    {
        // TMB: Soldano values (470pF treble cap, 220k treble, 1M bass, 25k mid,
        // 22nF legs) then the series 10k and VOLUME 1M of the real amp.
        const auto inStack = ch.preOut;
        const auto a = c.addNode(), b = c.addNode(), d = c.addNode(), e = c.addNode(), f = c.addNode();
        c.addCapacitor (inStack, a, 470.0e-12);
        c.addResistor (inStack, b, 33.0e3);
        c.addCapacitor (b, d, 0.022e-6);
        c.addCapacitor (b, e, 0.022e-6);
        ch.toneOut = c.addNode();
        ch.rTrebleTop = c.addResistor (a, ch.toneOut, 250.0e3);
        ch.rTrebleBottom = c.addResistor (ch.toneOut, d, 250.0e3);
        ch.rBassTop = c.addResistor (d, e, 1.0e6);
        ch.rBassBottom = c.addResistor (e, f, 1.0e6);
        c.addResistor (f, gnd, 10.0e3);
        ch.rMid = c.addResistor (ch.toneOut, gnd, 25.0e3);

        const auto seriesNode = c.addNode(), volumeOut = c.addNode();
        c.addResistor (ch.toneOut, seriesNode, 10.0e3);
        ch.preVolume2Top = c.addResistor (seriesNode, volumeOut, 1.0e6);
        ch.preVolume2Bottom = c.addResistor (volumeOut, gnd, 1.0e6);
        ch.preOut = volumeOut;
    }
}

void EL84BoutiqueAmpCore::buildSupply (Channel& ch)
{
    auto& c = ch.supply;
    c.setIntegrationTheta (0.5);
    const auto voc = c.addNode();
    ch.supplyA = c.addNode();
    const auto choke = c.addNode();
    ch.supplyB = c.addNode();
    ch.supplySource = c.addSource (voc, railNominal);
    ch.rectifierResistance = c.addResistor (voc, ch.supplyA,
        model == Model::carmenGhia ? 400.0 : 130.0);
    c.addCapacitor (ch.supplyA, NodalCircuit::ground, 16.0e-6);
    c.addResistor (ch.supplyA, choke, 120.0);
    c.addCoupledInductors ({ { choke, ch.supplyB } }, { 20.0 });
    c.addCapacitor (ch.supplyB, NodalCircuit::ground, 16.0e-6);
    ch.supplyCurrent = c.addCurrentSource (ch.supplyA, -0.15);
    c.setInitialGuess (ch.supplyA, railNominal);
    c.setInitialGuess (choke, railNominal);
    c.setInitialGuess (ch.supplyB, railNominal);
}

void EL84BoutiqueAmpCore::buildPower (Channel& ch)
{
    auto& c = ch.power;
    const auto gnd = NodalCircuit::ground;
    c.setIntegrationTheta (0.9);
    const auto rail = c.addNode();
    ch.powerRail = c.addSource (rail, railNominal);
    const auto input = c.addNode();
    ch.powerInput = c.addSource (input, 0.0);
    ch.powerCf = ch.powerInput;

    ch.piGridA = c.addNode();
    ch.piGridB = c.addNode();
    const auto tail = c.addNode();
    ch.piTail = tail;
    ch.piPlateA = c.addNode();
    ch.piPlateB = c.addNode();
    // Per-schematic LTP values: HC-30/CG use the legacy estimates; Hot Cat runs
    // Elevated-grid-reference LTP on the Matchless/Bad Cat/Soldano circuits:
    // the 1M grid leaks return to a node held a volt or two BELOW the common
    // cathodes (cathodes -> small R -> ref node -> large R to gnd), so the
    // pair idles at ~1 mA per side instead of being starved by a grounded tail.
    // Hot Cat/DC-30: cathodes -> 1k2 -> ref -> 47k -> gnd (~69 V cathodes);
    // Astroverb: cathodes -> 470R -> ref -> 10k -> gnd (~29 V cathodes,
    // 1M/820k leaks). The Carmen Ghia keeps the plain grounded-grid pair.
    const bool isHotCat = model == Model::hotCat30;
    const bool isAstro = model == Model::astroverb16;
    const bool elevatedRef = model == Model::matchlessHC30 || isHotCat;
    const auto piRef = c.addNode();
    const bool isCarmen = model == Model::carmenGhia;
    NodalCircuit::Node nfbJoint = gnd;
    if (isCarmen)
    {
        // CG: C14's output -> R19 220k -> C9 .001 -> the 5751's grid (R26 470k); R10 3M3 NFB lands on the R19 / C9 joint.
        nfbJoint = c.addNode();
        c.addResistor (input, nfbJoint, 220.0e3);
        c.addCapacitor (nfbJoint, ch.piGridA, 0.001e-6);
        c.addResistor (ch.piGridA, gnd, 470.0e3);
        c.setInitialGuess (nfbJoint, 0.0);
    }
    else
    {
        c.addCapacitor (input, ch.piGridA, isAstro ? 0.02e-6 : 0.01e-6);
        c.addResistor (ch.piGridA, elevatedRef ? piRef : gnd, 1.0e6);
    }
    c.addResistor (ch.piGridB, elevatedRef ? piRef : gnd, 1.0e6);
    // Channel 2 feeds the PI's second grid on the interactive two-channel
    // circuits: Hot Cat clean -> 10n -> V4b grid; DC-30 CH2 -> .01/400V -> V7b.
    if (model == Model::matchlessHC30 || isHotCat)
    {
        const auto inputB = c.addNode();
        ch.powerInput2 = c.addSource (inputB, 0.0);
        if (isHotCat)
            c.addCapacitor (inputB, ch.piGridB, 0.01e-6);
        else
            c.addCapacitor (inputB, ch.piGridB, 0.01e-6); // DC-30 CH2 -> .01/400V -> V7b grid
    }
    auto pi = tubes.triode;
    pi.kg1 *= 6.0;
    c.addTriode (ch.piPlateA, ch.piGridA, tail, pi);
    c.addTriode (ch.piPlateB, ch.piGridB, tail, pi);
    c.addCapacitor (ch.piGridA, ch.piPlateA, 1.7e-12);
    c.addCapacitor (ch.piGridB, ch.piPlateB, 1.7e-12);
    c.addResistor (rail, ch.piPlateA, 100.0e3);
    c.addResistor (rail, ch.piPlateB, 100.0e3);
    if (elevatedRef)
    {
        c.addResistor (tail, piRef, isAstro ? 470.0 : 1.2e3);
        c.addResistor (piRef, gnd, isAstro ? 10.0e3 : 47.0e3);
        c.setInitialGuess (piRef, isAstro ? 27.0 : 67.0);
    }
    else
        c.addResistor (tail, gnd, 1.2e3);
    c.setInitialGuess (ch.piPlateA, 260.0);
    c.setInitialGuess (ch.piPlateB, 260.0);
    c.setInitialGuess (tail, isHotCat ? 69.0 : 1.0);

    const auto gridCouplingA = c.addNode(), gridCouplingB = c.addNode();
    const auto gridA = c.addNode(), gridB = c.addNode();
    ch.powerPlateA = c.addNode();
    ch.powerPlateB = c.addNode();
    const double powerCoupling = isHotCat ? 0.033e-6 : 0.047e-6;
    c.addCapacitor (ch.piPlateA, gridCouplingA, powerCoupling);
    c.addCapacitor (ch.piPlateB, gridCouplingB, powerCoupling);
    if (model == Model::matchlessHC30 || isHotCat)
    {
        // Cross-line cap + pot between the two power-grid nodes: the DC-30 CUT
        // (4n7 + 500k) and the Hot Cat BRILLIANCE (4n7 + 1M log) are the same
        // network; only the pot taper/value differs (handled in updatePots).
        const auto cutNode = c.addNode();
        c.addCapacitor (gridCouplingA, cutNode, isHotCat ? 4.7e-9 : 2.2e-9); // DC-30 trace: CUT 250KA + .0022
        ch.rCut = c.addResistor (cutNode, gridCouplingB, 500.0e3);
    }
    // Astroverb runs fixed bias: the 220k grid leaks return to the -16 V bias
    // rail (via the 50k bias pot from B- = -20.5 V) instead of ground.
    // REVERTED: the true fixed-bias rail (-16 V through the 50k bias pot)
    // Newton-storms in this solver under the audit's hard pluck; the shared
    // cathode-bias topology stays for now. See PR notes.
    const NodalCircuit::Node biasRef = gnd;
    c.addResistor (gridCouplingA, biasRef, isCarmen ? 330.0e3 : 220.0e3); // CG: R13 / R17 330k
    c.addResistor (gridCouplingB, biasRef, isCarmen ? 330.0e3 : 220.0e3);
    // Grid stoppers: 1k5 (DC-30), 10k/2W (Hot Cat), 2k2 (Astroverb); CG reuses
    // these handles for its ganged PPIMV, so it starts at 1 ohm.
    const double gridStopper = model == Model::carmenGhia ? 1.0
                             : isHotCat ? 10.0e3 : 1.5e3;
    ch.rMasterTopA = c.addResistor (gridCouplingA, gridA, gridStopper);
    ch.rMasterTopB = c.addResistor (gridCouplingB, gridB, gridStopper);
    ch.cathodeBias = c.addNode();
    const bool fourTubes = model == Model::matchlessHC30;
    const double cathodeR = model == Model::matchlessHC30 ? 68.0 : isHotCat ? 135.0 : isCarmen ? 150.0 : 130.0;
    ch.rCathodeBias = c.addResistor (ch.cathodeBias, gnd, cathodeR);
    c.addCapacitor (ch.cathodeBias, gnd, 250.0e-6);
    const auto& powerTube = fourTubes ? tubes.el84Pair : tubes.el84;
    const double screen = isHotCat ? 367.0 : fourTubes ? 300.0 : 290.0;
    ch.penA = c.addPentode (ch.powerPlateA, gridA, ch.cathodeBias, powerTube, screen);
    ch.penB = c.addPentode (ch.powerPlateB, gridB, ch.cathodeBias, powerTube, screen);
    c.setInitialGuess (ch.powerPlateA, isHotCat ? 376.0 : 350.0);
    c.setInitialGuess (ch.powerPlateB, isHotCat ? 372.0 : 345.0);
    c.setInitialGuess (ch.cathodeBias, fourTubes ? 10.0 : 11.0);
    c.addCapacitor (ch.powerPlateA, ch.powerPlateB, 400.0e-12);
    c.addResistor (ch.powerPlateA, ch.powerPlateB, 20.0e3);
    c.addCapacitor (ch.powerPlateA, gnd, 400.0e-12);
    c.addCapacitor (ch.powerPlateB, gnd, 400.0e-12);

    const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode(), t1 = c.addNode(), t2 = c.addNode();
    c.addResistor (t1, ch.powerPlateA, fourTubes ? 40.0 : 60.0);
    c.addResistor (t2, ch.powerPlateB, fourTubes ? 40.0 : 60.0);
    c.addResistor (rail, a1, primaryHalfResistance);
    c.addResistor (rail, a2, primaryHalfResistance);
    // OT half-primary -> 16R turns: 4xEL84 ~4k p-p (DC-30), 2xEL34 ~4.5k p-p
    // (Hot Cat), 2xEL84 ~8k p-p (CG/Astroverb).
    const double turns = fourTubes ? halfToSecondaryTurns : isHotCat ? 8.4 : 11.18;
    const double lh = fourTubes ? primaryHalfInductance : isHotCat ? 4.0 : 5.0;
    const double ls = lh / (turns * turns);
    const double m12 = -couplingHalves * lh;
    const double mps = couplingSecondary * std::sqrt (lh * ls);
    // Secondary sense: the CG's R10 NFB returns to the PI's grid side (the Astroverb's to its tail), so it needs the
    // opposite winding sense for the loop to be negative.
    const double sense = isCarmen ? -1.0 : 1.0;
    c.addCoupledInductors ({ { a1, t1 }, { a2, t2 }, { sw, gnd } },
                           { lh, m12, -sense * mps,
                             m12, lh, sense * mps,
                             -sense * mps, sense * mps, ls });
    ch.speaker = c.addNode();
    c.addResistor (sw, ch.speaker, 0.2);
    const auto speakerModel = tubeamp::speakerModel (speakerNominal[2]);
    const auto na = c.addNode(), nb = c.addNode();
    ch.rSpeakerRe = c.addResistor (ch.speaker, na, speakerModel.re);
    ch.grpSpeakerLe = c.addCoupledInductors ({ { na, nb } }, { speakerModel.le });
    ch.rSpeakerEddy = c.addResistor (na, nb, speakerEddyLoss);
    ch.rSpeakerRp = c.addResistor (nb, gnd, speakerModel.rp);
    ch.grpSpeakerLp = c.addCoupledInductors ({ { nb, gnd } }, { speakerModel.lp });
    ch.capSpeakerCp = c.addCapacitor (nb, gnd, speakerModel.cp);
    c.setInitialGuess (t1, railNominal);
    c.setInitialGuess (t2, railNominal);
    c.setInitialGuess (a1, railNominal);
    c.setInitialGuess (a2, railNominal);

    if (isCarmen)
    {
        c.addResistor (ch.speaker, nfbJoint, 3.3e6);                   // R10 3M3 (the drawing's alternate is 2M2)
        c.addCapacitor (ch.powerPlateA, ch.powerPlateB, 0.0022e-6);    // C18 .0022: keeps the NFB loop's top end stable
    }

    if (model == Model::astroverb16)
    {
        const auto nfb = c.addNode();
        c.addCapacitor (ch.speaker, nfb, 0.1e-6);
        c.addResistor (nfb, ch.piTail, 47.0e3);
        ch.rPresence = c.addResistor (nfb, gnd, 25.0e3);
    }
}

void EL84BoutiqueAmpCore::buildChannel (Channel& ch)
{
    buildSupply (ch);
    buildPreamp (ch, ch.pre, false);
    if (hasSecondPreamp())
        buildPreamp (ch, ch.pre2, true);
    if (! reducedOrder)
        buildPower (ch);
}

void EL84BoutiqueAmpCore::updatePots (Channel& ch)
{
    const auto setPot = [] (NodalCircuit& c, int top, int bottom, double max, double value)
    {
        if (top >= 0) c.setResistance (top, juce::jmax (1.0, max * (1.0 - juce::jlimit (0.0, 1.0, value))));
        if (bottom >= 0) c.setResistance (bottom, juce::jmax (1.0, max * juce::jlimit (0.0, 1.0, value)));
    };

    const double power = smoothedParam (ch, 0, powerParam, 1.0);
    const double bias = smoothedParam (ch, 1, biasParam, 0.5);
    const double feel = smoothedParam (ch, 2, feelParam, 1.0);
    ch.pre.setSource (ch.preRail, ch.preRailFiltered > 0.0 ? ch.preRailFiltered : railNominal * 0.725);
    if (ch.preRailA >= 0)
        ch.pre.setSource (ch.preRailA, (ch.preRailFiltered > 0.0 ? ch.preRailFiltered : railNominal * 0.725) * 272.0 / 258.0);

    if (model == Model::matchlessHC30)
    {
        // Ch1 VOL is the real 500kA pot; Ch2 VOL is 1MA (factory schematic).
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 500.0e3, pots::audio (smoothedParam (ch, 3, volume1Param, 0.4)));
        setPot (ch.pre2, ch.preVolume2Top, ch.preVolume2Bottom, 1.0e6, pots::audio (smoothedParam (ch, 4, volume2Param, 0.4)));
        const double bass = smoothedParam (ch, 5, bass1Param, 0.5), treble = smoothedParam (ch, 6, treble1Param, 0.5);
        setPot (ch.pre, ch.rTrebleTop, ch.rTrebleBottom, 1.0e6, pots::audio (treble)); // TREBLE 1MA (DC-30 trace)
        setPot (ch.pre, ch.rBassTop, ch.rBassBottom, 1.0e6, pots::audio (bass));
        if (! reducedOrder)
            ch.power.setResistance (ch.rCut, boundedPot (1.0 - smoothedParam (ch, 7, cutParam, 0.5), 250.0e3));
        const int tonePosition = juce::jlimit (0, 5, juce::roundToInt ((float) param (tone2Param, 5.0)));
        if (tonePosition != ch.lastTonePosition)
        {
            ch.lastTonePosition = tonePosition;
            // Rotary TONE caps from the factory schematic (position 5 is the
            // factory-added 180pF on top of the .01uF).
            // DC-30 trace: 56p, 360p, .0012, .0022, .0047 and .01 + 180p (factory-added) series coupling caps, thinnest
            // first (the trace lists the 360 above the 56p; ordered by value so the knob turns one way).
            constexpr double toneCaps[6] { 56.0e-12, 360.0e-12, 1.2e-9, 2.2e-9, 4.7e-9, 10.18e-9 };
            ch.pre2.setCapacitance (ch.toneCap, toneCaps[tonePosition]);
        }
    }
    else if (model == Model::hotCat30)
    {
        const double treble = smoothedParam (ch, 9, trebleParam, 0.5);
        const double bass = smoothedParam (ch, 10, bassParam, 0.5);
        // GAIN: 250k log pot as the shunt leg of the 470k divider after V1a
        // (0 = node grounded = silent, 1 = full 250k leg = max signal).
        ch.pre.setResistance (ch.rGainShunt, boundedPot (pots::audio (smoothedParam (ch, 8, gainParam, 0.5)), 250.0e3));
        // EDGE: 1M log series rheostat between V2b and V2a.
        ch.pre.setResistance (ch.rEdge, boundedPot (1.0 - smoothedParam (ch, 13, edgeParam, 1.0), 1.0e6));
        // LEVEL: 1M linear interstage divider before V3b.
        setPot (ch.pre, ch.preVolume2Top, ch.preVolume2Bottom, 1.0e6, smoothedParam (ch, 11, levelParam, 0.7));
        // Clean channel VOLUME: 1M log.
        setPot (ch.pre2, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, pots::audio (smoothedParam (ch, 12, volume1Param, 0.5)));
        setPot (ch.pre, ch.rTrebleTop, ch.rTrebleBottom, 1.0e6, treble);
        setPot (ch.pre, ch.rBassTop, ch.rBassBottom, 1.5e6, bass);
        if (! reducedOrder)
            ch.power.setResistance (ch.rCut, boundedPot (smoothedParam (ch, 7, brillianceParam, 0.7), 1.0e6));
    }
    else if (model == Model::carmenGhia)
    {
        // Volume: 1M audio taper.
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, pots::audio (smoothedParam (ch, 3, volume1Param, 0.4)));
        // TONE P2 500k A as a rheostat to C17: up = more resistance = less of the treble shunt.
        const double tone = smoothedParam (ch, 12, toneParam, 0.5);
        ch.pre.setResistance (ch.rTonePot, juce::jmax (100.0, 500.0e3 * pots::audio (tone)));
    }
    else
    {
        const double treble = smoothedParam (ch, 9, trebleParam, 0.5);
        const double bass = smoothedParam (ch, 10, bassParam, 0.5);
        // Preamp and Volume: audio taper like the other gain/volume controls in the project.
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, pots::audio (smoothedParam (ch, 13, preampParam, 0.5)));
        setPot (ch.pre, ch.preVolume2Top, ch.preVolume2Bottom, 1.0e6, pots::audio (smoothedParam (ch, 3, volume1Param, 0.5)));
        setPot (ch.pre, ch.rTrebleTop, ch.rTrebleBottom, 250.0e3, treble); // TREBLE 250k (factory schematic)
        setPot (ch.pre, ch.rBassTop, ch.rBassBottom, 1.0e6, bass);
        if (ch.rMid >= 0) ch.pre.setResistance (ch.rMid, boundedPot (1.0 - smoothedParam (ch, 11, midParam, 0.5), 25.0e3));
        if (! reducedOrder)
            ch.power.setResistance (ch.rPresence, boundedPot (1.0 - smoothedParam (ch, 14, presenceParam, 0.5), 25.0e3));
    }

    if (! reducedOrder)
    {
        {
            const double cathodeNominal = model == Model::matchlessHC30 ? 68.0
                                        : model == Model::hotCat30 ? 135.0
                                        : model == Model::carmenGhia ? 150.0 : 130.0;
            ch.power.setResistance (ch.rCathodeBias, cathodeNominal * (0.7 + 0.6 * bias));
        }
        ch.supply.setResistance (ch.rectifierResistance,
            (model == Model::carmenGhia ? 400.0 : 130.0) * (0.05 + 0.95 * feel));
        if (model == Model::carmenGhia)
        {
            // Ganged 250K post-PI master: audio taper, like a real dual-gang PPIMV pot.
            const double master = pots::audio (smoothedParam (ch, 15, masterParam, 1.0));
            ch.power.setResistance (ch.rMasterTopA, 2.7e3 + boundedPot (1.0 - master, 250.0e3)); // + R30 / R31 2700
            ch.power.setResistance (ch.rMasterTopB, 2.7e3 + boundedPot (1.0 - master, 250.0e3));
        }
        const auto speakerIndex = juce::jlimit (0, 2, juce::roundToInt ((float) param (speakerParam, 2.0)));
        if (speakerIndex != ch.lastSpeakerIndex)
        {
            ch.lastSpeakerIndex = speakerIndex;
            const auto load = tubeamp::speakerModel (speakerNominal[speakerIndex]);
            ch.power.setResistance (ch.rSpeakerRe, load.re);
            ch.power.setResistance (ch.rSpeakerRp, load.rp);
            ch.power.setResistance (ch.rSpeakerEddy, speakerEddyLoss * speakerNominal[speakerIndex] / speakerNominal[2]);
            ch.power.setCapacitance (ch.capSpeakerCp, load.cp);
            // allocating setter -- only on an actual speaker change, never per control tick
            ch.power.setInductorInverse (ch.grpSpeakerLe, { 1.0 / load.le });
            ch.power.setInductorInverse (ch.grpSpeakerLp, { 1.0 / load.lp });
        }
    }
    else
    {
        // reducedOrder: the page-2 controls that live only in the power block are folded
        // into the behavioural stage instead -- master attenuates the drive, bias shifts
        // the knee's operating point, tube feel scales the sag depth, and cut/presence
        // run as cheap one-poles on the output.
        ch.bmDriveScale = pots::audio (smoothedParam (ch, 15, masterParam, 1.0));
        ch.bmBias = bias;
        ch.bmFeel = feel;
        // Hot Cat's page-2 control is Brilliance (more = brighter = less cut);
        // map it to the same one-pole as Cut with the direction flipped.
        const double cutControl = model == Model::hotCat30
                                ? 1.0 - smoothedParam (ch, 7, brillianceParam, 0.7)
                                : smoothedParam (ch, 7, cutParam, 0.5);
        const double cutHz = 20000.0 * std::pow (0.15, cutControl);
        ch.bmCutAlpha = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutHz / juce::jmax (1.0, sampleRate));
        ch.bmShelf = (smoothedParam (ch, 14, presenceParam, 0.5) - 0.5) * 4.0;
        ch.bmPresAlpha = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 3500.0 / juce::jmax (1.0, sampleRate));
    }
    (void) power;
    ch.smoothedParamsNeedSnap = false;
}

void EL84BoutiqueAmpCore::updateSupply (Channel& ch)
{
    if (! reducedOrder)
    {
        double ipA = 0.0, isA = 0.0, ipB = 0.0, isB = 0.0;
        ch.power.pentodeCurrents (ch.penA, ipA, isA);
        ch.power.pentodeCurrents (ch.penB, ipB, isB);
        const double current = (ipA + ipB + isA + isB);
        ch.supply.setCurrentSource (ch.supplyCurrent, -juce::jlimit (0.0, 0.5, current));
    }
    ch.supply.solveSample();
    const double rail = juce::jlimit (0.0, 500.0, ch.supply.voltage (ch.supplyB));
    if (! reducedOrder)
        ch.power.setSource (ch.powerRail, rail);
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double tau = 22.0e3 * 16.0e-6;
    ch.preRailFiltered += (1.0 - std::exp (-dt / tau)) * (rail * 0.725 - ch.preRailFiltered);
    ch.pre.setSource (ch.preRail, ch.preRailFiltered);
    if (ch.preRailA >= 0)
        ch.pre.setSource (ch.preRailA, ch.preRailFiltered * 272.0 / 258.0);
    if (hasSecondPreamp())
        ch.pre2.setSource (ch.preRail2, ch.preRailFiltered);
}

void EL84BoutiqueAmpCore::trackEf86Screen (Channel& ch, double dt) const noexcept
{
    // Screen = rail - 2M2 * Ig2 through the 0.1 uF bypass (tau 0.22 s); dt < 0: jump to the static value (prepare()).
    double ip = 0.0, ig2 = 0.0;
    ch.pre2.pentodeCurrents (ch.ef86Pen, ip, ig2);
    const double rail = ch.preRailFiltered > 0.0 ? ch.preRailFiltered : railNominal * 0.725;
    const double target = juce::jlimit (5.0, rail, rail - 2.2e6 * juce::jmax (0.0, ig2));
    ch.ef86Screen = dt < 0.0 ? target : ch.ef86Screen + (1.0 - std::exp (-dt / (2.2e6 * 0.1e-6))) * (target - ch.ef86Screen);
    ch.pre2.setPentodeScreen (ch.ef86Pen, ch.ef86Screen);
}

double EL84BoutiqueAmpCore::behavioralPowerStage (Channel& ch, double drive) const noexcept
{
    const double attack = 1.0 - std::exp (-1.0 / (0.008 * juce::jmax (1.0, sampleRate)));
    const double release = 1.0 - std::exp (-1.0 / (0.045 * juce::jmax (1.0, sampleRate)));
    const double magnitude = std::abs (drive);
    ch.envelope += (magnitude > ch.envelope ? attack : release) * (magnitude - ch.envelope);
    // matchless re-fit 2026-10-11 (was 14.0): a softer knee keeps the same small-signal gain
    // (level*gain) as the low-drive calibration while raising the saturated plateau the real
    // stage reaches at hot drive -- the old steep knee compressed too early and undershot there.
    // carmenGhia 10 -> 0.35 and astroverb16 18 -> 1.5 (2026-11-01): fitted on level AND distortion against the
    // netlist at Volume 0.2 / 0.5 / 0.8. The old values saturated the stage at any volume (38 % THD at Volume 0.2,
    // where the netlist makes 2-14 %): the "too much gain" the user heard.
    const double gain = model == Model::carmenGhia ? 0.35 : model == Model::astroverb16 ? 1.5 : model == Model::hotCat30 ? 0.9 :
                        model == Model::matchlessHC30 ? 2.0 : 14.0;
    // level re-fit 2026-10-11: matchless/hotCat defaults now restore the pre-audio-taper wiper
    // fractions, which moved the stage's calibration point; scaled so the reduced path re-tracks
    // the reference at the new defaults (was 1.17 / 14.0).
    // matchless 2.02 -> 5.2 (2026-10-24), 6.2 after the DC-30 trace rebuild (2026-10-25): 2.02 had been fitted against a test that ran the reference at Volume 2 = 0.6
    // and the reduced path at its default; at equal knobs it sat 7-10 dB under the netlist.
    const double level = model == Model::matchlessHC30 ? 6.2 :
                         model == Model::hotCat30 ? 40.0 :
                         model == Model::carmenGhia ? 33.0 : 28.3;
    // The Carmen Ghia's netlist compresses gradually (its NFB): a soft knee x / (1 + |x|^1.4)^(1/1.4), not tanh.
    const double kneeN = model == Model::carmenGhia ? 1.4 : 0.0;
    const double asym = reducedOrder ? 0.35 * (ch.bmBias - 0.5) : 0.0;
    const double sag = reducedOrder ? 0.03 * ch.bmFeel : 0.025;
    const auto curve = [kneeN] (double x)
    {
        return kneeN > 0.0 ? x / std::pow (1.0 + std::pow (std::abs (x), kneeN), 1.0 / kneeN) : std::tanh (x);
    };
    return level * (curve (drive * gain + asym) - curve (asym)) * (model == Model::carmenGhia ? 0.65 : 0.8)
        * (1.0 - sag * juce::jlimit (0.0, 1.0, ch.envelope));
}

void EL84BoutiqueAmpCore::prepare (double newSampleRate, int, int)
{
    if (newSampleRate <= 0.0 || (sampleRate == newSampleRate && dcOk))
        return;
    sampleRate = newSampleRate;
    dcOk = true;
    sampleCount = failureCount = 0;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto bmVoicing = tubeamp::voicings::classAChime;
        bmVoicing.asym = 0.06; // half-strength: the knee already shifts with the Bias control
        // (Matchless used to override cabLpHz 900 / cabAirHz 3000 here, "own netlist much darker": after #111's Cut / CH2
        // rework the netlist is not, and the override put the reduced treble 20 dB under it. Removed 2026-10-24: worst
        // band vs the netlist 13.2 -> 3.2 dB.)
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
        // Supply -> rails -> preamp / power -> the tubes' measured current -> supply again, until it settles: the supply
        // used to be solved once at an assumed 0.15 A and the rails then walked to their real point after start-up --
        // a thump that scaled with the Volume (it showed on the Carmen Ghia once its preamp was rebuilt).
        bool ok = true;
        for (int railPass = 0; railPass < 4; ++railPass)
        {
            ok = ch.supply.prepare (sampleRate / (double) supplyInterval);
            const double rail = juce::jlimit (0.0, 500.0, ch.supply.voltage (ch.supplyB));
            ch.preRailFiltered = rail * 0.725;
            ch.pre.setSource (ch.preRail, ch.preRailFiltered);
            if (ch.preRailA >= 0)
                ch.pre.setSource (ch.preRailA, ch.preRailFiltered * 272.0 / 258.0);
            if (hasSecondPreamp())
                ch.pre2.setSource (ch.preRail2, ch.preRailFiltered);
            if (! reducedOrder)
                ch.power.setSource (ch.powerRail, rail);
            ok = ch.pre.prepare (sampleRate) && ok;
            if (hasSecondPreamp())
                ok = ch.pre2.prepare (sampleRate) && ok;
            if (ch.ef86Pen >= 0)
                for (int pass = 0; pass < 6; ++pass) // the EF86 screen on its 2M2 dropper, to its own DC point
                {
                    ch.pre2.solveSample();
                    trackEf86Screen (ch, -1.0);
                    ok = ch.pre2.prepare (sampleRate) && ok;
                }
            if (! reducedOrder)
            {
                ok = ch.power.prepare (sampleRate) && ok;
                ch.power.solveSample(); // pentodeCurrents() reads the last solve
                double ipA = 0.0, isA = 0.0, ipB = 0.0, isB = 0.0;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.supply.setCurrentSource (ch.supplyCurrent, -juce::jlimit (0.0, 0.5, ipA + ipB + isA + isB));
                ok = ch.power.prepare (sampleRate) && ok; // back to the exact DC point after the probing solve
            }
        }
        dcOk = dcOk && ok;
        ch.pre.saveDynamicState (ch.preRest);
        if (hasSecondPreamp())
            ch.pre2.saveDynamicState (ch.pre2Rest);
        if (! reducedOrder)
            ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.preRailFiltered = ch.supply.voltage (ch.supplyB) * 0.725;
        ch.preDc = ch.pre.voltage (ch.preOut);
        if (hasSecondPreamp())
            ch.pre2Dc = ch.pre2.voltage (ch.preOut2);
        for (auto& s : ch.smoothedParams)
            s.reset (sampleRate, EffectProcessor::paramRampSeconds);
        ch.smoothedParamsNeedSnap = true;
        smoothedGainsNeedSnap = true;
        updatePots (ch);
    }
}

void EL84BoutiqueAmpCore::reset()
{
    if (sampleRate <= 0.0)
        return;
    const double currentRate = sampleRate;
    sampleRate = 0.0;
    shortcut.reset();
    prepare (currentRate, 0, 0);
}

void EL84BoutiqueAmpCore::process (juce::AudioBuffer<float>& buffer)
{
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);
    if (solveChannels == 0)
        return;
    // The panel Master is a level control like the other volume pots: same audio taper law
    // applied to the scalar gain it maps to.
    const double volumeTarget = model == Model::hotCat30 ? pots::audio (param (masterParam, 0.7)) :
        model == Model::matchlessHC30 ? pots::audio (param (masterParam, 0.7)) : 1.0;
    const double powerDriveTarget = std::pow (10.0, (param (powerParam, 1.0) - 1.0) * 18.0 / 20.0);
    const double outputTarget = std::pow (10.0, (param (outputParam, 0.5) - 0.5) * 48.0 / 20.0);

    // The solved-signal gains (master drive, power drive, output trim)
    // multiply audio per sample -- snap them on the first block after
    // prepare (preset load stays instant) then ramp afterwards.
    if (smoothedGainsNeedSnap)
    {
        smoothedVolumeGain.reset (sampleRate, EffectProcessor::paramRampSeconds);
        smoothedVolumeGain.setCurrentAndTargetValue ((float) volumeTarget);
        smoothedPowerDrive.reset (sampleRate, EffectProcessor::paramRampSeconds);
        smoothedPowerDrive.setCurrentAndTargetValue ((float) powerDriveTarget);
        smoothedOutputGain.reset (sampleRate, EffectProcessor::paramRampSeconds);
        smoothedOutputGain.setCurrentAndTargetValue ((float) outputTarget);
        smoothedGainsNeedSnap = false;
    }
    smoothedVolumeGain.setTargetValue ((float) volumeTarget);
    smoothedPowerDrive.setTargetValue ((float) powerDriveTarget);
    smoothedOutputGain.setTargetValue ((float) outputTarget);
    const int speakerIndex = juce::jlimit (0, 2, juce::roundToInt ((float) param (speakerParam, 2.0)));
    const bool useSecondChannel = hasSecondPreamp() && param (channelParam, 1.0) >= 0.5;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double volume = (double) smoothedVolumeGain.getNextValue();
        const double powerDrive = (double) smoothedPowerDrive.getNextValue();
        const double output = (double) smoothedOutputGain.getNextValue();

        if ((i & 15) == 0)
            for (auto& ch : channels)
            {
                updatePots (ch);
                if (ch.ef86Pen >= 0)
                    trackEf86Screen (ch, 16.0 / sampleRate);
            }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* samples = buffer.getWritePointer (chIdx);
            const double input = std::isfinite (samples[i]) ? tubeamp::inputLimit ((double) samples[i]) : 0.0;
            auto& pre = useSecondChannel ? ch.pre2 : ch.pre;
            pre.setSource (useSecondChannel ? ch.preInput2 : ch.preInput, input);
            const bool okPre = pre.solveSample();
            const double preOut = useSecondChannel ? pre.voltage (ch.preOut2) : pre.voltage (ch.preOut);
            bool ok = okPre;
            double speakerVolts = 0.0;
            if (reducedOrder)
            {
                auto& dc = useSecondChannel ? ch.pre2Dc : ch.preDc;
                const double dcAlpha = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 5.0 / juce::jmax (1.0, sampleRate));
                dc += dcAlpha * (preOut - dc);
                speakerVolts = behavioralPowerStage (ch, (preOut - dc) * powerDrive * ch.bmDriveScale);
                if (ch.bmCutAlpha < 0.999)
                {
                    ch.bmLp += ch.bmCutAlpha * (speakerVolts - ch.bmLp);
                    speakerVolts = ch.bmLp;
                }
                if (ch.bmShelf != 0.0)
                {
                    ch.bmLpPres += ch.bmPresAlpha * (speakerVolts - ch.bmLpPres);
                    speakerVolts = ch.bmLpPres + (1.0 + ch.bmShelf) * (speakerVolts - ch.bmLpPres);
                }
                speakerVolts = ch.bmTail.process (speakerVolts);
            }
            else
            {
                const double master = model == Model::carmenGhia ? 1.0 : volume;
                // Channel 2 injects at the PI's second grid (the interactive
                // two-channel wiring); channel 1 drives the first grid.
                ch.power.setSource (ch.powerCf, useSecondChannel ? 0.0 : preOut * master * powerDrive);
                if (ch.powerInput2 >= 0)
                    ch.power.setSource (ch.powerInput2, useSecondChannel ? preOut * master * powerDrive : 0.0);
                const bool okPower = ch.power.solveSample();
                ok = ok && okPower;
                speakerVolts = ch.power.voltage (ch.speaker);
                if (++ch.supplyCounter >= supplyInterval)
                {
                    ch.supplyCounter = 0;
                    updateSupply (ch);
                }
            }
            ch.outputState = speakerVolts;

            if (! std::isfinite (speakerVolts) || std::abs (speakerVolts) > 120.0)
            {
                ++ch.failures;
                speakerVolts = 0.0;
                ok = false;
            }
            const double out = speakerVolts * outputScale * output * std::pow (speakerNominal[2] / speakerNominal[speakerIndex], -0.8);
            samples[i] = (float) out;
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

double EL84BoutiqueAmpCore::debugVoltage (Probe probe) const noexcept
{
    const auto& ch = channels[0];
    switch (probe)
    {
        case Probe::preampPlate: return ch.pre.voltage (ch.prePlate);
        case Probe::piPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.piPlateA);
        case Probe::piPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.piPlateB);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.powerPlateA);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.powerPlateB);
        case Probe::cathodeBias: return reducedOrder ? 0.0 : ch.power.voltage (ch.cathodeBias);
        case Probe::speaker: return reducedOrder ? ch.outputState : ch.power.voltage (ch.speaker);
        case Probe::screen: return model == Model::hotCat30 ? 367.0
                                   : model == Model::carmenGhia || model == Model::astroverb16 ? 290.0 : 300.0;
    }
    return 0.0;
}

double EL84BoutiqueAmpCore::railPlates() const noexcept
{
    return channels[0].supply.voltage (channels[0].supplyB);
}

double EL84BoutiqueAmpCore::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    const auto& ch = channels[0];
    double ipA = 0.0, isA = 0.0, ipB = 0.0, isB = 0.0;
    ch.power.pentodeCurrents (ch.penA, ipA, isA);
    ch.power.pentodeCurrents (ch.penB, ipB, isB);
    return ipA + ipB + isA + isB;
}

void EL84BoutiqueAmpCore::drawIcon (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, bounds, svg.get());
}

} // namespace openguitarmultifx
