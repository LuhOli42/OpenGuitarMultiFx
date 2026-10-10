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
    constexpr double railNominal = 24.0; // the 360's single-rail preamp supply

    // ---- transistors: small-signal NPN of the era (2N5088/2N3394 class). Standard Ebers-Moll
    // set used across this project's SS pedals.
    const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, 300.0, 4.0 };

    // Variamp: selectable L-C trap; L fixed, the rotary picks C (see the doc for the mapping).
    constexpr double variampInductance = 0.5;
    constexpr double variampCapacitance[5] = { 0.33e-6, 0.1e-6, 0.033e-6, 0.01e-6, 0.0033e-6 };

    constexpr double speakerNominal[2] = { 4.0, 8.0 };
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
        "acoustic360_variamp", "Variamp", juce::NormalisableRange<float> (0.0f, 4.0f, 1.0f), 2.0f,
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
        "acoustic360", "360-Style Amplifier", "|", std::move (volume));
    group->addChild (std::move (bright));
    group->addChild (std::move (bass));
    group->addChild (std::move (treble));
    group->addChild (std::move (effect));
    group->addChild (std::move (variamp));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("acoustic360_page2", "Page 2", "|", std::move (output));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void Acoustic360StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ preamp: two NPN
    // common-emitter stages on 24 V around the passive bass/treble network and the Variamp trap.
    {
        auto& c = ch.pre;
        c.setIntegrationTheta (0.5);
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railNominal);
        ch.pSrcIn = c.addSource (in, 0.0);

        // Q1 -- input stage: divider bias, ~20x gain.
        const auto b1 = c.addNode(), c1 = c.addNode(), e1 = c.addNode(), ee1 = c.addNode();
        c.addCapacitor (in, b1, 1.0e-6);
        c.addResistor (vcc, b1, 150.0e3);
        c.addResistor (b1, gnd, 47.0e3);
        c.addBjt (c1, b1, e1, false, npn);
        c.addResistor (vcc, c1, 4.7e3);
        c.addResistor (e1, ee1, 220.0);                  // un-bypassed: sets the stage gain
        c.addResistor (ee1, gnd, 2.0e3);
        c.addCapacitor (ee1, gnd, 22.0e-6);
        c.setInitialGuess (b1, 5.6);
        c.setInitialGuess (e1, 5.0);
        c.setInitialGuess (c1, 13.0);
        ch.pCol1 = c1;

        // Passive bass/treble network (Fender-family, scaled to solid-state impedances).
        const auto ti = c.addNode(), t1 = c.addNode(), w1 = c.addNode(), nb = c.addNode(),
                   ns = c.addNode(), t2 = c.addNode(), wt = c.addNode(), tb = c.addNode();
        c.addCapacitor (c1, ti, 0.47e-6);
        c.addResistor (ti, t1, 56.0e3);
        ch.rBassTop = c.addResistor (t1, w1, 125.0e3);   // Bass 250k audio
        ch.rBassBot = c.addResistor (w1, nb, 125.0e3);
        c.addResistor (nb, gnd, 5.6e3);
        c.addCapacitor (w1, ns, 0.047e-6);             // bass-path cap, voiced for ~40 Hz control
        c.addResistor (ns, t2, 22.0e3);
        c.addCapacitor (ti, t2, 4.7e-9);                 // treble feed
        ch.rTrebleTop = c.addResistor (t2, wt, 125.0e3); // Treble 250k audio
        ch.rTrebleBot = c.addResistor (wt, tb, 125.0e3);
        c.addCapacitor (tb, gnd, 0.022e-6);
        ch.pTone = wt;
        c.setInitialGuess (ti, 0.0);

        // Q2 -- recovery stage after the lossy network.
        const auto b2 = c.addNode(), c2 = c.addNode(), e2 = c.addNode(), ee2 = c.addNode();
        c.addCapacitor (wt, b2, 0.47e-6);
        c.addResistor (vcc, b2, 150.0e3);
        c.addResistor (b2, gnd, 47.0e3);
        c.addBjt (c2, b2, e2, false, npn);
        c.addResistor (vcc, c2, 4.7e3);
        c.addResistor (e2, ee2, 470.0);
        c.addResistor (ee2, gnd, 2.0e3);
        c.addCapacitor (ee2, gnd, 22.0e-6);
        c.setInitialGuess (b2, 5.6);
        c.setInitialGuess (e2, 5.0);
        c.setInitialGuess (c2, 13.0);
        ch.pCol2 = c2;

        // The Variamp: a selectable series L-C trap on the recovery output through the Effect pot --
        // the amp's Varitone-derived active voicing (see the doc for what is simplified).
        const auto mt = c.addNode(), mtp = c.addNode(), mc = c.addNode();
        c.addCapacitor (c2, mt, 0.47e-6);
        c.addResistor (mt, gnd, 100.0e3);
        ch.rEffect = c.addResistor (mt, mtp, 50.0e3);    // Effect: 100k linear rheostat
        ch.capVarC = c.addCapacitor (mtp, mc, variampCapacitance[2]);
        ch.grpVarL = c.addCoupledInductors ({ { mc, gnd } }, { variampInductance });
        ch.pVar = mt;
        c.setInitialGuess (mt, 0.0);

        // Volume (front panel) -> out, with the Bright treble-bleed cap across the top leg.
        const auto vw = c.addNode(), po = c.addNode();
        ch.rVolumeTop = c.addResistor (mt, vw, 250.0e3); // Volume 500k linear-ish
        ch.rVolumeBot = c.addResistor (vw, gnd, 250.0e3);
        ch.capBright = c.addCapacitor (mt, vw, 1.0e-12); // Bright on: 470 pF bleed
        c.addCapacitor (vw, po, 0.47e-6);
        c.addResistor (po, gnd, 1.0e6);
        c.setInitialGuess (vw, 0.0);
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
        c.addResistor (cin, ip, 22.0e3);
        c.addResistor (ip, gnd, 100.0e3);
        c.addResistor (ch.wOut, im, 56.0e3);             // feedback divider -> gain ~30
        c.addResistor (im, gnd, 2.0e3);
        c.addCapacitor (im, gnd, 0.047e-6);              // LF stabilisation at the input leg
        c.addSaturatingOpAmp (ip, im, ch.wOut, { -40.0, 40.0 }); // ~28 Vrms into 4 ohm, plus margin

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
    const auto knee = [k, rail = ch.bmRail] (double x) { return std::tanh (x / juce::jmax (1.0e-9, k)) * rail * bmYmax; };
    const double shift = bmAsym * k;
    const double raw = knee (clamped + shift) - knee (shift);

    const double dcCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmDcHz / sampleRate);
    ch.bmDcState += dcCoeff * (raw - ch.bmDcState);
    const double rawAc = raw - ch.bmDcState;

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / sampleRate);
    ch.bmToneState += shelfCoeff * (rawAc - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (rawAc - ch.bmToneState);
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
    const int i = juce::jlimit (0, 4, index);
    ch.pre.setCapacitance (ch.capVarC, variampCapacitance[i]);
    ch.pre.setInductorInverse (ch.grpVarL, 1.0 / variampInductance);
}

void Acoustic360StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double volumeBot = juce::jmax (1.0, 500.0e3 * pots::audio (k.volume));
    const double bassBot = juce::jmax (1.0, 250.0e3 * pots::audio (k.bass));
    const double trebleBot = juce::jmax (1.0, 250.0e3 * pots::audio (k.treble));
    const double effectR = juce::jmax (1.0, 100.0e3 * (1.0 - k.effect)); // Effect up = trap fully in circuit
    const double brightC = k.bright > 0.5 ? 470.0e-12 : 1.0e-12;

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolumeTop, juce::jmax (1.0, 500.0e3 - volumeBot));
        ch.pre.setResistance (ch.rVolumeBot, volumeBot);
        ch.pre.setCapacitance (ch.capBright, brightC);
        ch.pre.setResistance (ch.rBassTop, juce::jmax (1.0, 250.0e3 - bassBot));
        ch.pre.setResistance (ch.rBassBot, bassBot);
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 250.0e3 - trebleBot));
        ch.pre.setResistance (ch.rTrebleBot, trebleBot);
        ch.pre.setResistance (ch.rEffect, effectR);
        if (! reducedOrder)
            /* speaker handled by behavioral stage */;
        else         if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        if (k.variamp != appliedVariamp)
            applyVariamp (ch, k.variamp);
    }
    appliedSpeaker = k.speaker;
    appliedVariamp = k.variamp;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 1, k.speaker)] / 4.0, -0.8);
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
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
