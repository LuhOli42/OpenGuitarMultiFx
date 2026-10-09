#include "EffectRegistry.h"
#include "Effects/MarkIICPlusStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class MarkIICPlusStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    MarkIICPlusStyleAmplifierProcessorTests() : juce::UnitTest ("MarkIICPlusStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = MarkIICPlusStyleAmplifierProcessor::Probe;

    static void setParam (MarkIICPlusStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        MarkIICPlusStyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            MarkIICPlusStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPi(), 1) + ", V3 rail " + juce::String (amp.railV3(), 1)
                        + ", V2 rail " + juce::String (amp.railV2(), 1) + ", V1 rail " + juce::String (amp.railV1(), 1)
                        + ", V1a plate " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V1b plate " + juce::String (amp.debugVoltage (P::v1bPlate), 1)
                        + ", V2a plate " + juce::String (amp.debugVoltage (P::v2aPlate), 1)
                        + ", V2b plate " + juce::String (amp.debugVoltage (P::v2bPlate), 1)
                        + ", V3a plate " + juce::String (amp.debugVoltage (P::v3aPlate), 1)
                        + ", follower out " + juce::String (amp.debugVoltage (P::followerOut), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1));
            expect (amp.railPlates() > 400.0 && amp.railPlates() < 520.0);
            expect (amp.railV1() > 150.0 && amp.railV1() < 450.0);
            expect (amp.railV2() > 150.0 && amp.railV2() < 450.0);
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            MarkIICPlusStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.01);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note: finite, bounded, converges");
        {
            MarkIICPlusStyleAmplifierProcessor amp;
            setParam (amp, "mk2c_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (5);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            long long n = 0;
            bool finite = true;
            double peak = 0.0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.6 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.0); level = 0.1 + 0.2 * rnd.nextDouble(); age = 0.0; }
                    age += 1.0 / sr;
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::sin (phase) * std::exp (-age * 2.0));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("pluck: failure rate " + juce::String (amp.getSolveFailureRate(), 6)
                        + ", recoveries " + juce::String (amp.debugRecoveries()) + ", peak " + juce::String (peak, 2));
            expect (finite);
            expectLessThan (amp.getSolveFailureRate(), 2.0e-3);
        }

        beginTest ("page 2: Speaker is a preset parameter, its own sub-group");
        {
            MarkIICPlusStyleAmplifierProcessor amp;
            int page2Count = 0;
            bool foundSpeaker = false;
            const auto pages = amp.getParameterPages();
            for (auto* f : pages[1])
            {
                ++page2Count;
                if (f->paramID == "mk2c_speaker")
                    foundSpeaker = true;
            }
            expect (page2Count == 5, "Power Drive, Bias, Tube Feel, Speaker, Output");
            expect (foundSpeaker);
        }

        {
            beginTest ("reducedOrder power stage: finite output, zero failures (reference model unreliable — see JCM800 pattern)");
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = true;
            MarkIICPlusStyleAmplifierProcessor red;
            setParam (red, "mk2c_gain", 0.5f);
            setParam (red, "mk2c_power", 1.0f);
            red.prepare (sr, 128, 2);

            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 5.0e-3, 0.02, 0.05, 0.1, 0.2, 0.4, 0.6 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double redOutSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                MarkIICPlusStyleAmplifierProcessor::reducedOrder = true;
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    red.process (one);
                    if (n >= measureFrom) { const double y = red.debugVoltage (P::speaker); redOutSumSq += y * y; ++n2; }
                }
                const double redRms = std::sqrt (redOutSumSq / (double) juce::jmax (1LL, n2));
                logMessage ("level " + juce::String (level, 4) + ": reduced " + juce::String (redRms, 3)
                            + " Vrms, fails " + juce::String (red.getSolveFailureRate(), 6) + ", recov " + juce::String (red.debugRecoveries()));
                expect (std::isfinite (redRms) && redRms > 0.1);
                expectLessThan (red.getSolveFailureRate(), 1.0e-6);
                expect (red.debugRecoveries() == 0);
            }
        }

        {
            beginTest ("reducedOrder (shipped default): a plucked-note sequence stays bounded and reasonably loud");
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = true;
            MarkIICPlusStyleAmplifierProcessor amp;
            setParam (amp, "mk2c_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (5);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            long long n = 0;
            bool finite = true;
            double peak = 0.0, sumSq = 0.0;
            long long n2 = 0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.6 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.0); level = 0.1 + 0.2 * rnd.nextDouble(); age = 0.0; }
                    age += 1.0 / sr;
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::sin (phase) * std::exp (-age * 2.0));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double y = (double) buf.getSample (0, i);
                    finite = finite && std::isfinite (y);
                    peak = juce::jmax (peak, std::abs (y));
                    sumSq += y * y; ++n2;
                }
            }
            logMessage ("reducedOrder pluck sequence: peak " + juce::String (peak, 3) + ", rms " + juce::String (std::sqrt (sumSq / (double) juce::jmax (1LL, n2)), 4)
                        + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (finite);
            expectGreaterThan (peak, 0.05, "the shipped default should not be near-silent for a healthy pluck");
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: 4 / 8 / 16 ohm sound equally loud");
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = true;
            const auto rmsAt = [] (float speaker)
            {
                MarkIICPlusStyleAmplifierProcessor amp;
                setParam (amp, "mk2c_gain", 0.5f);
                setParam (amp, "mk2c_speaker", speaker);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> one (2, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                double sumSq = 0.0;
                long long n2 = 0;
                const long long total = (long long) (0.7 * sr), measureFrom = total - (long long) (0.1 * sr);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (0.05 * std::sin (twoPi * 200.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom) { const double y = amp.debugVoltage (P::speaker); sumSq += y * y; ++n2; }
                }
                return std::sqrt (sumSq / (double) juce::jmax (1LL, n2));
            };
            const double r4 = rmsAt (0.0f), r8 = rmsAt (1.0f), r16 = rmsAt (2.0f);
            logMessage ("reducedOrder speaker volts at 4 / 8 / 16 ohm: " + juce::String (r4, 2) + " / " + juce::String (r8, 2) + " / " + juce::String (r16, 2) + " V rms");
            expectLessThan (std::abs (20.0 * std::log10 (r4 / r8)), 0.1);
            expectLessThan (std::abs (20.0 * std::log10 (r16 / r8)), 0.1);
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: worst-block cost under a hot-pedal stress");
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = true;
            MarkIICPlusStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            double seconds = 0.0, worstPct = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (1.0 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.0); level = 0.5 + 0.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::sin (phase));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                const auto before = std::chrono::high_resolution_clock::now();
                amp.process (buf);
                const auto after = std::chrono::high_resolution_clock::now();
                const double elapsed = std::chrono::duration<double> (after - before).count();
                const double blockBudget = 128.0 / sr;
                const double pct = 100.0 * elapsed / blockBudget;
                seconds += elapsed;
                if (pct > worstPct)
                    worstPct = pct;
            }
            const double avgPct = 100.0 * seconds / 10.0;
            logMessage ("reducedOrder cost: avg " + juce::String (avgPct, 2) + "%, worst block " + juce::String (worstPct, 1)
                        + "%, failures " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            if (! testEnvironment::underEmulation())
                expectLessThan (avgPct, 15.0);
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("MK2C_SETTLE_DIAG", "").isNotEmpty())
        {
            beginTest ("MK2C_SETTLE_DIAG: 10 s silence settle with per-second peaks");
            MarkIICPlusStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            const int blocksPerSec = (int) (sr / 128);
            for (int sec = 0; sec < 10; ++sec)
            {
                double secPeak = 0.0;
                for (int b = 0; b < blocksPerSec; ++b)
                {
                    amp.process (buf);
                    for (int i = 0; i < 128; ++i)
                        secPeak = juce::jmax (secPeak, (double) std::abs (buf.getSample (0, i)));
                }
                logMessage ("  sec " + juce::String (sec) + ": peak " + juce::String (secPeak, 8));
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("MK2C_POWERCAL", "").isNotEmpty())
        {
            beginTest ("MK2C_POWERCAL: calibration sweep for the reduced-order power stage");
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = false;
            MarkIICPlusStyleAmplifierProcessor amp;
            setParam (amp, "mk2c_gain", 0.5f);
            setParam (amp, "mk2c_power", 1.0f);
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            logMessage ("level, toneStackRms, speakerRms, ratio");
            for (double level : { 1.0e-3, 2.0e-3, 5.0e-3, 0.01, 0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.0 })
            {
                const long long total = (long long) (2.0 * sr);
                const long long measureFrom = total - (long long) (0.2 * sr);
                double tsSumSq = 0.0, spkSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom)
                    {
                        const double ts = amp.debugVoltage (P::toneStackOut);
                        const double sp = amp.debugVoltage (P::speaker);
                        tsSumSq += ts * ts;
                        spkSumSq += sp * sp;
                        ++n2;
                    }
                }
                const double tsRms = std::sqrt (tsSumSq / (double) juce::jmax (1LL, n2));
                const double spRms = std::sqrt (spkSumSq / (double) juce::jmax (1LL, n2));
                const double ratio = spRms / juce::jmax (1.0e-12, tsRms);
                logMessage (juce::String (level, 4) + ", " + juce::String (tsRms, 6) + ", " + juce::String (spRms, 4) + ", " + juce::String (ratio, 4));
            }
            MarkIICPlusStyleAmplifierProcessor::reducedOrder = false;
        }
    }
};

static MarkIICPlusStyleAmplifierProcessorTests mk2cTests;

} // namespace openguitarmultifx
