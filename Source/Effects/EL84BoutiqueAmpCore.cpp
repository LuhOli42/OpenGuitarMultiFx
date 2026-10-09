#include "EL84BoutiqueAmpCore.h"

#include "IconKit.h"
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
        volume1Param = add ("hc30_volume1", "Volume 1", 0.4f);
        bass1Param = add ("hc30_bass1", "Bass 1", 0.5f);
        treble1Param = add ("hc30_treble1", "Treble 1", 0.5f);
        volume2Param = add ("hc30_volume2", "Volume 2", 0.4f);
        auto tone = std::make_unique<juce::AudioParameterFloat> (
            "hc30_tone2", "Tone 2", juce::NormalisableRange<float> (0.0f, 5.0f, 1.0f), 0.0f);
        tone2Param = tone.get();
        group->addChild (std::move (tone));
        cutParam = add ("hc30_cut", "Cut", 0.5f);
        masterParam = add ("hc30_master", "Master", 0.7f);
    }
    else if (model == Model::hotCat30)
    {
        gainParam = add ("hotcat30_gain", "Gain", 0.5f);
        bassParam = add ("hotcat30_bass", "Bass", 0.5f);
        midParam = add ("hotcat30_mid", "Middle", 0.5f);
        trebleParam = add ("hotcat30_treble", "Treble", 0.5f);
        masterParam = add ("hotcat30_master", "Master", 0.7f);
    }
    else if (model == Model::carmenGhia)
    {
        volume1Param = add ("cghia_volume", "Volume", 0.4f);
        toneParam = add ("cghia_tone", "Tone", 0.5f);
        masterParam = add ("cghia_master", "Master", 1.0f);
    }
    else
    {
        preampParam = add ("astro16_preamp", "Preamp", 0.5f);
        bassParam = add ("astro16_bass", "Bass", 0.5f);
        midParam = add ("astro16_mid", "Middle", 0.5f);
        trebleParam = add ("astro16_treble", "Treble", 0.5f);
        volume1Param = add ("astro16_volume", "Volume", 0.5f);
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

    const auto addEF86 = [&] (NodalCircuit::Node input)
    {
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode(), out = c.addNode();
        c.addCapacitor (input, grid, 0.02e-6);
        c.addResistor (grid, gnd, 1.0e6);
        c.addPentode (plate, grid, cathode, tubes.ef86, 200.0);
        c.addResistor (rail, plate, 220.0e3);
        c.addResistor (cathode, gnd, 2.2e3);
        c.addCapacitor (cathode, gnd, 25.0e-6);
        c.addCapacitor (plate, out, 0.02e-6);
        c.addResistor (out, gnd, 1.0e6);
        c.setInitialGuess (plate, 220.0);
        c.setInitialGuess (cathode, 1.8);
        ch.prePlate = plate;
        return out;
    };

    if (model == Model::matchlessHC30 && ! channelTwo)
    {
        const auto grid = c.addNode(), cathode = c.addNode(), plate = c.addNode(), coupling = c.addNode();
        c.addResistor (in, grid, 68.0e3);
        c.addResistor (grid, gnd, 1.0e6);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addTriode (plate, grid, cathode, tubes.triode);
        c.addResistor (rail, plate, 100.0e3);
        c.addResistor (cathode, gnd, 820.0);
        c.addCapacitor (cathode, gnd, 25.0e-6);
        c.setInitialGuess (plate, 200.0);
        c.setInitialGuess (cathode, 1.0);

        const auto a = c.addNode(), b = c.addNode(), d = c.addNode(), e = c.addNode(), f = c.addNode();
        const auto toneOut = c.addNode();
        c.addCapacitor (plate, a, 220.0e-12);
        c.addResistor (a, b, 100.0e3);
        c.addCapacitor (b, d, 0.022e-6);
        ch.rTrebleTop = c.addResistor (a, toneOut, 1.0e6);
        ch.rTrebleBottom = c.addResistor (toneOut, d, 1.0e6);
        ch.rBassTop = c.addResistor (d, e, 1.0e6);
        ch.rBassBottom = c.addResistor (e, f, 1.0e6);
        c.addResistor (f, gnd, 100.0e3);
        c.addCapacitor (b, toneOut, 0.022e-6);
        ch.prePlate = plate;
        ch.toneOut = toneOut;
        ch.preVolumeTop = c.addResistor (toneOut, coupling, 1.0e6);
        ch.preVolumeBottom = c.addResistor (coupling, gnd, 1.0e6);
        c.addCapacitor (plate, coupling, 0.022e-6);
        const auto second = addTriode (coupling, 100.0e3, 1.5e3, 0.0, 0.022e-6, false);
        ch.preOut = second.second;
        return;
    }

    if (model == Model::matchlessHC30)
    {
        auto output = addEF86 (in);
        const auto volumeOut = c.addNode();
        ch.preVolume2Top = c.addResistor (output, volumeOut, 1.0e6);
        ch.preVolume2Bottom = c.addResistor (volumeOut, gnd, 1.0e6);
        const auto toneNode = c.addNode(), toneOut = c.addNode();
        c.addResistor (volumeOut, toneNode, 100.0e3);
        ch.toneCap = c.addCapacitor (toneNode, gnd, 1.0e-12);
        c.addResistor (toneNode, toneOut, 47.0e3);
        c.addResistor (toneOut, gnd, 1.0e6);
        ch.preOut2 = toneOut;
        return;
    }

    if (model == Model::hotCat30)
    {
        auto output = addEF86 (in);
        const auto gainOut = c.addNode();
        ch.preVolumeTop = c.addResistor (output, gainOut, 1.0e6);
        ch.preVolumeBottom = c.addResistor (gainOut, gnd, 1.0e6);
        auto first = addTriode (gainOut, 100.0e3, 1.5e3, 1.0e-6, 0.022e-6, true);
        auto second = addTriode (first.second, 100.0e3, 2.7e3, 0.0, 0.022e-6, false);
        const auto follower = c.addNode();
        c.addFollower (second.first, follower, 1.5);
        output = follower;
        ch.prePlate = first.first;
        ch.preOut = output;
    }
    else if (model == Model::carmenGhia)
    {
        auto first = addTriode (in, 100.0e3, 1.5e3, 25.0e-6, 0.022e-6, true);
        const auto volumeOut = c.addNode();
        ch.preVolumeTop = c.addResistor (first.second, volumeOut, 1.0e6);
        ch.preVolumeBottom = c.addResistor (volumeOut, gnd, 1.0e6);
        auto second = addTriode (volumeOut, 100.0e3, 1.5e3, 0.0, 0.022e-6, false);
        const auto tone = c.addNode();
        c.addResistor (second.second, tone, 100.0e3);
        ch.toneCap = c.addCapacitor (tone, gnd, 4.7e-9);
        c.addResistor (tone, gnd, 1.0e6);
        ch.prePlate = first.first;
        ch.preOut = tone;
    }
    else
    {
        auto first = addTriode (in, 220.0e3, 1.8e3, 1.0e-6, 0.022e-6, true);
        const auto preampOut = c.addNode();
        ch.preVolumeTop = c.addResistor (first.second, preampOut, 1.0e6);
        ch.preVolumeBottom = c.addResistor (preampOut, gnd, 1.0e6);
        auto second = addTriode (preampOut, 100.0e3, 2.7e3, 0.0, 0.022e-6, false);
        auto third = addTriode (second.second, 100.0e3, 1.5e3, 1.0e-6, 0.022e-6, true);
        const auto follower = c.addNode();
        c.addFollower (third.first, follower, 1.5);
        ch.prePlate = first.first;
        ch.preOut = follower;
    }

    const bool hasTmb = model == Model::hotCat30 || model == Model::astroverb16 || model == Model::matchlessHC30;
    if (hasTmb)
    {
        const auto inStack = ch.preOut;
        const auto a = c.addNode(), b = c.addNode(), d = c.addNode(), e = c.addNode(), f = c.addNode();
        c.addCapacitor (inStack, a, model == Model::matchlessHC30 ? 220.0e-12 : 470.0e-12);
        c.addResistor (inStack, b, model == Model::matchlessHC30 ? 100.0e3 : 33.0e3);
        c.addCapacitor (b, d, 0.022e-6);
        c.addCapacitor (b, e, 0.022e-6);
        ch.toneOut = c.addNode();
        ch.rTrebleTop = c.addResistor (a, ch.toneOut, 250.0e3);
        ch.rTrebleBottom = c.addResistor (ch.toneOut, d, 250.0e3);
        ch.rBassTop = c.addResistor (d, e, 1.0e6);
        ch.rBassBottom = c.addResistor (e, f, 1.0e6);
        c.addResistor (f, gnd, 10.0e3);
        ch.rMid = c.addResistor (ch.toneOut, gnd, 25.0e3);
        ch.preOut = ch.toneOut;
    }

    if (model == Model::astroverb16)
    {
        const auto volumeOut = c.addNode();
        ch.preVolume2Top = c.addResistor (ch.preOut, volumeOut, 1.0e6);
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
    c.addCapacitor (input, ch.piGridA, 0.02e-6);
    c.addResistor (ch.piGridA, gnd, 1.0e6);
    c.addResistor (ch.piGridB, gnd, 1.0e6);
    auto pi = tubes.triode;
    pi.kg1 *= 6.0;
    c.addTriode (ch.piPlateA, ch.piGridA, tail, pi);
    c.addTriode (ch.piPlateB, ch.piGridB, tail, pi);
    c.addCapacitor (ch.piGridA, ch.piPlateA, 1.7e-12);
    c.addCapacitor (ch.piGridB, ch.piPlateB, 1.7e-12);
    c.addResistor (rail, ch.piPlateA, 100.0e3);
    c.addResistor (rail, ch.piPlateB, 100.0e3);
    c.addResistor (tail, gnd, 1.2e3);
    c.setInitialGuess (ch.piPlateA, 260.0);
    c.setInitialGuess (ch.piPlateB, 260.0);
    c.setInitialGuess (tail, 1.0);
    if (model == Model::matchlessHC30)
    {
        const auto cutNode = c.addNode();
        c.addCapacitor (ch.piPlateA, cutNode, 4.7e-9);
        ch.rCut = c.addResistor (cutNode, ch.piPlateB, 100.0e3);
    }

    const auto gridCouplingA = c.addNode(), gridCouplingB = c.addNode();
    const auto gridA = c.addNode(), gridB = c.addNode();
    ch.powerPlateA = c.addNode();
    ch.powerPlateB = c.addNode();
    c.addCapacitor (ch.piPlateA, gridCouplingA, 0.047e-6);
    c.addCapacitor (ch.piPlateB, gridCouplingB, 0.047e-6);
    c.addResistor (gridCouplingA, gnd, 220.0e3);
    c.addResistor (gridCouplingB, gnd, 220.0e3);
    ch.rMasterTopA = c.addResistor (gridCouplingA, gridA, 1.0);
    ch.rMasterTopB = c.addResistor (gridCouplingB, gridB, 1.0);
    c.addResistor (gridA, gnd, 220.0e3);
    c.addResistor (gridB, gnd, 220.0e3);
    ch.cathodeBias = c.addNode();
    const bool fourTubes = model == Model::matchlessHC30 || model == Model::hotCat30;
    const double cathodeR = fourTubes ? 50.0 : 130.0;
    ch.rCathodeBias = c.addResistor (ch.cathodeBias, gnd, cathodeR);
    c.addCapacitor (ch.cathodeBias, gnd, 250.0e-6);
    const auto& powerTube = fourTubes ? tubes.el84Pair : tubes.el84;
    const double screen = fourTubes ? 300.0 : 290.0;
    ch.penA = c.addPentode (ch.powerPlateA, gridA, ch.cathodeBias, powerTube, screen);
    ch.penB = c.addPentode (ch.powerPlateB, gridB, ch.cathodeBias, powerTube, screen);
    c.setInitialGuess (ch.powerPlateA, 350.0);
    c.setInitialGuess (ch.powerPlateB, 345.0);
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
    const double turns = fourTubes ? halfToSecondaryTurns : 11.18;
    const double lh = fourTubes ? primaryHalfInductance : 5.0;
    const double ls = lh / (turns * turns);
    const double m12 = -couplingHalves * lh;
    const double mps = couplingSecondary * std::sqrt (lh * ls);
    c.addCoupledInductors ({ { a1, t1 }, { a2, t2 }, { sw, gnd } },
                           { lh, m12, -mps,
                             m12, lh, mps,
                             -mps, mps, ls });
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
    if (model == Model::matchlessHC30)
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

    const double power = param (powerParam, 1.0);
    const double bias = param (biasParam, 0.5);
    const double feel = param (feelParam, 1.0);
    ch.pre.setSource (ch.preRail, ch.preRailFiltered > 0.0 ? ch.preRailFiltered : railNominal * 0.725);

    if (model == Model::matchlessHC30)
    {
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, param (volume1Param, 0.4));
        setPot (ch.pre2, ch.preVolume2Top, ch.preVolume2Bottom, 1.0e6, param (volume2Param, 0.4));
        const double bass = param (bass1Param, 0.5), treble = param (treble1Param, 0.5);
        ch.pre.setResistance (ch.rTrebleTop, boundedPot (1.0 - treble, 1.0e6));
        ch.pre.setResistance (ch.rTrebleBottom, boundedPot (treble, 1.0e6));
        ch.pre.setResistance (ch.rBassTop, boundedPot (1.0 - bass, 1.0e6));
        ch.pre.setResistance (ch.rBassBottom, boundedPot (bass, 1.0e6));
        if (! reducedOrder)
            ch.power.setResistance (ch.rCut, boundedPot (1.0 - param (cutParam, 0.5), 100.0e3));
        const int tonePosition = juce::jlimit (0, 5, juce::roundToInt ((float) param (tone2Param, 0.0)));
        constexpr double toneCaps[6] { 1.0e-12, 470.0e-12, 1.0e-9, 2.2e-9, 4.7e-9, 10.0e-9 };
        ch.pre2.setCapacitance (ch.toneCap, toneCaps[tonePosition]);
    }
    else if (model == Model::hotCat30)
    {
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, param (gainParam, 0.5));
        ch.pre.setResistance (ch.rTrebleTop, boundedPot (1.0 - param (trebleParam, 0.5), 250.0e3));
        ch.pre.setResistance (ch.rTrebleBottom, boundedPot (param (trebleParam, 0.5), 250.0e3));
        ch.pre.setResistance (ch.rBassTop, boundedPot (1.0 - param (bassParam, 0.5), 1.0e6));
        ch.pre.setResistance (ch.rBassBottom, boundedPot (param (bassParam, 0.5), 1.0e6));
        if (ch.rMid >= 0) ch.pre.setResistance (ch.rMid, boundedPot (1.0 - param (midParam, 0.5), 25.0e3));
    }
    else if (model == Model::carmenGhia)
    {
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, param (volume1Param, 0.4));
        const double tone = param (toneParam, 0.5);
        ch.pre.setCapacitance (ch.toneCap, 4.7e-9 * (0.2 + 1.6 * (1.0 - tone)));
    }
    else
    {
        setPot (ch.pre, ch.preVolumeTop, ch.preVolumeBottom, 1.0e6, param (preampParam, 0.5));
        setPot (ch.pre, ch.preVolume2Top, ch.preVolume2Bottom, 1.0e6, param (volume1Param, 0.5));
        ch.pre.setResistance (ch.rTrebleTop, boundedPot (1.0 - param (trebleParam, 0.5), 250.0e3));
        ch.pre.setResistance (ch.rTrebleBottom, boundedPot (param (trebleParam, 0.5), 250.0e3));
        ch.pre.setResistance (ch.rBassTop, boundedPot (1.0 - param (bassParam, 0.5), 1.0e6));
        ch.pre.setResistance (ch.rBassBottom, boundedPot (param (bassParam, 0.5), 1.0e6));
        if (ch.rMid >= 0) ch.pre.setResistance (ch.rMid, boundedPot (1.0 - param (midParam, 0.5), 25.0e3));
        if (! reducedOrder)
            ch.power.setResistance (ch.rPresence, boundedPot (1.0 - param (presenceParam, 0.5), 25.0e3));
    }

    if (! reducedOrder)
    {
        const double cathodeNominal = model == Model::matchlessHC30 || model == Model::hotCat30 ? 50.0 : 130.0;
        ch.power.setResistance (ch.rCathodeBias, cathodeNominal * (0.7 + 0.6 * bias));
        ch.supply.setResistance (ch.rectifierResistance,
            (model == Model::carmenGhia ? 400.0 : 130.0) * (0.05 + 0.95 * feel));
        if (model == Model::carmenGhia)
        {
            const double master = param (masterParam, 1.0);
            ch.power.setResistance (ch.rMasterTopA, boundedPot (1.0 - master, 250.0e3));
            ch.power.setResistance (ch.rMasterTopB, boundedPot (1.0 - master, 250.0e3));
        }
        const auto speakerIndex = juce::jlimit (0, 2, juce::roundToInt ((float) param (speakerParam, 2.0)));
        const auto load = tubeamp::speakerModel (speakerNominal[speakerIndex]);
        ch.power.setResistance (ch.rSpeakerRe, load.re);
        ch.power.setResistance (ch.rSpeakerRp, load.rp);
        ch.power.setCapacitance (ch.capSpeakerCp, load.cp);
    }
    (void) power;
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
    if (model == Model::matchlessHC30)
        ch.pre2.setSource (ch.preRail2, ch.preRailFiltered);
}

double EL84BoutiqueAmpCore::behavioralPowerStage (Channel& ch, double drive) const noexcept
{
    const double attack = 1.0 - std::exp (-1.0 / (0.008 * juce::jmax (1.0, sampleRate)));
    const double release = 1.0 - std::exp (-1.0 / (0.045 * juce::jmax (1.0, sampleRate)));
    const double magnitude = std::abs (drive);
    ch.envelope += (magnitude > ch.envelope ? attack : release) * (magnitude - ch.envelope);
    const double gain = model == Model::carmenGhia ? 10.0 : model == Model::astroverb16 ? 18.0 : 14.0;
    const double level = model == Model::matchlessHC30 ? 0.97 :
                         model == Model::hotCat30 ? 9.5 :
                         model == Model::carmenGhia ? 22.4 : 21.4;
    return level * std::tanh (drive * gain) * (model == Model::carmenGhia ? 0.65 : 0.8)
        * (1.0 - 0.025 * juce::jlimit (0.0, 1.0, ch.envelope));
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
        bool ok = ch.supply.prepare (sampleRate / (double) supplyInterval);
        ok = ch.pre.prepare (sampleRate) && ok;
        if (model == Model::matchlessHC30)
            ok = ch.pre2.prepare (sampleRate) && ok;
        if (! reducedOrder)
            ok = ch.power.prepare (sampleRate) && ok;
        dcOk = dcOk && ok;
        ch.pre.saveDynamicState (ch.preRest);
        if (model == Model::matchlessHC30)
            ch.pre2.saveDynamicState (ch.pre2Rest);
        if (! reducedOrder)
        {
            ch.power.saveDynamicState (ch.powerRest);
            double ipA = 0.0, isA = 0.0, ipB = 0.0, isB = 0.0;
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
            ch.supply.setCurrentSource (ch.supplyCurrent, -(ipA + ipB + isA + isB));
        }
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.preRailFiltered = ch.supply.voltage (ch.supplyB) * 0.725;
        ch.preDc = ch.pre.voltage (ch.preOut);
        if (model == Model::matchlessHC30)
            ch.pre2Dc = ch.pre2.voltage (ch.preOut2);
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
    const double volume = model == Model::hotCat30 ? param (masterParam, 0.7) :
        model == Model::matchlessHC30 ? param (masterParam, 0.7) : 1.0;
    const double powerDrive = std::pow (10.0, (param (powerParam, 1.0) - 1.0) * 18.0 / 20.0);
    const double output = std::pow (10.0, (param (outputParam, 0.5) - 0.5) * 48.0 / 20.0);
    const int speakerIndex = juce::jlimit (0, 2, juce::roundToInt ((float) param (speakerParam, 2.0)));
    const bool useSecondChannel = model == Model::matchlessHC30 && param (channelParam, 1.0) >= 0.5;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        if ((i & 15) == 0)
            for (auto& ch : channels)
                updatePots (ch);

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
                speakerVolts = behavioralPowerStage (ch, (preOut - dc) * powerDrive);
            }
            else
            {
                const double master = model == Model::carmenGhia ? 1.0 : volume;
                ch.power.setSource (ch.powerCf, preOut * master * powerDrive);
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
        case Probe::screen: return model == Model::carmenGhia || model == Model::astroverb16 ? 290.0 : 300.0;
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
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, bounds, svg.get());
}

} // namespace openguitarmultifx
