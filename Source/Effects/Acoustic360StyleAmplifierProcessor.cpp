#include "Acoustic360StyleAmplifierProcessor.h"
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

    // ---- supply ----
    constexpr double railNominal = 25.0; // the 360's regulated +25 V preamp rail (schematic 1006A, block 17-7)

    // ---- transistors: small-signal NPN of the era (2N5088/2N3394 class). Standard Ebers-Moll
    // set used across this project's SS pedals.
    const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, 300.0, 4.0 };

    // Variamp (schematic 1006A): L1 1.5 H in series with the RANGE cap C15 2.2 / C16 .47 / C17 .1 / C18 .027 uF:
    // resonances at 87 / 190 / 411 / 790 Hz.
    constexpr double variampInductance = 1.5;
    constexpr int variampPositions = 4;
    constexpr double variampCapacitance[variampPositions] = { 2.2e-6, 0.47e-6, 0.1e-6, 0.027e-6 };

    constexpr double speakerNominal[2] = { 4.0, 8.0 };
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::bass810, &tubeamp::cabs::bass115, &tubeamp::cabs::tweed410 };
}

Acoustic360StyleAmplifierProcessor::Acoustic360StyleAmplifierProcessor()
{
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto bright = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_bright", "Bright", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto effect = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_effect", "Effect", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto variamp = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_variamp", "Variamp", juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) + 1);
        }));
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_output", "Output", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "acoustic360_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 1, juce::roundToInt (v))], 0) + " ohm";
        }));

    volumeParam = volume.get();
    brightParam = bright.get();
    bassParam = bass.get();
    trebleParam = treble.get();
    effectParam = effect.get();
    variampParam = variamp.get();
    outputParam = output.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "acoustic360", "Acoustic 360", "|", std::move (volume));
    group->addChild (std::move (bright));
    group->addChild (std::move (bass));
    group->addChild (std::move (treble));
    group->addChild (std::move (effect));
    group->addChild (std::move (variamp));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("acoustic360_page2", "Page 2", "|", std::move (output));
    page2->addChild (std::move (speaker));
    auto cab = tubeamp::makeCabParam ("acoustic360_cab", "1x18 W-bin", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void Acoustic360StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ preamp: the factory 360 schematic
    // (Acoustic Control Corp 1006A, rev 7/10/70; docs/circuits/Acoustic360.md). Six 2N3391-class NPNs on +25 V:
    // Q1 bootstrapped emitter follower -> Q2 shunt-feedback stage whose feedback ratio IS the Volume pot -> Q3 follower
    // -> Q4 -> passive Treble/Bass -> Q5 with the Variamp across its collector and emitter -> Q6 follower -> output.
    {
        auto& c = ch.pre;
        c.setIntegrationTheta (0.5);
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railNominal);
        ch.pSrcIn = c.addSource (in, 0.0);

        // Q1 (MPSA09): input R1 22K -> C2 1.0 -> base, R3 1M (100 pF across) to the rail, R4 4.7M to ground,
        // emitter R5 100K, out through C4 1.0.
        const auto r1 = c.addNode(), b1 = c.addNode(), e1 = c.addNode(), n = c.addNode();
        c.addResistor (in, r1, 22.0e3);
        c.addCapacitor (r1, b1, 1.0e-6);
        c.addResistor (vcc, b1, 1.0e6);
        c.addCapacitor (vcc, b1, 100.0e-12);
        c.addResistor (b1, gnd, 4.7e6);
        c.addBjt (vcc, b1, e1, false, npn);
        c.addResistor (e1, gnd, 100.0e3);
        c.addCapacitor (e1, n, 1.0e-6);
        c.addResistor (n, gnd, 470.0e3);                   // R6
        c.setInitialGuess (b1, 20.6);
        c.setInitialGuess (e1, 20.0);
        ch.pCol1 = e1;

        // VOLUME R7 50K: input end at n, wiper -> C6 1.0 -> Q2 base, far end -> R9 8.2K -> C7 1.0 -> Q2 collector.
        // BRIGHT (S1): C5 .01 + R8 2.2K from n to the wiper.
        const auto wv = c.addNode(), fv = c.addNode(), f2 = c.addNode(), br = c.addNode();
        const auto b2 = c.addNode(), c2 = c.addNode(), e2 = c.addNode();
        ch.rVolumeTop = c.addResistor (n, wv, 25.0e3);
        ch.rVolumeBot = c.addResistor (wv, fv, 25.0e3);
        c.addResistor (fv, f2, 8.2e3);
        c.addCapacitor (f2, c2, 1.0e-6);
        ch.capBright = c.addCapacitor (n, br, 1.0e-12);    // .01 when Bright is on
        c.addResistor (br, wv, 2.2e3);
        c.addCapacitor (wv, b2, 1.0e-6);
        // Q2: R11 10K collector, R12 680 emitter, R10 2.7M collector-base bias.
        c.addBjt (c2, b2, e2, false, npn);
        c.addResistor (vcc, c2, 10.0e3);
        c.addResistor (e2, gnd, 680.0);
        c.addResistor (c2, b2, 2.7e6);
        c.setInitialGuess (b2, 1.3);
        c.setInitialGuess (e2, 0.7);
        c.setInitialGuess (c2, 11.0);

        // Q3 follower (R13 10K) -> C8 1.0 -> Q4: R14 270K collector-base, R15 680 emitter, R16 3.9K collector.
        const auto e3 = c.addNode(), b4 = c.addNode(), c4 = c.addNode(), e4 = c.addNode();
        c.addBjt (vcc, c2, e3, false, npn);
        c.addResistor (e3, gnd, 10.0e3);
        c.addCapacitor (e3, b4, 1.0e-6);
        c.addBjt (c4, b4, e4, false, npn);
        c.addResistor (vcc, c4, 3.9e3);
        c.addResistor (e4, gnd, 680.0);
        c.addResistor (c4, b4, 270.0e3);
        c.setInitialGuess (e3, 10.4);
        c.setInitialGuess (b4, 1.3);
        c.setInitialGuess (e4, 0.65);
        c.setInitialGuess (c4, 13.0);
        ch.pCol2 = c4;

        // Treble/Bass: C40 .0025 -> TREBLE R18 25K (top), R17 10K -> A, C10 1.0 A -> B (treble bottom = bass top),
        // BASS R19 25K as a rheostat B -> D, C11 .47 A -> D, R20 680 D -> ground; the treble wiper -> C12 1.0 -> Q5.
        const auto tt = c.addNode(), ta = c.addNode(), tb = c.addNode(), td = c.addNode(), tw = c.addNode();
        c.addCapacitor (c4, tt, 0.0025e-6);
        c.addResistor (c4, ta, 10.0e3);
        ch.rTrebleTop = c.addResistor (tt, tw, 12.5e3);
        ch.rTrebleBot = c.addResistor (tw, tb, 12.5e3);
        c.addCapacitor (ta, tb, 1.0e-6);
        ch.rBassTop = c.addResistor (tb, td, 12.5e3);     // the bass rheostat (wiper tied to its top)
        ch.rBassBot = c.addResistor (td, gnd, 1.0e9);     // unused leg: keeps the handle (R19 has no bottom leg)
        c.addCapacitor (ta, td, 0.47e-6);
        c.addResistor (td, gnd, 680.0);
        ch.pTone = tw;

        // Q5: C12 1.0 in, R21 1M collector-base, R22 4.7K collector, R23 2.2K emitter.
        // VARIAMP: collector -> C14 6.8 -> R24 50K <- wiper -> R25 680 -> C13 6.8 -> emitter; the wiper -> R26 100K to
        // ground and -> L1 1.5 H -> the RANGE cap to ground. Wiper toward the collector: the trap shunts the output (cut);
        // toward the emitter: it bypasses the emitter resistor at resonance (boost); centre: about flat.
        const auto b5 = c.addNode(), c5 = c.addNode(), e5 = c.addNode();
        const auto vc = c.addNode(), vw = c.addNode(), ve = c.addNode(), vee = c.addNode(), vl = c.addNode();
        c.addCapacitor (tw, b5, 1.0e-6);
        c.addBjt (c5, b5, e5, false, npn);
        c.addResistor (vcc, c5, 4.7e3);
        c.addResistor (e5, gnd, 2.2e3);
        c.addResistor (c5, b5, 1.0e6);
        c.addCapacitor (c5, vc, 6.8e-6);
        ch.rEffect = c.addResistor (vc, vw, 25.0e3);
        ch.rEffectB = c.addResistor (vw, ve, 25.0e3);
        c.addResistor (ve, vee, 680.0);
        c.addCapacitor (vee, e5, 6.8e-6);
        c.addResistor (vw, gnd, 100.0e3);
        ch.grpVarL = c.addCoupledInductors ({ { vw, vl } }, { variampInductance });
        ch.capVarC = c.addCapacitor (vl, gnd, variampCapacitance[1]);
        c.setInitialGuess (b5, 2.3);
        c.setInitialGuess (e5, 1.65);
        c.setInitialGuess (c5, 13.0);
        ch.pVar = vw;

        // Q6 follower (R27 10K) -> C19 1.0 -> R28 8.2K -> R29 50K level pot (kept at max) -> the 361's input.
        const auto e6 = c.addNode(), o1 = c.addNode(), po = c.addNode();
        c.addBjt (vcc, c5, e6, false, npn);
        c.addResistor (e6, gnd, 10.0e3);
        c.addCapacitor (e6, o1, 1.0e-6);
        c.addResistor (o1, po, 8.2e3);
        c.addResistor (po, gnd, 50.0e3);
        c.setInitialGuess (e6, 12.4);
        ch.pOut = po;
    }

    // ================================================================ power amp: 200 W solid-state
    // -- a saturating op-amp gain block driving the speaker directly (no output transformer).
    {
        auto& c = ch.power;
        c.setIntegrationTheta (0.9);
        const auto cin = c.addNode(), ip = c.addNode(), im = c.addNode();
        ch.wOut = c.addNode();
        ch.wSrcPre = c.addSource (cin, 0.0);
        ch.wTone = cin;
        c.addResistor (cin, gnd, 1.0e6);

        // FULL reference power stage only -- reducedOrder replaces everything below with
        // behavioralPowerStage(), fitted to this same circuit (see the header + docs/circuits/Acoustic360.md).
        if (! reducedOrder)
        {
        // 361 power amp (schematic 1005A): input C2 .47 -> R2 10K to ground -> C3 .1 -> R35 1K -> the 2N4248 pair
        // (R3 47K bias); NFB R11 100K from the output into R9 470 + (R10 100K || C6 10 uF): ~46 dB closed loop.
        // A single +80 V rail (C1 7800 uF) swinging +-40 V around half-rail, the speaker through C9 3900 uF.
        const auto n1 = c.addNode(), n2 = c.addNode(), fb = c.addNode(), amp = c.addNode();
        c.addCapacitor (cin, n1, 0.47e-6);
        c.addResistor (n1, gnd, 10.0e3);
        c.addCapacitor (n1, n2, 0.1e-6);
        c.addResistor (n2, gnd, 47.0e3);
        c.addResistor (n2, ip, 1.0e3);
        c.addResistor (amp, im, 100.0e3);
        c.addResistor (im, fb, 470.0);
        c.addResistor (fb, gnd, 100.0e3);
        c.addCapacitor (fb, gnd, 10.0e-6);
        c.addSaturatingOpAmp (ip, im, amp, { -40.0, 40.0 });
        c.addCapacitor (amp, ch.wOut, 3900.0e-6);

        {
            const auto sm = speakerModel (4.0);
            const auto na2 = c.addNode(), nbb2 = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na2, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na2, nbb2 } }, { sm.le });
            ch.rSpkRp = c.addResistor (nbb2, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb2, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb2, gnd, sm.cp);
        }
        c.setInitialGuess (ip, 0.0);
        c.setInitialGuess (im, 0.0);
        c.setInitialGuess (amp, 0.0);
        }
    }
}

double Acoustic360StyleAmplifierProcessor::sagRail (double envelope) const noexcept
{
    // Stiff solid-state rails: only ~3 % droop under full drive (big filter bank, no tube rectifier).
    return 65.0 * juce::jlimit (0.94, 1.0, 1.0 - 0.002 * envelope);
}

double Acoustic360StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) noexcept
{
    const double attackCoeff = 1.0 - std::exp (-1.0 / (0.008 * sampleRate));
    const double releaseCoeff = 1.0 - std::exp (-1.0 / (0.045 * sampleRate));
    const double absDrive = std::abs (toneVoltage);
    ch.bmEnvelope += (absDrive > ch.bmEnvelope ? attackCoeff : releaseCoeff) * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRail (ch.bmEnvelope);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double over = toneVoltage / bmGridClampV;
    const double clamped = toneVoltage / std::sqrt (1.0 + over * over);
    // a hard solid-state clip (the 361's quasi-complementary pair runs clean up to its rails)
    const auto knee = [k, rail = ch.bmRail] (double x)
    {
        const double u = x / juce::jmax (1.0e-9, k);
        return u / std::pow (1.0 + std::pow (std::abs (u), bmKneeN), 1.0 / bmKneeN) * rail * bmYmax;
    };
    const double shift = bmAsym * k;
    const double raw = knee (clamped + shift) - knee (shift);

    const double dcCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmDcHz / sampleRate);
    ch.bmDcState += dcCoeff * (raw - ch.bmDcState);
    const double rawAc = raw - ch.bmDcState;

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / sampleRate);
    ch.bmToneState += shelfCoeff * (rawAc - ch.bmToneState);
    ch.bmOutput = ch.bmTail.process (ch.bmToneState + bmShelfHfGain * (rawAc - ch.bmToneState));
    return ch.bmOutput;
}

void Acoustic360StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    const auto sm = speakerModel (speakerNominal[juce::jlimit (0, 1, index)]);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void Acoustic360StyleAmplifierProcessor::applyVariamp (Channel& ch, int index) const
{
    const int i = juce::jlimit (0, variampPositions - 1, index);
    ch.pre.setCapacitance (ch.capVarC, variampCapacitance[i]);
    ch.pre.setInductorInverse (ch.grpVarL, 1.0 / variampInductance);
}

void Acoustic360StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // VOLUME 50K (audio): up = the wiper toward the input end -> more feedback resistance per input resistance.
    const double volIn = juce::jmax (1.0, 50.0e3 * (1.0 - pots::audio (k.volume)));
    const double trebleBot = juce::jmax (1.0, 25.0e3 * k.treble);
    const double bassR = juce::jmax (1.0, 25.0e3 * k.bass);    // the bass rheostat in series with the bass leg: up = more bass
    // VARIAMP depth (R24 50K): 0 = full cut, 0.5 = flat, 1 = full boost.
    const double effectTop = juce::jmax (1.0, 50.0e3 * k.effect);
    const double brightC = k.bright > 0.5 ? 0.01e-6 : 1.0e-12;

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolumeTop, volIn);
        ch.pre.setResistance (ch.rVolumeBot, juce::jmax (1.0, 50.0e3 - volIn));
        ch.pre.setCapacitance (ch.capBright, brightC);
        ch.pre.setResistance (ch.rBassTop, bassR);
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 25.0e3 - trebleBot));
        ch.pre.setResistance (ch.rTrebleBot, trebleBot);
        ch.pre.setResistance (ch.rEffect, effectTop);
        ch.pre.setResistance (ch.rEffectB, juce::jmax (1.0, 50.0e3 - effectTop));
        if (! reducedOrder && ! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        if (k.variamp != appliedVariamp)
            applyVariamp (ch, k.variamp);
    }
    appliedSpeaker = k.speaker;
    appliedVariamp = k.variamp;
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 1, k.speaker)] / 4.0, -0.8);
}

void Acoustic360StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.failStreak = 0;
    ch.alignOutput = true;
}

double Acoustic360StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::firstCollector: return ch.pre.voltage (ch.pCol1);
        case Probe::toneStackOut: return ch.pre.voltage (ch.pTone);
        case Probe::secondCollector: return ch.pre.voltage (ch.pCol2);
        case Probe::variampNode: return ch.pre.voltage (ch.pVar);
        case Probe::preampOut: return ch.pre.voltage (ch.pOut);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

double Acoustic360StyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void Acoustic360StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e-3);
        ch.power.setInductorInverse (ch.grpSpkLe, { 1.0e6 });
        ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 });
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-9);
    }
    appliedSpeaker = -2;
    resistiveLoadForced = true;
}

void Acoustic360StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, voicings::solidState, 1.0 / outputScale); // direct-coupled: speaker reaction only
    }

    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volumeParam->get());
    smoothedBass.reset (newSampleRate, 0.02);
    smoothedBass.setCurrentAndTargetValue (bassParam->get());
    smoothedTreble.reset (newSampleRate, 0.02);
    smoothedTreble.setCurrentAndTargetValue (trebleParam->get());
    smoothedEffect.reset (newSampleRate, 0.02);
    smoothedEffect.setCurrentAndTargetValue (effectParam->get());
    smoothedOutput.reset (newSampleRate, 0.02);
    smoothedOutput.setCurrentAndTargetValue (outputParam->get());

    appliedSpeaker = 0;   // built with the 4 ohm speaker
    appliedVariamp = 2;
    updatePots ({ (double) volumeParam->get(), (double) brightParam->get(), (double) bassParam->get(),
                  (double) trebleParam->get(), (double) effectParam->get(),
                  juce::roundToInt (variampParam->get()), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        dcOk = ch.power.prepare (newSampleRate) && dcOk;
        ch.power.solveSample();
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.failStreak = 0;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true;
    channel1Stale = false;
    identicalRun = 0;
}

void Acoustic360StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

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

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedEffect.setTargetValue (effectParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int variampChoice = juce::roundToInt (variampParam->get());
    const double brightOn = brightParam->get() > 0.5f ? 1.0 : 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float ef = smoothedEffect.getNextValue();
        const float ou = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ (double) vo, brightOn, (double) ba, (double) tr, (double) ef,
                          variampChoice, speakerChoice });
        }

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

            ch.power.setSource (ch.wSrcPre, ch.pre.voltage (ch.pOut));
            const bool ok2 = ch.power.solveSample();
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.power.voltage (ch.wTone))
                                                     : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 250.0;
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
                if (! ok)
                    ++failureCount;
            }
        }
    }

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void Acoustic360StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
