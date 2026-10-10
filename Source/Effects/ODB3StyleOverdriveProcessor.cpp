#include "ODB3StyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"
#include "TheveninCombine.h"

#include <IconData.h>

namespace openguitarmultifx
{

ODB3StyleOverdriveProcessor::ODB3StyleOverdriveProcessor()
{
    auto makeParam = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    // Front-panel order on the real pedal: LEVEL, HIGH, LOW, BALANCE, GAIN.
    auto levelParam = makeParam ("odb3_level", "Level");
    auto highParam = makeParam ("odb3_high", "High");
    auto lowParam = makeParam ("odb3_low", "Low");
    auto balanceParam = makeParam ("odb3_balance", "Balance");
    auto gainParam = makeParam ("odb3_gain", "Gain");

    level = levelParam.get();
    high = highParam.get();
    low = lowParam.get();
    balance = balanceParam.get();
    gain = gainParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "odb3", "ODB-3-Style Overdrive", "|", std::move (levelParam));
    group->addChild (std::move (highParam));
    group->addChild (std::move (lowParam));
    group->addChild (std::move (balanceParam));
    group->addChild (std::move (gainParam));
    parameters = std::move (group);

    for (auto& ch : channels)
    {
        ch.clipper.setParameters (diodeSaturationCurrent, diodeThermalVoltage * diodeIdealityFactor, 1.0, 1.0);
        ch.ledPair.setParameters (ledSaturationCurrent, ledNVt, 1.0, 1.0);
    }
}

void ODB3StyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    if (juce::exactlyEqual (settledSampleRate, newSampleRate))
        return;
    settledSampleRate = newSampleRate;

    solveCount = 0;
    solveFailures = 0;

    smoothedRGain.reset (newSampleRate, 0.02);
    smoothedRGain.setCurrentAndTargetValue (juce::jmax (1.0f, gainMax * (float) pots::audio (gain->get())));
    smoothedRBalance.reset (newSampleRate, 0.02);
    smoothedRBalance.setCurrentAndTargetValue (juce::jmax (1.0f, balanceSum * balance->get()));
    smoothedRLowWiperToPlus.reset (newSampleRate, 0.02);
    smoothedRLowWiperToPlus.setCurrentAndTargetValue (juce::jmax (1.0f, toneMax * low->get()));
    smoothedRHighWiperToPlus.reset (newSampleRate, 0.02);
    smoothedRHighWiperToPlus.setCurrentAndTargetValue (juce::jmax (1.0f, toneMax * high->get()));
    smoothedRLevelWiperToBottom.reset (newSampleRate, 0.02);
    smoothedRLevelWiperToBottom.setCurrentAndTargetValue (juce::jmax (1.0f, levelMax * (float) pots::audio (level->get())));

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c2.prepare (newSampleRate, c2Value);
        ch.c3.prepare (newSampleRate, c3Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c5.prepare (newSampleRate, c5Value);
        ch.cLow.prepare (newSampleRate, cLowValue);
        ch.cHigh.prepare (newSampleRate, cHighValue);
        ch.cLp.prepare (newSampleRate, cLpValue);
        ch.c7.prepare (newSampleRate, c7Value);
        ch.c8.prepare (newSampleRate, c8Value);
        ch.c9.prepare (newSampleRate, c9Value);
        ch.clipper.reset (0.0);
        ch.ledPair.reset (0.0);
    }

    if (newSampleRate > 0.0)
    {
        const int settleBlockSize = 512;
        juce::AudioBuffer<float> silence (2, settleBlockSize);
        int samplesRemaining = (int) (newSampleRate * 6.0);

        while (samplesRemaining > 0)
        {
            const int thisBlock = juce::jmin (settleBlockSize, samplesRemaining);
            silence.clear();
            juce::AudioBuffer<float> silenceView (silence.getArrayOfWritePointers(), 2, thisBlock);
            process (silenceView);
            samplesRemaining -= thisBlock;
        }

        solveCount = 0;
        solveFailures = 0;
    }
}

void ODB3StyleOverdriveProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.c1.reset();
        ch.c2.reset();
        ch.c3.reset();
        ch.c4.reset();
        ch.c5.reset();
        ch.cLow.reset();
        ch.cHigh.reset();
        ch.cLp.reset();
        ch.c7.reset();
        ch.c8.reset();
        ch.c9.reset();
    }
}

void ODB3StyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedRGain.setTargetValue (juce::jmax (1.0f, gainMax * (float) pots::audio (gain->get())));
    smoothedRBalance.setTargetValue (juce::jmax (1.0f, balanceSum * balance->get()));
    smoothedRLowWiperToPlus.setTargetValue (juce::jmax (1.0f, toneMax * low->get()));
    smoothedRHighWiperToPlus.setTargetValue (juce::jmax (1.0f, toneMax * high->get()));
    smoothedRLevelWiperToBottom.setTargetValue (juce::jmax (1.0f, levelMax * (float) pots::audio (level->get())));

    const double rOut = 1.0 / (1.0 / 100.0e3 + 1.0 / (double) outputLoadResistance);
    const double rSeriesOut = 470.0;

    // The LED shunt clipper's downstream load: the blend summer's input leg, approximated at its
    // midband value (the summer is linear; the approximation only moves the clipping knee slightly).
    constexpr double rLedLoad = 47.0e3;

    for (int i = 0; i < numSamples; ++i)
    {
        const double rGain = (double) smoothedRGain.getNextValue();
        const double rFeedback = (double) r6 + rGain;

        const double rBalWet = juce::jmax (1.0, (double) balanceSum + 1.0 - (double) smoothedRBalance.getNextValue());
        const double rBalDry = juce::jmax (1.0, (double) smoothedRBalance.getNextValue());

        const double rLowAW = juce::jmax (1.0, (double) smoothedRLowWiperToPlus.getNextValue());
        const double rLowWB = juce::jmax (1.0, (double) toneMax - rLowAW);
        const double rHighAW = juce::jmax (1.0, (double) smoothedRHighWiperToPlus.getNextValue());
        const double rHighWB = juce::jmax (1.0, (double) toneMax - rHighAW);

        const double rLevelWiperToBottom = juce::jmax (1.0, (double) smoothedRLevelWiperToBottom.getNextValue());
        const double rLevelTopToWiper = juce::jmax (1.0, (double) levelMax - rLevelWiperToBottom);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);
            const double x = (double) data[i];

            // ---- Q1: the input buffer (ideal follower, same as the TS family) ----
            const double reqC1 = s.c1.getEquivalentResistance();
            const double histC1 = s.c1.getHistoryVoltage();

            const Thevenin q1Base = combineParallel (Thevenin { r1 + reqC1, x - histC1 },
                                                   Thevenin { r2, (double) bias });

            const double reqC2 = s.c2.getEquivalentResistance();
            const double histC2 = s.c2.getHistoryVoltage();
            // The emitter feeds op-amp 1's (+) node AND the clean path's low-pass.
            const double reqCLp = s.cLp.getEquivalentResistance();
            const double histCLp = s.cLp.getHistoryVoltage();
            const Thevenin q1Emitter = combineParallel (
                combineParallel (Thevenin { r3, 0.0 },
                                 Thevenin { reqC2 + r5, (double) bias + histC2 }),
                Thevenin { rLp + reqCLp, (double) bias + histCLp });

            const double q1Vb = q1Base.vth;
            const double q1Ve = q1Vb - followerDrop;
            s.debugQ1Vb = q1Vb;
            s.debugQ1Ve = q1Ve;

            const double iC1 = (x - histC1 - q1Vb) / (r1 + reqC1);
            s.c1.updateState ((iC1 * reqC1 + histC1), iC1);

            const double iC2 = (q1Ve - (double) bias - histC2) / (reqC2 + r5);
            const double opAmpPlus = (double) bias + iC2 * r5;
            s.c2.updateState ((iC2 * reqC2 + histC2), iC2);

            // The clean path's low-pass: the emitter drives R_lp into node L, C_lp to the bias rail.
            const double vLp = (q1Ve / rLp + ((double) bias + histCLp) / reqCLp)
                             / (1.0 / rLp + 1.0 / reqCLp);
            const double iCLp = (vLp - (double) bias - histCLp) / reqCLp;
            s.cLp.updateState ((iCLp * reqCLp + histCLp), iCLp);

            // ---- Op-amp 1: non-inverting clipper with the Gain pot ----
            const double reqC3 = s.c3.getEquivalentResistance();
            const double histC3 = s.c3.getHistoryVoltage();
            const double iFeedback = (opAmpPlus - histC3) / (reqC3 + r4);
            s.c3.updateState ((iFeedback * reqC3 + histC3), iFeedback);

            const double reqC4 = s.c4.getEquivalentResistance();
            const double histC4 = s.c4.getHistoryVoltage();
            const double gLinear = 1.0 / rFeedback + 1.0 / reqC4;
            const double feedbackRth = 1.0 / gLinear;
            const double feedbackVth = feedbackRth * (iFeedback + histC4 / reqC4);

            double feedbackVoltage;
            {
                const bool converged = s.clipper.solve (feedbackRth, feedbackVth, feedbackVoltage);
                ++solveCount;
                if (! converged)
                    ++solveFailures;
            }

            const double op1Out = opAmpPlus + feedbackVoltage;
            s.debugOp1Out = op1Out;
            const double iC4 = (feedbackVoltage - histC4) / reqC4;
            s.c4.updateState (feedbackVoltage, iC4);

            // ---- The LED shunt clipper: op-amp output through R -> LED pair to the bias rail,
            // with the blend summer's input leg as its downstream load. The diodes' port voltage is
            // referenced to the rail, so the solve runs on u = vNode - bias. ----
            const Thevenin ledSource = combineParallel (Thevenin { rSeriesLed, op1Out },
                                                      Thevenin { rLedLoad, (double) bias });
            double uLed;
            {
                const bool converged = s.ledPair.solve (ledSource.rth, ledSource.vth - (double) bias, uLed);
                ++solveCount;
                if (! converged)
                    ++solveFailures;
            }
            const double vLed = (double) bias + uLed;

            // ---- Balance: the driven signal against the low-passed clean feed (resistive summer).
            // The dry signal is re-centred on the bias rail first. ----
            const Thevenin wetBranch { rBalWet, vLed };
            const Thevenin dryBranch { rDry + rBalDry, vLp + 0.62 };
            const Thevenin mixNode = combineParallel (wetBranch, dryBranch);

            // ---- Low/High tone stage (the bridged-feedback Baxandall arrangement, same as the TS9B) ----
            const double reqC5 = s.c5.getEquivalentResistance();
            const double histC5 = s.c5.getHistoryVoltage();
            const double reqCLow = s.cLow.getEquivalentResistance();
            const double histCLow = s.cLow.getHistoryVoltage();
            const double reqCHigh = s.cHigh.getEquivalentResistance();
            const double histCHigh = s.cHigh.getHistoryVoltage();

            const double gPotL = 1.0 / rLowAW + 1.0 / rLowWB;
            const double gShuntL = 1.0 / (reqCLow + r8l);
            const double alphaL = gPotL / (gPotL + gShuntL);
            const double betaL = histCLow * gShuntL / (gPotL + gShuntL);

            const double gPotH = 1.0 / rHighAW + 1.0 / rHighWB;
            const double gShuntH = 1.0 / (reqCHigh + r8h);
            const double alphaH = gPotH / (gPotH + gShuntH);
            const double betaH = histCHigh * gShuntH / (gPotH + gShuntH);

            const double numerator = mixNode.vth / r7 + histC5 / reqC5 + (double) bias / r10
                                   + betaL / rLowAW + betaH / rHighAW;
            const double denominator = 1.0 / r7 + 1.0 / reqC5 + 1.0 / r10
                                     + (1.0 - alphaL) / rLowAW + (1.0 - alphaH) / rHighAW;
            const double vA = numerator / denominator;
            const double vWl = alphaL * vA + betaL;
            const double vWh = alphaH * vA + betaH;

            const double op2Out = vA + r9 * ((vA - vWl) / rLowWB + (vA - vWh) / rHighWB);

            const double iC5 = (vA - histC5) / reqC5;
            s.c5.updateState (vA, iC5);
            const double iCLow = (vWl - histCLow) / (reqCLow + r8l);
            s.cLow.updateState ((iCLow * reqCLow + histCLow), iCLow);
            const double iCHigh = (vWh - histCHigh) / (reqCHigh + r8h);
            s.cHigh.updateState ((iCHigh * reqCHigh + histCHigh), iCHigh);

            // ---- Level pot + output buffer ----
            const double reqC7 = s.c7.getEquivalentResistance();
            const double histC7 = s.c7.getHistoryVoltage();
            const double reqC8 = s.c8.getEquivalentResistance();
            const double histC8 = s.c8.getHistoryVoltage();
            const double reqC9 = s.c9.getEquivalentResistance();
            const double histC9 = s.c9.getHistoryVoltage();

            const Thevenin levelTop { reqC7 + r11 + rLevelTopToWiper, op2Out - histC7 };
            const Thevenin levelBottom { rLevelWiperToBottom, (double) bias };
            const Thevenin wiper = combineParallel (levelTop, levelBottom);

            const Thevenin d { wiper.rth + closedSwitchResistance, wiper.vth };
            const Thevenin q2Base = combineParallel (Thevenin { d.rth + reqC8, d.vth - histC8 },
                                                   Thevenin { r12, (double) bias });

            const Thevenin q2Emitter = combineParallel (Thevenin { r13, 0.0 },
                                                      Thevenin { rSeriesOut + reqC9 + rOut, histC9 });

            const double q2Vb = q2Base.vth;
            const double q2Ve = q2Vb - followerDrop;

            const double iC8 = (d.vth - histC8 - q2Vb) / (d.rth + reqC8);
            const double vD = d.vth - iC8 * d.rth;
            s.c8.updateState ((iC8 * reqC8 + histC8), iC8);

            const double iSwitch = (wiper.vth - vD) / (wiper.rth + closedSwitchResistance);
            const double wiperVoltage = wiper.vth - iSwitch * wiper.rth;
            const double iC7 = iSwitch + (wiperVoltage - (double) bias) / rLevelWiperToBottom;
            s.c7.updateState ((iC7 * reqC7 + histC7), iC7);

            const double iC9 = (q2Ve - histC9) / (rSeriesOut + reqC9 + rOut);
            s.c9.updateState ((iC9 * reqC9 + histC9), iC9);

            data[i] = (float) (iC9 * rOut);
        }
    }
}

void ODB3StyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
