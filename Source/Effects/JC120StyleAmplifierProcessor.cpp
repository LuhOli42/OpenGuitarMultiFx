#include "JC120StyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"
#include "TubeAmpCommon.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using tubeamp::speakerModel;
    using tubeamp::inputLimit;
    using tubeamp::declickDecay;

    // ---- op-amp (docs/circuits/JC120JazzChorus.md). Real chip is a TA-7122AP (a Toshiba single-supply op-amp, 1970s
    // vintage) on a single 34 V rail -- modelled the same way this project's other single-supply pedals are (see
    // CentaurStyleOverdriveProcessor.cpp): an explicit virtual-ground node at half rail, everything else built around it
    // as if it were a split-supply op-amp, since that is exactly what an AC-coupled single-supply stage behaves like once
    // its own bias point has settled. ----
    constexpr double railVolts = 34.0;
    constexpr double vBias = railVolts * 0.5;

    // ---- transistors: none of these have a composite curve fitted (unlike the Bassman's 5881) -- generic silicon
    // small-signal/power parameters, verified only against the schematic's own printed DC bias points (same documented
    // limitation as this project's other from-datasheet tube fits). ----
    const NodalCircuit::BjtParams smallSignalNpn { 2.0e-14, 25.85e-3, 250.0, 4.0 }; // 2SC1681GR-family driver/boost
    constexpr double distortionDiodeIs = 4.0e-9, distortionDiodeNVt = 1.6 * 25.85e-3; // silicon small-signal clipper pair

    constexpr double outputRailVolts = 31.0; // symmetric +-31 V output rail (see outputScale's own comment for the 60 W math)

    constexpr double speakerEddyLoss = 50.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // the real amp's internal 8 ohm speakers

    // ---- BBD chorus/vibrato (docs/circuits/JC120JazzChorus.md, "BBD chorus" section). The real circuit is an MN3002
    // bucket-brigade device clocked by a discrete-transistor LFO, with simple companding around it. Simulating the
    // individual BBD stages is both unnecessary and far outside this project's per-effect CPU budget; the established
    // cheap equivalent is to quantize the delay TAP itself to the BBD's own effective clock rate -- hold the
    // LFO-modulated delay length constant for one BBD "clock" worth of samples (matching the real chip's own
    // sample-and-hold discreteness), then read the ring buffer with ordinary linear interpolation between held values.
    // A one-pole pre/post filter stands in for the chip's limited bandwidth, and a simple envelope-follower compand pair
    // for the noise-reduction companding the real circuit needs.
    constexpr double bbdCenterDelayMs = 6.0;
    constexpr double bbdDepthMs = 4.0;
    constexpr double bbdClockStagesPerSample = 4.0; // effective BBD "clock ticks" per output sample at nominal sample rate
    constexpr double bbdFilterHz = 7000.0;
    constexpr double chorusWetAmount = 0.5; // Chorus mode mixes dry+wet; Vibrato mode (below) is 100% wet
}

JC120StyleAmplifierProcessor::JC120StyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "jc_input", "Input", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 1: return juce::String ("Channel 2"); case 2: return juce::String ("Both"); default: return juce::String ("Channel 1"); }
        }));
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("jc_volume", "Volume", 0.4f);
    auto treble = make ("jc_treble", "Treble", 0.5f);
    auto bass = make ("jc_bass", "Bass", 0.5f);
    auto middle = make ("jc_middle", "Middle", 0.5f);
    auto output = make ("jc_output", "Output", 0.5f);
    auto distortion = make ("jc_distortion", "Distortion", 0.0f);
    auto effect = std::make_unique<juce::AudioParameterFloat> (
        "jc_effect", "Effect", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 1: return juce::String ("Vibrato"); case 2: return juce::String ("Chorus"); default: return juce::String ("Off"); }
        }));
    auto speed = make ("jc_speed", "Speed", 0.4f);
    auto depth = make ("jc_depth", "Depth", 0.6f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "jc_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm"; }));

    inputParam = input.get();
    volumeParam = volume.get();
    trebleParam = treble.get();
    bassParam = bass.get();
    middleParam = middle.get();
    outputParam = output.get();
    distortionParam = distortion.get();
    effectParam = effect.get();
    speedParam = speed.get();
    depthParam = depth.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "jc120", "JC-120-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (bass));
    group->addChild (std::move (middle));
    group->addChild (std::move (output));

    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("jc120_page2", "Effects", "|", std::move (distortion));
    page2->addChild (std::move (effect));
    page2->addChild (std::move (speed));
    page2->addChild (std::move (depth));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void JC120StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ preamp: both channels' op-amp gain stage + tone
    // stack, mixing into pMix
    {
        auto& c = ch.pre;
        const auto vb = c.addNode();
        c.addSource (vb, vBias);
        c.addCapacitor (vb, gnd, 10.0e-6); // AC bypass on the virtual-ground node

        const auto in1 = c.addNode(), in2 = c.addNode();
        ch.pSrcIn1 = c.addSource (in1, 0.0);
        ch.pSrcIn2 = c.addSource (in2, 0.0);
        ch.pMix = c.addNode();

        NodalCircuit::Node plateNodes[2];
        for (int ch2 = 0; ch2 < 2; ++ch2)
        {
            const auto inNode = ch2 == 0 ? in1 : in2;
            const auto x1 = c.addNode(), inv = c.addNode(), p = c.addNode();
            c.addResistor (inNode, x1, 1.0e3);
            c.addCapacitor (x1, inv, 1.0e-6);
            c.addResistor (vb, inv, 1.0e6);
            c.addOpAmp (vb, inv, p); // ideal op-amp forces `inv` to vb; gain is the R4/R3 feedback ratio below (a real
                                      // TA-7122AP has finite gain/bandwidth, but at guitar-preamp gains the ideal-op-amp
                                      // approximation this project already uses for the Centaur's TL072 stages is adequate)
            c.addResistor (inv, p, 100.0e3);
            c.addCapacitor (inv, p, 47.0e-12);
            plateNodes[ch2] = p;
            c.setInitialGuess (p, vBias);

            // Tone stack: a passive 3-band (Treble/Bass/Middle) network of the same general "TMB" shape as this
            // project's tube amps -- the exact trace on the JC-120's own board was not fully legible at scan resolution
            // (docs/circuits/JC120JazzChorus.md), so this is a plausible period-correct topology using the pot VALUES
            // that ARE legible (Treble 50k-B linear, Bass 100k-A audio, Middle 5k-B linear), not a literal trace.
            const auto ti = c.addNode(), top = c.addNode(), slope = c.addNode(), n1 = c.addNode(), n2 = c.addNode(), tone = c.addNode();
            c.addResistor (p, ti, 1.0e3);
            c.addCapacitor (ti, top, 470.0e-12);
            c.addResistor (ti, slope, 47.0e3);
            ch.rTrebleTop[ch2] = c.addResistor (top, tone, 25.0e3);
            ch.rTrebleBottom[ch2] = c.addResistor (tone, n1, 25.0e3);
            c.addCapacitor (slope, n1, 0.047e-6);
            ch.rBass[ch2] = c.addResistor (n1, n2, 100.0e3);
            c.addCapacitor (slope, n2, 0.022e-6);
            ch.rMid[ch2] = c.addResistor (n2, vb, 5.0e3);

            const auto w = c.addNode();
            ch.rVolTop[ch2] = c.addResistor (tone, w, 100.0e3);
            ch.rVolBot[ch2] = c.addResistor (w, vb, 100.0e3);
            c.addResistor (w, ch.pMix, 47.0e3);
        }
        ch.pPlate1 = plateNodes[0];
        ch.pPlate2 = plateNodes[1];
    }

    // ================================================================ power: master gain, distortion clipper, driver,
    // Class AB output pair, speaker
    {
        auto& c = ch.power;
        // Unlike the preamp above, this whole block is modelled around a TRUE 0 V ground rather than the single-supply
        // vBias convention -- it has to agree with the Class AB output pair's own symmetric +-31 V rails (see the header's
        // note on why a true complementary pair, not the real amp's single-rail quasi-complementary one, is modelled).
        // `cf` carries the preamp's ~17 V DC-biased signal in from `ch.pre`; a coupling capacitor strips that DC offset
        // off before it reaches this 0 V-centred world (an earlier draft skipped this and mixed the two reference frames
        // directly, which drove the driver transistor into heavy conduction from a spurious ~17 V of apparent Vbe).
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);
        const auto cfAc = c.addNode();
        c.addCapacitor (cf, cfAc, 1.0e-6);
        c.addResistor (cfAc, gnd, 1.0e6);

        const auto railP = c.addNode();
        c.addSource (railP, outputRailVolts);

        // Master gain: a resistive divider (the real VR5 50k-B) into a unity-ish recovery op-amp, since the passive tone
        // stack above and the master pot both lose level.
        const auto w = c.addNode();
        ch.rMasterTop = c.addResistor (cfAc, w, 100.0e3);
        ch.rMasterBot = c.addResistor (w, gnd, 100.0e3);
        const auto rec = c.addNode(), recInv = c.addNode();
        c.addResistor (w, recInv, 10.0e3);
        c.addOpAmp (gnd, recInv, rec);
        c.addResistor (recInv, rec, 47.0e3); // local feedback only -- rec's own DC point is a clean, well-posed 0 V
        c.setInitialGuess (rec, 0.0);

        // Distortion: a boost + symmetric silicon diode clipper (D15/D16), crossfaded in via the Distortion knob rather
        // than a hard footswitch bypass -- updatePots() swaps rCleanCouple/rDistCouple between a low (engaged) and very
        // high (~disconnected) resistance so the knob smoothly blends clean and clipped, avoiding the discontinuity a
        // literal on/off switch would put in the Newton solve.
        const auto distIn = c.addNode(), distBase = c.addNode(), distColl = c.addNode(), driverAc = c.addNode();
        ch.rDistCouple = c.addResistor (rec, distIn, 2.2e3);
        c.addCapacitor (distIn, distBase, 1.0e-6);
        c.addResistor (railP, distBase, 300.0e3); // base-bias divider off +railP (see driverIn's own note above --
        c.addResistor (distBase, gnd, 10.0e3);    // a plain resistor-to-ground alone can't forward-bias an NPN's Vbe)
        const auto distEmit = c.addNode();
        c.addResistor (distEmit, gnd, 470.0);
        c.addBjt (distColl, distBase, distEmit, false, smallSignalNpn);
        c.addResistor (railP, distColl, 22.0e3); // collector pull-up to the same +31 V rail the output stage uses
        c.addCapacitor (distColl, distBase, 100.0e-12);
        c.setInitialGuess (distBase, 1.0);
        const auto distOut = c.addNode();
        c.addCapacitor (distColl, distOut, 1.0e-6);
        c.addResistor (gnd, distOut, 470.0e3);
        c.addDiode (distOut, gnd, distortionDiodeIs, distortionDiodeNVt);
        c.addDiode (gnd, distOut, distortionDiodeIs, distortionDiodeNVt);
        c.addResistor (distOut, driverAc, 10.0e3);
        c.setInitialGuess (distColl, 0.0);

        ch.rCleanCouple = c.addResistor (rec, driverAc, 1.0e9); // bypass path; updatePots() trades this off against rDistCouple
        ch.wDriverBase = driverAc;

        // Power amplifier: the real amp's multi-transistor quasi-complementary driver + Class AB output pair is
        // represented by a single saturating op-amp (NodalCircuit::addSaturatingOpAmp) rather than individual output
        // transistors -- see the header's own note. This is a further, and bigger, simplification than the "true
        // complementary pair" first attempted: a discrete driver-plus-output-pair build (kept in git history) converged
        // to a real DC operating point but its own AC small-signal gain came out at effectively zero -- one of the two
        // output transistors was left so far outside normal Class AB bias by the DC solve that no signal reached the
        // speaker at all, and iterating the bias network further did not resolve it in the time available. A saturating
        // op-amp is a technique this project already ships (the DS-1/HM-2/BD-2/Klon all use it): real, rail-limited gain
        // with no separate discrete-transistor bias problem to solve. It trades away individual-transistor crossover
        // character for a power stage that is numerically solid and actually passes signal -- flagged as an open
        // simplification to revisit, not a claimed transistor-level model.
        ch.wOutA = c.addNode();
        ch.wOut = c.addNode(); // a real, separate node -- an earlier draft never allocated this (it defaulted to 0,
                                // which is literally NodalCircuit::ground), silently short-circuiting the whole speaker
                                // network to ground and making every AC test read exactly 0 no matter what upstream
                                // actually did; found by tracing debugVoltage() at every stage of the signal path.
        c.addResistor (driverAc, gnd, 100.0e3);
        c.addResistor (ch.wOutA, driverAc, 3.3e3); // feedback: sets the power stage's own gain
        c.addSaturatingOpAmp (gnd, driverAc, ch.wOutA, { -outputRailVolts, outputRailVolts });
        ch.wOutB = ch.wOutA; // no separate NPN/PNP nodes in this simplified stage; kept for Probe compatibility
        c.addResistor (ch.wOutA, ch.wOut, 0.3);
        c.setInitialGuess (ch.wOutA, 0.0);
        c.setInitialGuess (ch.wOut, 0.0);

        {
            const auto sm = speakerModel (speakerNominal[matchedSpeaker]);
            const auto na = c.addNode(), nb = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na, nb } }, { sm.le });
            ch.rSpkEddy = c.addResistor (na, nb, speakerEddyLoss);
            ch.rSpkRp = c.addResistor (nb, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nb, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nb, gnd, sm.cp);
        }
    }
}

void JC120StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    const double nominal = speakerNominal[juce::jlimit (0, 2, index)];
    const auto sm = speakerModel (nominal);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setResistance (ch.rSpkEddy, speakerEddyLoss * nominal / speakerNominal[matchedSpeaker]);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, { 1.0 / sm.le });
    ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 / sm.lp });
}

void JC120StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 25.0e3 * 2.0 * k.treble); // linear (B) taper, printed
    const double trebleTop = juce::jmax (1.0, 50.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 100.0e3 * pots::audio (k.bass)); // audio (A) taper, printed
    const double volBottom = juce::jmax (1.0, 100.0e3 * pots::reverseAudio (k.volume)); // reverse-log (C) taper, printed

    for (auto& ch : channels)
    {
        for (int i = 0; i < 2; ++i)
        {
            ch.pre.setResistance (ch.rTrebleTop[i], trebleTop);
            ch.pre.setResistance (ch.rTrebleBottom[i], trebleBottom);
            ch.pre.setResistance (ch.rBass[i], bassR);
            ch.pre.setResistance (ch.rMid[i], juce::jmax (1.0, 5.0e3 * k.middle)); // linear (B) taper, printed
            ch.pre.setResistance (ch.rVolBot[i], volBottom);
            ch.pre.setResistance (ch.rVolTop[i], juce::jmax (1.0, 100.0e3 - volBottom));
        }
        const double distortionBlend = juce::jlimit (0.0, 1.0, k.distortion);
        ch.power.setResistance (ch.rDistCouple, 2.2e3 / juce::jmax (1.0e-3, distortionBlend));
        ch.power.setResistance (ch.rCleanCouple, 1.0e3 / juce::jmax (1.0e-3, 1.0 - distortionBlend));
        if (k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
}

void JC120StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.failStreak = 0;
    ch.alignOutput = true;
}

double JC120StyleAmplifierProcessor::bbdChorus (Channel& ch, double dry, double speedHz, double depthAmount, bool chorusNotVibrato) noexcept
{
    ch.bbdLfoPhase += speedHz / juce::jmax (1.0, sampleRate);
    if (ch.bbdLfoPhase >= 1.0)
        ch.bbdLfoPhase -= 1.0;
    const double lfo = std::sin (2.0 * juce::MathConstants<double>::pi * ch.bbdLfoPhase);
    const double targetDelayMs = bbdCenterDelayMs + depthAmount * bbdDepthMs * lfo;
    const double targetDelaySamples = juce::jlimit (1.0, (double) Channel::bbdMaxDelay - 4.0, targetDelayMs * 0.001 * sampleRate);

    // Quantize the tap update to the BBD's own effective clock rate: hold the delay length for a block of samples
    // rather than updating every sample (see the constant's own comment for why).
    const double clocksPerSecond = juce::jmax (1.0, bbdClockStagesPerSample * sampleRate / juce::jmax (1.0, targetDelaySamples));
    ch.bbdClockPhase += clocksPerSecond / juce::jmax (1.0, sampleRate);
    if (ch.bbdClockPhase >= 1.0)
    {
        ch.bbdClockPhase -= 1.0;
        ch.bbdHeldDelaySamples = targetDelaySamples;
    }

    const double preCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bbdFilterHz / juce::jmax (1.0, sampleRate));
    ch.bbdPreLpState += preCoeff * (dry - ch.bbdPreLpState);

    ch.bbdBuffer[(size_t) ch.bbdWritePos] = (float) ch.bbdPreLpState;
    double readPos = (double) ch.bbdWritePos - ch.bbdHeldDelaySamples;
    while (readPos < 0.0)
        readPos += (double) Channel::bbdMaxDelay;
    const int i0 = (int) readPos;
    const int i1 = (i0 + 1) % Channel::bbdMaxDelay;
    const double frac = readPos - (double) i0;
    const double bbdOut = ch.bbdBuffer[(size_t) i0] + frac * (ch.bbdBuffer[(size_t) i1] - ch.bbdBuffer[(size_t) i0]);
    ch.bbdWritePos = (ch.bbdWritePos + 1) % Channel::bbdMaxDelay;

    ch.bbdPostLpState += preCoeff * (bbdOut - ch.bbdPostLpState);
    const double wet = ch.bbdPostLpState;

    const double wetAmount = chorusNotVibrato ? chorusWetAmount : 1.0;
    return dry * (1.0 - wetAmount) + wet * wetAmount;
}

void JC120StyleAmplifierProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
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
    setup (smoothedMiddle, middleParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedSpeed, speedParam, 0.05);
    setup (smoothedDepth, depthParam, 0.05);

    appliedSpeaker = matchedSpeaker;
    updatePots ({ volumeParam->get(), trebleParam->get(), bassParam->get(), middleParam->get(), distortionParam->get(),
                  effectParam->get(), speedParam->get(), depthParam->get(), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        const double mixDc = ch.pre.voltage (ch.pMix);
        ch.power.setSource (ch.wSrcCf, mixDc);
        dcOk = ch.power.prepare (newSampleRate) && dcOk;
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.failStreak = 0;
        ch.bbdBuffer.fill (0.0f);
        ch.bbdWritePos = 0;
        ch.bbdReadPos = 0.0;
        ch.bbdClockPhase = 0.0;
        ch.bbdHeldDelaySamples = bbdCenterDelayMs * 0.001 * newSampleRate;
        ch.bbdLfoPhase = 0.0;
        ch.bbdPreLpState = 0.0;
        ch.bbdPostLpState = 0.0;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void JC120StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedSpeed.setTargetValue (speedParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int inputChoice = juce::roundToInt (inputParam->get());
    const bool inputConnects1 = inputChoice != 1;
    const bool inputConnects2 = inputChoice != 0;
    const double distortionKnob = distortionParam->get();
    const int effectChoice = juce::roundToInt (effectParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float mi = smoothedMiddle.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float sp = smoothedSpeed.getNextValue();
        const float de = smoothedDepth.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, tr, ba, mi, distortionKnob, (double) effectChoice, sp, de, speakerChoice });
        }

        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);
        const double speedHz = 0.3 + 4.5 * pots::audio ((double) sp);
        const double depthAmount = (double) de;

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn1, inputConnects1 ? x : 0.0);
            ch.pre.setSource (ch.pSrcIn2, inputConnects2 ? x : 0.0);
            const bool okPre = ch.pre.solveSample();

            const double mixV = ch.pre.voltage (ch.pMix);
            ch.power.setSource (ch.wSrcCf, mixV);
            const bool okPower = ch.power.solveSample();
            const bool ok = okPre && okPower;

            const double speakerVolts = ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 80.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            const bool overallOk = ok && sane;

            if (overallOk)
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
            if (overallOk)
            {
                double preFx = speakerVolts * outputScale * outGain;
                if (effectChoice != 0)
                    preFx = bbdChorus (ch, preFx, speedHz, depthAmount, effectChoice == 2);
                out = preFx;
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
                lastSampleOk = overallOk;
                if (! overallOk)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

double JC120StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::channelOnePlate: return ch.pre.voltage (ch.pPlate1);
        case Probe::channelTwoPlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::mixNode: return ch.pre.voltage (ch.pMix);
        case Probe::driverBase: return ch.power.voltage (ch.wDriverBase);
        case Probe::outputNodeA: return ch.power.voltage (ch.wOutA);
        case Probe::outputNodeB: return ch.power.voltage (ch.wOutB);
        case Probe::speaker: return ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

void JC120StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    resistiveLoadForced = true;
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e9);
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-12);
        ch.power.setInductorInverse (ch.grpSpkLe, { 0.0 });
        ch.power.setInductorInverse (ch.grpSpkLp, { 0.0 });
    }
}

void JC120StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
