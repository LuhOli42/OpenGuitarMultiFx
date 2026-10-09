#include "EffectRegistry.h"
#include "Effects/SuperLeadStyleAmplifierProcessor.h"
#include "Effects/TubeModels.h"
#include "Effects/TubeAmpCommon.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <limits>

namespace openguitarmultifx
{

/** The Super Lead-style amplifier (Marshall 1959SLP drawings; docs/circuits/SuperLead1959.md). */
class SuperLeadStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    SuperLeadStyleAmplifierProcessorTests() : juce::UnitTest ("SuperLeadStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = SuperLeadStyleAmplifierProcessor::Probe;

    static void setParam (SuperLeadStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    /** RMS (and peak) of `probe` over the last `cycles` cycles of a sine at `freq`/`amplitude` after a settling time. */
    static void sineRun (SuperLeadStyleAmplifierProcessor& amp, double freq, double amplitude, P probe, double& rms, double& peak, double seconds = 0.5)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) (seconds * sr);
        const long long len = (long long) std::llround (std::floor (0.3 * freq) * sr / freq);
        juce::AudioBuffer<float> one (2, 1);
        double sumSq = 0.0, sum = 0.0;
        peak = 0.0;
        long long n2 = 0;
        for (long long n = 0; n < total; ++n)
        {
            const float v = (float) (amplitude * std::sin (twoPi * freq * (double) n / sr));
            one.setSample (0, 0, v);
            one.setSample (1, 0, v);
            amp.process (one);
            if (n >= total - len)
            {
                const double y = amp.debugVoltage (probe);
                sum += y;
                sumSq += y * y;
                peak = juce::jmax (peak, std::abs (y));
                ++n2;
            }
        }
        const double mean = sum / (double) juce::jmax (1LL, n2);
        rms = std::sqrt (juce::jmax (0.0, sumSq / (double) juce::jmax (1LL, n2) - mean * mean));
    }

    void runTest() override
    {
        // Deterministic regardless of process history: EffectRegistry.cpp flips this static to true for the real app, and
        // some other test/bench that goes through the registry could have run first in this same process. Every test below
        // (except SL_REDUCED / SL_COST_REDUCED, which set it explicitly and restore it) assumes the full reference netlist.
        SuperLeadStyleAmplifierProcessor::reducedOrder = false;

        if (juce::SystemStats::getEnvironmentVariable ("SL_EXPLORE", {}).isNotEmpty())
        {
            beginTest ("explore (dev only)");
            for (double amp_in : { 0.01, 0.03, 0.1, 0.3 })
            {
                SuperLeadStyleAmplifierProcessor amp;
                amp.prepare (sr, 128, 2);
                setParam (amp, "sl_loud2", 0.7f);
                setParam (amp, "sl_loud1", 0.7f);
                double r = 0, pk = 0, rf = 0, pkf = 0, rt = 0, pkt = 0;
                sineRun (amp, 1000.0, amp_in, P::speaker, r, pk);
                sineRun (amp, 1000.0, amp_in, P::followerOut, rf, pkf);
                sineRun (amp, 1000.0, amp_in, P::toneStackOut, rt, pkt);
                logMessage ("in " + juce::String (amp_in, 3) + " V: follower " + juce::String (rf, 2) + " Vrms, tone out " + juce::String (rt, 3) + " Vrms, speaker "
                            + juce::String (r, 2) + " Vrms (" + juce::String (r * r / 16.0, 1) + " W), peak " + juce::String (pk, 1)
                            + " ; failures " + juce::String (amp.getSolveFailureRate(), 6) + ", iterations " + juce::String (amp.debugIterations (0), 2) + "/" + juce::String (amp.debugIterations (1), 2)
                            + ", recoveries " + juce::String (amp.debugRecoveries()));
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_EXPLORE_POWER", {}).isNotEmpty())
        {
            beginTest ("explore power (dev only)");
            for (double amp_in : { 0.008, 0.012, 0.016, 0.02, 0.025, 0.03, 0.04, 0.06, 0.1 })
            {
                SuperLeadStyleAmplifierProcessor amp;
                amp.prepare (sr, 128, 2);
                setParam (amp, "sl_loud2", 0.5f);
                setParam (amp, "sl_input", 0.0f);
                const double freq = 1000.0, twoPi = 2.0 * juce::MathConstants<double>::pi;
                const long long total = (long long) (0.6 * sr);
                juce::AudioBuffer<float> one (2, 1);
                std::vector<double> y;
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (amp_in * std::sin (twoPi * freq * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= total - 4800)
                        y.push_back (amp.debugVoltage (P::speaker));
                }
                double sumSq = 0.0, mean = 0.0;
                for (double v : y) mean += v;
                mean /= (double) y.size();
                for (double v : y) sumSq += (v - mean) * (v - mean);
                const double rms = std::sqrt (sumSq / (double) y.size());
                double h[6] {};
                for (int k = 1; k <= 5; ++k)
                {
                    double sc = 0.0, ss = 0.0;
                    for (size_t n = 0; n < y.size(); ++n)
                    {
                        sc += y[n] * std::cos (twoPi * freq * k * (double) n / sr);
                        ss += y[n] * std::sin (twoPi * freq * k * (double) n / sr);
                    }
                    h[k] = 2.0 * std::sqrt (sc * sc + ss * ss) / (double) y.size();
                }
                const double thd = std::sqrt (h[2] * h[2] + h[3] * h[3] + h[4] * h[4] + h[5] * h[5]) / h[1];
                logMessage ("in " + juce::String (amp_in * 1000.0, 2) + " mV: speaker " + juce::String (rms, 2) + " Vrms = " + juce::String (rms * rms / 16.0, 1) + " W, THD(2-5) "
                            + juce::String (100.0 * thd, 1) + " %, plates " + juce::String (amp.plateCurrentTotal() * 1000.0, 0) + " mA, rail " + juce::String (amp.railPlates(), 0) + " V");
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_EXPLORE_TONE", {}).isNotEmpty())
        {
            beginTest ("explore tone (dev only)");
            struct Case { const char* name; float treble, middle, bass; };
            const Case cases[] = { { "all noon", 0.5f, 0.5f, 0.5f }, { "treble 0", 0.0f, 0.5f, 0.5f }, { "treble 1", 1.0f, 0.5f, 0.5f }, { "mid 0", 0.5f, 0.0f, 0.5f }, { "mid 1", 0.5f, 1.0f, 0.5f },
                                   { "bass 0", 0.5f, 0.5f, 0.0f }, { "bass 1", 0.5f, 0.5f, 1.0f }, { "all 0", 0.0f, 0.0f, 0.0f }, { "all 1", 1.0f, 1.0f, 1.0f }, { "flat-ish 0,1,0", 0.0f, 1.0f, 0.0f } };
            for (const auto& cs : cases)
            {
                juce::String line;
                for (double f : { 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
                {
                    SuperLeadStyleAmplifierProcessor amp;
                    amp.prepare (sr, 128, 2);
                    setParam (amp, "sl_treble", cs.treble);
                    setParam (amp, "sl_middle", cs.middle);
                    setParam (amp, "sl_bass", cs.bass);
                    setParam (amp, "sl_input", 0.0f);
                    setParam (amp, "sl_loud2", 0.3f);
                    double rf, pf, rt, pt;
                    sineRun (amp, f, 0.0002, P::followerOut, rf, pf, 0.6);
                    SuperLeadStyleAmplifierProcessor amp2;
                    amp2.prepare (sr, 128, 2);
                    setParam (amp2, "sl_treble", cs.treble);
                    setParam (amp2, "sl_middle", cs.middle);
                    setParam (amp2, "sl_bass", cs.bass);
                    setParam (amp2, "sl_input", 0.0f);
                    setParam (amp2, "sl_loud2", 0.3f);
                    sineRun (amp2, f, 0.0002, P::toneStackOut, rt, pt, 0.6);
                    line << juce::String (f, 0) << ": " << juce::String (20.0 * std::log10 (juce::jmax (1.0e-12, rt) / juce::jmax (1.0e-12, rf)), 1) << "  ";
                }
                logMessage (juce::String (cs.name) + " (tone out / follower, dB): " + line);
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_EXPLORE_IDLE", {}).isNotEmpty())
        {
            beginTest ("explore idle drift (dev only)");
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            const int blockSize = juce::SystemStats::getEnvironmentVariable ("SL_IDLE_BLOCK", "128").getIntValue();
            const double inAmp = juce::SystemStats::getEnvironmentVariable ("SL_IDLE_AMP", "0").getDoubleValue();
            juce::AudioBuffer<float> buf (2, blockSize);
            long long sampleIndex = 0;
            for (int b = 0; b <= 400 * 128 / blockSize; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++sampleIndex)
                {
                    const float v = (float) (inAmp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) sampleIndex / sr));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                if (b % (25 * 128 / blockSize) == 0)
                    logMessage ("t " + juce::String (b * 128.0 / sr, 3) + " s: plateA " + juce::String (amp.debugVoltage (P::powerPlateA), 1) + " rail " + juce::String (amp.railPlates(), 1)
                                + " screens " + juce::String (amp.railScreens(), 1) + " PI " + juce::String (amp.railPhaseInverter(), 1) + " PIplateA " + juce::String (amp.debugVoltage (P::phaseInverterPlateA), 1)
                                + " bias " + juce::String (amp.debugVoltage (P::biasNode), 2) + " gridA " + juce::String (amp.debugVoltage (P::powerGridA), 2)
                                + " Iplate " + juce::String (amp.plateCurrentTotal() * 1000.0, 1) + " spk " + juce::String (amp.debugVoltage (P::speaker), 2) + " fails " + juce::String (amp.getSolveFailureRate(), 6));
                amp.process (buf);
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_EXPLORE_HOT", {}).isNotEmpty())
        {
            beginTest ("explore hot events (dev only)");
            SuperLeadStyleAmplifierProcessor amp;
            if (juce::SystemStats::getEnvironmentVariable ("SL_HOT_INPUT", {}).isNotEmpty())
                setParam (amp, "sl_input", (float) juce::SystemStats::getEnvironmentVariable ("SL_HOT_INPUT", "1").getDoubleValue());
            amp.prepare (sr, 128, 2);
            if (juce::SystemStats::getEnvironmentVariable ("SL_HOT_OPEN", {}).isNotEmpty())
                amp.debugSetFeedbackResistance (juce::SystemStats::getEnvironmentVariable ("SL_HOT_OPEN", "1e9").getDoubleValue());
            juce::AudioBuffer<float> one (2, 1);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            struct Row { double spk, pp1, pp2, g, pgA, pia, pib, tail, fb, tone; };
            std::vector<Row> ring (24);
            long long n = 0, logged = 0;
            for (long long i = 0; i < (long long) (10.0 * sr) && logged < 3; ++i, ++n)
            {
                if (n % (long long) (0.5 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                const float v = (float) (level * std::tanh (8.0 * std::sin (phase)));
                one.setSample (0, 0, v);
                one.setSample (1, 0, v);
                amp.process (one);
                ring[(size_t) (n % 24)] = { amp.debugVoltage (P::speaker), amp.debugVoltage (P::powerPlateA), amp.debugVoltage (P::powerPlateB), amp.debugVoltage (P::powerGridA),
                                            amp.debugVoltage (P::phaseInverterGrid), amp.debugVoltage (P::phaseInverterPlateA),
                                            amp.debugVoltage (P::phaseInverterPlateB), amp.debugVoltage (P::phaseInverterTail), amp.debugVoltage (P::feedbackNode), amp.debugVoltage (P::toneStackOut) };
                if ((std::getenv ("SL_HOT_FAIL") != nullptr ? ! amp.debugLastSampleOk() : std::abs (ring[(size_t) (n % 24)].spk) > 150.0) && (n - logged * 100000) > 0)
                {
                    juce::String line = "event at sample " + juce::String (n) + " (f0 " + juce::String (f0, 0) + ", level " + juce::String (level, 2) + "):\n";
                    for (int k = 23; k >= 0; --k)
                    {
                        const auto& r = ring[(size_t) (((n - k) % 24 + 24) % 24)];
                        line << "   -" << juce::String (k) << ": spk " << juce::String (r.spk, 1) << " pp1 " << juce::String (r.pp1, 0) << " pp2 " << juce::String (r.pp2, 0) << " gridA " << juce::String (r.g, 1)
                             << " piGrid " << juce::String (r.pgA, 1) << " piPlateA " << juce::String (r.pia, 0)
                             << " piPlateB " << juce::String (r.pib, 0) << " tail " << juce::String (r.tail, 1) << " F " << juce::String (r.fb, 1) << " tone " << juce::String (r.tone, 1) << "\n";
                    }
                    logMessage (line);
                    ++logged;
                    i += 20000;
                    n += 20000;
                }
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_HOT_RATE", {}).isNotEmpty())
        {
            beginTest ("hot pedal at another sample rate (dev only)");
            const double rate = juce::SystemStats::getEnvironmentVariable ("SL_HOT_RATE", "96000").getDoubleValue();
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (rate, 128, 2);
            if (juce::SystemStats::getEnvironmentVariable ("SL_HOT_OPEN", {}).isNotEmpty())
                amp.debugSetFeedbackResistance (juce::SystemStats::getEnvironmentVariable ("SL_HOT_OPEN", "1e9").getDoubleValue());
            if (juce::SystemStats::getEnvironmentVariable ("SL_HOT_INPUT", {}).isNotEmpty())
                setParam (amp, "sl_input", (float) juce::SystemStats::getEnvironmentVariable ("SL_HOT_INPUT", "1").getDoubleValue());
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            for (int b = 0; b < (int) (10.0 * rate / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.5 * rate) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / rate;
                    const float v = (float) (level * std::tanh (8.0 * std::sin (phase)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
            }
            logMessage ("rate " + juce::String (rate, 0) + ": power failures " + juce::String (amp.debugPowerFailures()) + ", sanity rejects " + juce::String (amp.debugSanityRejects())
                        + ", recoveries " + juce::String (amp.debugRecoveries()) + ", worst sane " + juce::String (amp.debugWorstSaneVolts(), 1) + ", iterations " + juce::String (amp.debugIterations (1), 2));
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_SOAK", {}).isNotEmpty())
        {
            beginTest ("soak explore (dev only)");
            for (float inputChoice : { 0.0f, 1.0f })
                for (double maxLevel : { 0.1, 0.3, 0.6, 1.0 })
                {
                    SuperLeadStyleAmplifierProcessor amp;
                    setParam (amp, "sl_input", inputChoice);
                    setParam (amp, "sl_loud1", 1.0f);
                    setParam (amp, "sl_loud2", 1.0f);
                    amp.prepare (sr, 128, 2);
                    juce::Random rnd (99);
                    juce::AudioBuffer<float> buf (2, 128);
                    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                    double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {}, peak = 0.0, sumSq = 0.0;
                    long long n = 0, count = 0;
                    for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
                    {
                        for (int i = 0; i < 128; ++i, ++n)
                        {
                            if (n % (long long) (0.45 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0); level = maxLevel * (0.3 + 0.7 * rnd.nextDouble()); age = 0.0; }
                            age += 1.0 / sr;
                            double v = 0.0;
                            for (int k = 1; k <= 8; ++k) { phase[k] += twoPi * f0 * k / sr; v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k)); }
                            const double attack = std::min (1.0, age / 0.002);
                            buf.setSample (0, i, (float) (v * level * attack));
                            buf.setSample (1, i, (float) (v * level * attack));
                        }
                        amp.process (buf);
                        for (int i = 0; i < 128; ++i)
                        {
                            const double y = buf.getSample (0, i);
                            peak = std::max (peak, std::abs (y));
                            sumSq += y * y;
                            ++count;
                        }
                    }
                    logMessage ("input " + juce::String (inputChoice, 0) + ", max level " + juce::String (maxLevel, 1) + ": failures " + juce::String (amp.getSolveFailureRate(), 6) + ", rejects "
                                + juce::String (amp.debugSanityRejects()) + ", recoveries " + juce::String (amp.debugRecoveries()) + ", rms " + juce::String (std::sqrt (sumSq / (double) count), 2)
                                + ", peak " + juce::String (peak, 2) + ", pre/power failures " + juce::String (amp.debugPreFailures()) + "/" + juce::String (amp.debugPowerFailures()));
                }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_ITERHIST", {}).isNotEmpty())
        {
            beginTest ("power-stage iteration histogram under a hot pedal (dev only)");
            SuperLeadStyleAmplifierProcessor amp;
            setParam (amp, "sl_input", 0.0f);
            setParam (amp, "sl_loud1", 0.8f);
            setParam (amp, "sl_loud2", 0.8f);
            amp.prepare (sr, 128, 2);
            // Diagnostic: open the global NFB loop to see whether the phase-inverter/feedback coupling (not the power
            // pentodes' own device nonlinearity) is what drives the expensive iteration tail.
            if (juce::SystemStats::getEnvironmentVariable ("SL_ITERHIST_OPEN", {}).isNotEmpty())
                amp.debugSetFeedbackResistance (1.0e12);
            juce::Random rnd (5);
            juce::AudioBuffer<float> buf (2, 1);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {};
            long long n = 0;
            int histogram[8] = {}; // buckets: <5, <10, <20, <30, <50, <100, <200, >=200
            const int bounds[7] = { 5, 10, 20, 30, 50, 100, 200 };
            int worst = 0;
            long long worstSample = -1;
            const long long total = (long long) (4.0 * sr);
            for (n = 0; n < total; ++n)
            {
                if (n % (long long) (0.45 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0); level = 2.0 * (0.3 + 0.7 * rnd.nextDouble()); age = 0.0; }
                age += 1.0 / sr;
                double v = 0.0;
                for (int k = 1; k <= 8; ++k) { phase[k] += twoPi * f0 * k / sr; v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k)); }
                const double x = 2.0 * std::tanh (8.0 * v * level * std::min (1.0, age / 0.002)); // approximates a hot pedal's clip in front of the amp
                buf.setSample (0, 0, (float) x);
                buf.setSample (1, 0, (float) x);
                amp.process (buf);
                const int iters = amp.debugLastPowerIterations();
                int bucket = 7;
                for (int bIdx = 0; bIdx < 7; ++bIdx)
                    if (iters < bounds[bIdx]) { bucket = bIdx; break; }
                ++histogram[bucket];
                if (iters > worst) { worst = iters; worstSample = n; }
            }
            logMessage ("power-stage iterations/sample over " + juce::String (total) + " samples: <5 " + juce::String (histogram[0]) + ", <10 " + juce::String (histogram[1])
                        + ", <20 " + juce::String (histogram[2]) + ", <30 " + juce::String (histogram[3]) + ", <50 " + juce::String (histogram[4]) + ", <100 " + juce::String (histogram[5])
                        + ", <200 " + juce::String (histogram[6]) + ", >=200 " + juce::String (histogram[7]) + " -- worst " + juce::String (worst) + " at sample " + juce::String (worstSample)
                        + ", recoveries " + juce::String (amp.debugRecoveries()) + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_TUBE", {}).isNotEmpty())
        {
            beginTest ("triode at negative plate voltage (dev only)");
            KorenTriode t;
            for (double vpk : { 200.0, 20.0, 5.0, 0.0, -5.0, -20.0, -60.0, -180.0 })
                for (double vgk : { -5.0, 0.0, 5.0, 20.0 })
                {
                    const auto o = t.evaluate (vgk, vpk);
                    logMessage ("vpk " + juce::String (vpk, 0) + " vgk " + juce::String (vgk, 0) + ": ip " + juce::String (o.ip * 1000.0, 4) + " mA, ig " + juce::String (o.ig * 1000.0, 4) + " mA, dip/dvpk " + juce::String (o.dip_dvpk * 1000.0, 5) + " mA/V");
                }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_COST", {}).isNotEmpty())
        {
            beginTest ("cost profile (dev only)");
            for (double maxLevel : { 0.1, 0.6 })
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 1.0f);
                setParam (amp, "sl_loud1", 0.8f);
                setParam (amp, "sl_loud2", 0.8f);
                amp.prepare (sr, 128, 2);
                juce::Random rnd (5);
                juce::AudioBuffer<float> buf (2, 128);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {};
                long long n = 0;
                double seconds = 0.0;
                struct BlockTime { double pct; int index; int rec; int fails; int rej; };
                std::vector<BlockTime> blockTimes;
                const int blocks = (int) (4.0 * sr / 128);
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        if (n % (long long) (0.45 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0); level = maxLevel * (0.3 + 0.7 * rnd.nextDouble()); age = 0.0; }
                        age += 1.0 / sr;
                        double v = 0.0;
                        for (int k = 1; k <= 8; ++k) { phase[k] += twoPi * f0 * k / sr; v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k)); }
                        buf.setSample (0, i, (float) (v * level * std::min (1.0, age / 0.002)));
                        buf.setSample (1, i, buf.getSample (0, i));
                    }
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    const int recBefore = amp.debugRecoveries();
                    const long long failBefore = amp.debugPreFailures() + amp.debugPowerFailures(), rejBefore = amp.debugSanityRejects();
                    amp.process (buf);
                    const double blockSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                    seconds += blockSeconds;
                    blockTimes.push_back ({ 100.0 * blockSeconds / (128.0 / sr), b, amp.debugRecoveries() - recBefore,
                                            (int) (amp.debugPreFailures() + amp.debugPowerFailures() - failBefore), (int) (amp.debugSanityRejects() - rejBefore) });
                }
                std::sort (blockTimes.begin(), blockTimes.end(), [] (const auto& a, const auto& b) { return a.pct > b.pct; });
                for (size_t k = 0; k < 6 && k < blockTimes.size(); ++k)
                    logMessage ("   worst block " + juce::String (k) + ": " + juce::String (blockTimes[k].pct, 0) + "% of the budget, block " + juce::String (blockTimes[k].index) + ", recoveries " + juce::String (blockTimes[k].rec)
                                + ", failed solves " + juce::String (blockTimes[k].fails) + ", sanity rejects " + juce::String (blockTimes[k].rej));
                logMessage ("level " + juce::String (maxLevel, 1) + ": " + juce::String (100.0 * seconds / (4.0), 2) + " % of a core, iterations pre/power " + juce::String (amp.debugIterations (0), 2) + "/" + juce::String (amp.debugIterations (1), 2)
                            + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_COST_REDUCED", {}).isNotEmpty())
        {
            // Same worst-block profile as SL_COST above, but with behavioralPowerStage() (2026-09-27): the point is
            // specifically the WORST block, not the average -- that is what the user's "never spike" requirement is about.
            beginTest ("cost profile, reducedOrder power stage (dev only)");
            SuperLeadStyleAmplifierProcessor::reducedOrder = true;
            for (double maxLevel : { 0.1, 0.6 })
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 1.0f);
                setParam (amp, "sl_loud1", 0.8f);
                setParam (amp, "sl_loud2", 0.8f);
                amp.prepare (sr, 128, 2);
                juce::Random rnd (5);
                juce::AudioBuffer<float> buf (2, 128);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {};
                long long n = 0;
                double seconds = 0.0, worstPct = 0.0;
                const int blocks = (int) (4.0 * sr / 128);
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        if (n % (long long) (0.45 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0); level = maxLevel * (0.3 + 0.7 * rnd.nextDouble()); age = 0.0; }
                        age += 1.0 / sr;
                        double v = 0.0;
                        for (int k = 1; k <= 8; ++k) { phase[k] += twoPi * f0 * k / sr; v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k)); }
                        buf.setSample (0, i, (float) (v * level * std::min (1.0, age / 0.002)));
                        buf.setSample (1, i, buf.getSample (0, i));
                    }
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    amp.process (buf);
                    const double blockSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                    seconds += blockSeconds;
                    worstPct = juce::jmax (worstPct, 100.0 * blockSeconds / (128.0 / sr));
                }
                logMessage ("level " + juce::String (maxLevel, 1) + ": " + juce::String (100.0 * seconds / 4.0, 2) + " % avg, worst block " + juce::String (worstPct, 1)
                            + " %, failures " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            }
            SuperLeadStyleAmplifierProcessor::reducedOrder = false;
        }

        // SL_BUDGET (a forced-every-sample deadline mode vs. the reference solve) was removed 2026-09-27 along with the
        // real-time guard it tested: see SuperLeadStyleAmplifierProcessor.h's note above debugSetResistiveLoad() for why.

        beginTest ("operating points: the rails come out where a plexi's do, and each stage sits between them");
        {
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged(), "DC solve converged");
            logMessage ("rails: plates " + juce::String (amp.railPlates(), 1) + ", screens " + juce::String (amp.railScreens(), 1)
                        + ", PI " + juce::String (amp.railPhaseInverter(), 1) + ", V2 " + juce::String (amp.railV2(), 1) + ", V1 " + juce::String (amp.railV1(), 1));
            expectWithinAbsoluteError (amp.railPlates(), 480.0, 3.0);
            expect (amp.railScreens() < amp.railPlates() && amp.railScreens() > amp.railPlates() - 8.0, "the screens sit just below the plates (the choke's drop)");
            expect (amp.railPhaseInverter() < amp.railScreens() - 60.0 && amp.railV2() < amp.railPhaseInverter() - 20.0 && amp.railV1() < amp.railV2() - 5.0,
                    "each filter node is lower than the one before it");
            const double perTube = amp.plateCurrentTotal() / 4.0 * 1000.0;
            logMessage ("idle plate current per tube " + juce::String (perTube, 1) + " mA, screens " + juce::String (amp.screenCurrentTotal() * 250.0, 2) + " mA per tube");
            expect (perTube > 28.0 && perTube < 45.0, "a plexi idles its EL34s at about 35 mA each");
            expectWithinAbsoluteError (amp.debugVoltage (P::biasNode), -55.0, 1.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::powerGridA), amp.debugVoltage (P::biasNode), 0.3);
            const double tail = amp.debugVoltage (P::phaseInverterTail);
            expect (tail > 20.0 && tail < 50.0, "phase inverter tail " + juce::String (tail, 1) + " V");
            expect (std::abs (amp.debugVoltage (P::phaseInverterPlateA) - amp.debugVoltage (P::phaseInverterPlateB)) < 25.0, "phase inverter plates are close");
            expect (amp.debugVoltage (P::followerOut) > 100.0 && amp.debugVoltage (P::followerOut) < 220.0, "the cathode follower's DC");
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            const double before = amp.debugVoltage (P::powerPlateA), railBefore = amp.railPlates(), phaseBefore = amp.debugVoltage (P::phaseInverterPlateA);
            juce::AudioBuffer<float> buf (2, 128);
            for (int b = 0; b < 400; ++b)
            {
                buf.clear(); // process() is in place: what comes back is the amp's output, not silence
                amp.process (buf);
            }
            expectWithinAbsoluteError (amp.debugVoltage (P::powerPlateA), before, 2.0);
            expectWithinAbsoluteError (amp.railPlates(), railBefore, 2.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::phaseInverterPlateA), phaseBefore, 2.0);
            expectEquals (amp.getSolveFailureRate(), 0.0);
        }

        beginTest ("page 2: Bias moves the idle current (hotter with the knob down) and stays stable at both ends");
        {
            double idle[3] {};
            int i = 0;
            for (float bias : { 0.0f, 0.5f, 1.0f })
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_bias", bias);
                amp.prepare (sr, 128, 2);
                expect (amp.dcConverged(), "DC solve converged");
                idle[i++] = amp.plateCurrentTotal() * 1000.0;
                double r, pk;
                sineRun (amp, 220.0, 0.03, P::speaker, r, pk);
                expect (std::isfinite (r) && amp.getSolveFailureRate() < 1.0e-4, "runs at bias " + juce::String (bias, 1));
            }
            logMessage ("idle plate current (4 tubes) at Bias 0 / 0.5 / 1: " + juce::String (idle[0], 1) + " / " + juce::String (idle[1], 1) + " / " + juce::String (idle[2], 1) + " mA");
            expect (idle[0] > idle[1] && idle[1] > idle[2], "hotter with the knob down");
            expect (idle[0] > idle[2] * 1.3, "the trimmer has a real range (the drawing's 20K trimmer in series with a 56K)");
        }

        beginTest ("tone stack: each control acts in its own band, the stack's insertion loss is a Marshall's (about -8 dB at noon)");
        {
            auto stackDb = [] (float treble, float middle, float bass, double freq)
            {
                SuperLeadStyleAmplifierProcessor a, b;
                for (auto* amp : { &a, &b })
                {
                    setParam (*amp, "sl_treble", treble);
                    setParam (*amp, "sl_middle", middle);
                    setParam (*amp, "sl_bass", bass);
                    setParam (*amp, "sl_input", 0.0f);
                    setParam (*amp, "sl_loud2", 0.3f);
                    amp->prepare (sr, 128, 2);
                }
                double rf, pf, rt, pt;
                sineRun (a, freq, 0.0002, P::followerOut, rf, pf, 0.6);
                sineRun (b, freq, 0.0002, P::toneStackOut, rt, pt, 0.6);
                return 20.0 * std::log10 (rt / rf);
            };
            const double noon = stackDb (0.5f, 0.5f, 0.5f, 1000.0);
            logMessage ("noon, 1 kHz: " + juce::String (noon, 1) + " dB");
            expect (noon < -5.0 && noon > -13.0, "insertion loss at noon");
            expect (stackDb (1.0f, 0.5f, 0.5f, 4000.0) > stackDb (0.0f, 0.5f, 0.5f, 4000.0) + 6.0, "Treble acts at 4 kHz");
            expect (stackDb (0.5f, 0.5f, 1.0f, 60.0) > stackDb (0.5f, 0.5f, 0.0f, 60.0) + 6.0, "Bass acts at 60 Hz");
            expect (stackDb (0.5f, 1.0f, 0.5f, 1000.0) > stackDb (0.5f, 0.0f, 0.5f, 1000.0) + 5.0, "Middle acts at 1 kHz");
            expect (std::abs (stackDb (1.0f, 0.5f, 0.5f, 60.0) - stackDb (0.0f, 0.5f, 0.5f, 60.0)) < 1.0, "Treble does not move the bass");
            expect (std::abs (stackDb (0.5f, 0.5f, 1.0f, 8000.0) - stackDb (0.5f, 0.5f, 0.0f, 8000.0)) < 1.0, "Bass does not move the treble");
        }

        beginTest ("channels: Channel I (bright) has more treble against its bass than Channel II (normal), as its 680 nF cathode, 3n3 coupling and 4n7 bridge say");
        {
            auto plateRatioDb = [] (float inputChoice)
            {
                double low, high;
                for (int which = 0; which < 2; ++which)
                {
                    SuperLeadStyleAmplifierProcessor amp;
                    setParam (amp, "sl_input", inputChoice);
                    setParam (amp, "sl_loud1", 0.3f);
                    setParam (amp, "sl_loud2", 0.3f);
                    amp.prepare (sr, 128, 2);
                    double r, pk;
                    sineRun (amp, which == 0 ? 100.0 : 4000.0, 0.0003, P::mixNode, r, pk, 0.6);
                    (which == 0 ? low : high) = r;
                }
                return 20.0 * std::log10 (high / low);
            };
            const double normal = plateRatioDb (0.0f), bright = plateRatioDb (2.0f);
            logMessage ("mix node, 4 kHz relative to 100 Hz: Normal " + juce::String (normal, 1) + " dB, Bright " + juce::String (bright, 1) + " dB");
            expect (bright > normal + 6.0, "the bright channel is brighter");
        }

        beginTest ("Input selector: Normal / Jumped / Bright each drive only the channel(s) they say");
        {
            auto levelWith = [] (float inputChoice, float loud2, float loud1)
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", inputChoice);
                setParam (amp, "sl_loud2", loud2);
                setParam (amp, "sl_loud1", loud1);
                amp.prepare (sr, 128, 2);
                double r, pk;
                sineRun (amp, 220.0, 0.002, P::gainStagePlate, r, pk, 0.6);
                return r;
            };
            // The unconnected channel's own Loudness still LOADS the shared mixing node (as it does on the real amp: two 470k resistors
            // into one grid), so the connected channel's level moves by a fraction of a dB with it; what must not happen is the
            // unconnected channel passing signal: with the CONNECTED channel's Loudness at 0, nothing gets through however far the
            // other one is turned up.
            const double normalOnly = levelWith (0.0f, 0.6f, 0.6f), normalBlocked = levelWith (0.0f, 0.0f, 1.0f);
            const double brightOnly = levelWith (2.0f, 0.6f, 0.6f), brightBlocked = levelWith (2.0f, 1.0f, 0.0f);
            const double jumped = levelWith (1.0f, 0.6f, 0.6f);
            logMessage ("Normal-only " + juce::String (normalOnly, 4) + " (blocked: " + juce::String (normalBlocked, 4) + "); Bright-only " + juce::String (brightOnly, 4)
                        + " (blocked: " + juce::String (brightBlocked, 4) + "); Jumped " + juce::String (jumped, 4));
            expectLessThan (normalBlocked, normalOnly * 0.05);
            expectLessThan (brightBlocked, brightOnly * 0.05);
            expectGreaterThan (normalOnly, 0.05);
            expectGreaterThan (brightOnly, 0.05);
            expectGreaterThan (jumped, std::max (normalOnly, brightOnly) * 1.05);
        }

        beginTest ("power: about 100 W at 5% distortion into 16 ohm, and the supply sags under it");
        {
            double powerAt5 = 0.0, railAtFull = 480.0, lastThd = 0.0;
            for (double amp_in : { 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.1 })
            {
                SuperLeadStyleAmplifierProcessor amp;
                amp.prepare (sr, 128, 2);
                setParam (amp, "sl_loud2", 0.5f);
                setParam (amp, "sl_input", 0.0f);
                const double freq = 1000.0, twoPi = 2.0 * juce::MathConstants<double>::pi;
                const long long total = (long long) (0.6 * sr);
                juce::AudioBuffer<float> one (2, 1);
                std::vector<double> y;
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (amp_in * std::sin (twoPi * freq * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= total - 4800)
                        y.push_back (amp.debugVoltage (P::speaker));
                }
                double mean = 0.0, sumSq = 0.0;
                for (double v : y) mean += v;
                mean /= (double) y.size();
                for (double v : y) sumSq += (v - mean) * (v - mean);
                const double rms = std::sqrt (sumSq / (double) y.size());
                double h[6] {};
                for (int k = 1; k <= 5; ++k)
                {
                    double sc = 0.0, ss = 0.0;
                    for (size_t n = 0; n < y.size(); ++n)
                    {
                        sc += y[n] * std::cos (twoPi * freq * k * (double) n / sr);
                        ss += y[n] * std::sin (twoPi * freq * k * (double) n / sr);
                    }
                    h[k] = 2.0 * std::sqrt (sc * sc + ss * ss) / (double) y.size();
                }
                const double thd = std::sqrt (h[2] * h[2] + h[3] * h[3] + h[4] * h[4] + h[5] * h[5]) / h[1];
                const double watts = rms * rms / 16.0;
                logMessage ("in " + juce::String (amp_in * 1000.0, 0) + " mV: " + juce::String (watts, 1) + " W, THD " + juce::String (100.0 * thd, 1) + " %, rail " + juce::String (amp.railPlates(), 0) + " V");
                if (thd <= 0.05)
                    powerAt5 = watts;
                lastThd = thd;
                railAtFull = amp.railPlates();
            }
            expect (powerAt5 > 70.0 && powerAt5 < 125.0, "clean power at 5% THD: " + juce::String (powerAt5, 1) + " W");
            expect (lastThd > 0.05, "it does clip at the top of the sweep");
            expect (railAtFull < 440.0, "the plate rail sags at full power: " + juce::String (railAtFull, 0) + " V");
        }

        beginTest ("global feedback: opening the 47k loop raises the gain a lot; Presence opens the top end");
        {
            auto gainDb = [] (double feedbackOhms, float presence, double freq)
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 0.0f);
                setParam (amp, "sl_loud2", 0.3f);
                setParam (amp, "sl_presence", presence);
                amp.prepare (sr, 128, 2);
                if (feedbackOhms > 0.0)
                    amp.debugSetFeedbackResistance (feedbackOhms);
                double r, pk;
                sineRun (amp, freq, 0.0005, P::speaker, r, pk, 0.7);
                return 20.0 * std::log10 (r / 0.0005);
            };
            const double closed = gainDb (0.0, 0.3f, 1000.0), open = gainDb (1.0e9, 0.3f, 1000.0);
            logMessage ("gain at 1 kHz, loop closed / open: " + juce::String (closed, 1) + " / " + juce::String (open, 1) + " dB");
            expect (open > closed + 8.0, "the negative feedback takes a lot of gain away");
            const double topLow = gainDb (0.0, 0.0f, 6000.0) - gainDb (0.0, 0.0f, 500.0), topHigh = gainDb (0.0, 1.0f, 6000.0) - gainDb (0.0, 1.0f, 500.0);
            logMessage ("6 kHz relative to 500 Hz, Presence 0 / 1: " + juce::String (topLow, 1) + " / " + juce::String (topHigh, 1) + " dB");
            expect (topHigh > topLow + 3.0, "Presence opens the top end");
        }

        beginTest ("page 2: Power Drive is a master volume; Tube Feel scales the sag and the feedback");
        {
            auto out = [] (float power, float feel, double level)
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 0.0f);
                setParam (amp, "sl_loud2", 0.5f);
                setParam (amp, "sl_power", power);
                setParam (amp, "sl_tube_feel", feel);
                amp.prepare (sr, 128, 2);
                double r, pk;
                sineRun (amp, 1000.0, level, P::speaker, r, pk, 0.6);
                return r;
            };
            expect (out (0.5f, 1.0f, 0.02) < out (1.0f, 1.0f, 0.02) * 0.6, "Power Drive turned down lowers the output");
            SuperLeadStyleAmplifierProcessor stiff, real;
            setParam (stiff, "sl_tube_feel", 0.0f);
            stiff.prepare (sr, 128, 2);
            real.prepare (sr, 128, 2);
            double r, pk;
            sineRun (stiff, 220.0, 0.1, P::speaker, r, pk, 0.6);
            sineRun (real, 220.0, 0.1, P::speaker, r, pk, 0.6);
            expect (stiff.railPlates() > real.railPlates() + 10.0, "Tube Feel 0 sags less than the real supply: " + juce::String (stiff.railPlates(), 0) + " vs " + juce::String (real.railPlates(), 0) + " V");
        }

        beginTest ("every corner of the controls finds its operating point and runs");
        {
            int bad = 0;
            for (float bass : { 0.0f, 1.0f })
                for (float mid : { 0.0f, 1.0f })
                    for (float treble : { 0.0f, 1.0f })
                        for (float pres : { 0.0f, 1.0f })
                            for (float vol : { 0.0f, 1.0f })
                            {
                                SuperLeadStyleAmplifierProcessor amp;
                                setParam (amp, "sl_bass", bass);
                                setParam (amp, "sl_middle", mid);
                                setParam (amp, "sl_treble", treble);
                                setParam (amp, "sl_presence", pres);
                                setParam (amp, "sl_loud1", vol);
                                setParam (amp, "sl_loud2", vol);
                                amp.prepare (sr, 128, 2);
                                const double tail = amp.debugVoltage (P::phaseInverterTail);
                                double r, pk;
                                sineRun (amp, 440.0, 0.1, P::speaker, r, pk, 0.15);
                                // a corner that clips the whole chain can hold a sample or two (docs/circuits/SuperLead1959.md, "Known limits")
                                if (! amp.dcConverged() || tail < 20.0 || tail > 50.0 || amp.getSolveFailureRate() > 3.0e-4 || ! std::isfinite (r))
                                {
                                    ++bad;
                                    logMessage ("bad corner: bass " + juce::String (bass) + " mid " + juce::String (mid) + " treble " + juce::String (treble) + " pres " + juce::String (pres) + " vol " + juce::String (vol)
                                                + ": failures " + juce::String (amp.getSolveFailureRate(), 6));
                                }
                            }
            expectEquals (bad, 0);
        }

        beginTest ("soak: ten seconds of plucked notes, hot settings: the solver never gets stuck");
        {
            SuperLeadStyleAmplifierProcessor amp;
            setParam (amp, "sl_presence", 1.0f);
            setParam (amp, "sl_loud2", 0.8f);
            setParam (amp, "sl_loud1", 0.8f);
            amp.prepare (sr, 128, 2);
            juce::Random rnd (1234);
            juce::AudioBuffer<float> buf (2, 128);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {};
            long long n = 0;
            double peak = 0.0;
            bool finite = true;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.45 * sr) == 0)
                    {
                        f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0);
                        level = 0.03 * std::pow (20.0, rnd.nextDouble());
                        age = 0.0;
                    }
                    age += 1.0 / sr;
                    double v = 0.0;
                    for (int k = 1; k <= 8; ++k)
                    {
                        phase[k] += twoPi * f0 * k / sr;
                        v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k));
                    }
                    buf.setSample (0, i, (float) (v * level));
                    buf.setSample (1, i, (float) (v * level));
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("failure rate " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()) + ", peak " + juce::String (peak, 2));
            expect (finite, "output stays finite");
            expect (amp.getSolveFailureRate() < 1.0e-4, "failure rate " + juce::String (amp.getSolveFailureRate()));
            expect (peak < 8.0, "output stays bounded");
        }

        beginTest ("a burst of garbage on the input (NaN, huge values) does not wedge the amp");
        {
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            for (int b = 0; b < 100; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    float v = (float) (0.05 * std::sin (0.06 * (double) (b * 128 + i)));
                    if (b >= 20 && b < 24)
                        v = i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1 ? 1.0e6f : -1.0e6f);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
            }
            double peak = 0.0;
            bool finite = true;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float v = (float) (0.05 * std::sin (0.06 * (double) (b * 128 + i)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            expect (finite, "finite after the garbage");
            expect (peak > 0.02 && peak < 4.0, "the amp plays normally again, peak " + juce::String (peak));
        }

        beginTest ("random knob settings, plucked notes: at most the odd held sample and a rare restore");
        {
            juce::Random rnd (2024);
            int bad = 0;
            for (int combo = 0; combo < 20; ++combo)
            {
                SuperLeadStyleAmplifierProcessor amp;
                const float k[10] = { rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat() };
                setParam (amp, "sl_loud2", k[0]); setParam (amp, "sl_loud1", k[1]); setParam (amp, "sl_treble", k[2]); setParam (amp, "sl_middle", k[3]);
                setParam (amp, "sl_bass", k[4]); setParam (amp, "sl_presence", k[5]); setParam (amp, "sl_bias", k[6]); setParam (amp, "sl_power", k[7]);
                setParam (amp, "sl_tube_feel", k[8]);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {}, peak = 0.0;
                long long n = 0;
                for (int b = 0; b < (int) (4.0 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        if (n % (long long) (0.45 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0); level = 0.03 * std::pow (20.0, rnd.nextDouble()); age = 0.0; }
                        age += 1.0 / sr;
                        double v = 0.0;
                        for (int q = 1; q <= 8; ++q) { phase[q] += twoPi * f0 * q / sr; v += std::sin (phase[q]) / (double) q * std::exp (-age * (2.0 + q)); }
                        const double attack = std::min (1.0, age / 0.002);
                        buf.setSample (0, i, (float) (v * level * attack));
                        buf.setSample (1, i, (float) (v * level * attack));
                    }
                    amp.process (buf);
                    for (int i = 0; i < 128; ++i)
                        peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
                // Hard-driven combinations still lose the odd sample to the power stage's flyback spikes (see the doc, "Known limits"): a held
                // sample is inaudible, a couple of de-clicked restores in four seconds of the hottest settings are tolerated, a streak is not.
                if (amp.getSolveFailureRate() > 3.0e-4 || amp.debugRecoveries() > 3 || ! (peak < 6.0))
                {
                    ++bad;
                    logMessage ("combo " + juce::String (combo) + " [loud2 " + juce::String (k[0], 2) + " loud1 " + juce::String (k[1], 2) + " t " + juce::String (k[2], 2) + " m " + juce::String (k[3], 2) + " b " + juce::String (k[4], 2)
                                + " p " + juce::String (k[5], 2) + " bias " + juce::String (k[6], 2) + " power " + juce::String (k[7], 2) + " feel " + juce::String (k[8], 2) + "]: failures "
                                + juce::String (amp.getSolveFailureRate(), 5) + " recoveries " + juce::String (amp.debugRecoveries()) + " peak " + juce::String (peak, 2));
                }
            }
            expectEquals (bad, 0);
        }

        beginTest ("a hot pedal into the amp (square-ish notes at up to 3 V): finite, bounded, no long failure streaks");
        {
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            double peak = 0.0;
            bool finite = true;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.5 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::tanh (8.0 * std::sin (phase)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("pre failures " + juce::String (amp.debugPreFailures()) + ", power failures " + juce::String (amp.debugPowerFailures()) + ", sanity rejects " + juce::String (amp.debugSanityRejects())
                        + ", worst sane speaker volts " + juce::String (amp.debugWorstSaneVolts(), 1));
            logMessage ("recoveries " + juce::String (amp.debugRecoveries()) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 5) + ", peak " + juce::String (peak, 2)
                        + ", Newton iterations/sample: preamp " + juce::String (amp.debugIterations (0), 2) + ", power " + juce::String (amp.debugIterations (1), 2));
            expectLessThan (amp.debugIterations (1), 12.0);
            expect (finite);
            expect (peak <= 150.0 * SuperLeadStyleAmplifierProcessor::outputScale * 2.0 + 1.0e-3);
            expectLessThan (amp.getSolveFailureRate(), 2.0e-3);
            expectLessThan (amp.debugRecoveries(), 10);
        }

        beginTest ("a hot pedal into ONE channel (the way it is normally played): no failed solves at all, no restores");
        for (float inputChoice : { 0.0f, 2.0f })
        {
            SuperLeadStyleAmplifierProcessor amp;
            setParam (amp, "sl_input", inputChoice);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.5 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::tanh (8.0 * std::sin (phase)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
            }
            logMessage ("Input " + juce::String (inputChoice, 0) + ": failure rate " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries())
                        + ", sanity rejects " + juce::String (amp.debugSanityRejects()));
            expectLessThan (amp.getSolveFailureRate(), 1.0e-3);
            expectLessThan (amp.debugRecoveries(), 2);
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_POWERCAL", {}).isNotEmpty())
        {
            // Calibration sweep for a behavioural power-stage model (2026-09-27): the real, already-validated full model
            // IS the ground truth here -- this drives it with a slow sine at a range of levels, lets the supply settle to
            // its own quasi-equilibrium at each level (so sag is captured as part of the curve, not ignored), and prints
            // (post-tone-stack drive, speaker output, plate rail) so a lookup table / fitted curve can be built from real
            // numbers instead of a guessed shape. Boundary: tone stack stays a real linear circuit (untouched); only the
            // phase inverter + power tubes + output transformer are candidates for replacement, so the input side of the
            // curve is `toneStackOut`, not the raw preamp signal.
            beginTest ("power-stage calibration sweep (dev only)");
            SuperLeadStyleAmplifierProcessor amp;
            setParam (amp, "sl_input", 0.0f);
            setParam (amp, "sl_loud1", 0.8f);
            setParam (amp, "sl_loud2", 0.8f);
            setParam (amp, "sl_treble", 0.5f);
            setParam (amp, "sl_middle", 0.5f);
            setParam (amp, "sl_bass", 0.5f);
            setParam (amp, "sl_power", 1.0f); // Power Drive at max: masterGain == 1, no extra scaling ahead of the curve
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 2.0e-5, 5.0e-5, 1.0e-4, 2.0e-4, 4.0e-4, 7.0e-4, 1.0e-3, 1.5e-3, 2.0e-3, 3.0e-3, 4.0e-3, 5.5e-3, 7.0e-3, 9.0e-3, 0.012, 0.016, 0.02, 0.03, 0.05, 0.1 })
            {
                const long long total = (long long) (1.5 * sr); // settle so the supply reaches ITS OWN equilibrium at this level
                const long long measureFrom = total - (long long) (0.1 * sr);
                double drivePeak = 0.0, outPeak = 0.0, outSumSq = 0.0;
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
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::toneStackOut)));
                        const double y = amp.debugVoltage (P::speaker);
                        outPeak = juce::jmax (outPeak, std::abs (y));
                        outSumSq += y * y;
                        ++n2;
                    }
                }
                logMessage ("powercal drive_pk=" + juce::String (drivePeak, 6) + " out_pk=" + juce::String (outPeak, 4)
                            + " out_rms=" + juce::String (std::sqrt (outSumSq / (double) juce::jmax (1LL, n2)), 4) + " rail=" + juce::String (amp.railPlates(), 2)
                            + " fails=" + juce::String (amp.getSolveFailureRate(), 6));
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("SL_NOON_DIAG", {}).isNotEmpty())
        {
            // Diagnostic: PedalUnityLevelTests sets every page-1 knob to 0.5 and measures gain via the registry (so
            // reducedOrder is whatever EffectRegistry.cpp sets) -- isolate which knob's now-missing effect explains the gap.
            beginTest ("noon knobs diagnostic (dev only)");
            const auto gainAt = [] (bool reduced, float presence)
            {
                SuperLeadStyleAmplifierProcessor::reducedOrder = reduced;
                SuperLeadStyleAmplifierProcessor amp;
                const auto pages = amp.getParameterPages();
                for (auto* f : pages[0])
                    if (f->range.interval < 1.0f) // matches PedalUnityLevelTests.cpp's own skip for stepped selectors
                        *f = juce::jlimit (f->range.start, f->range.end, 0.5f);
                setParam (amp, "sl_presence", presence);
                amp.prepare (sr, 512, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi, f0 = 164.81, rmsIn = 0.1;
                double norm = 0.0;
                for (int k = 1; k <= 10; ++k) norm += 0.5 / (double) (k * k);
                const double scale = rmsIn / std::sqrt (norm);
                const int warm = (int) (5.0 * sr), len = (int) (1.0 * sr);
                juce::AudioBuffer<float> buf (1, 64);
                double sIn = 0.0, sOut = 0.0;
                for (long long base = 0; base < warm + len; base += 64)
                {
                    double x[64];
                    for (int i = 0; i < 64; ++i)
                    {
                        double v = 0.0;
                        for (int k = 1; k <= 10; ++k) v += std::sin (twoPi * f0 * k * (double) (base + i) / sr) / (double) k;
                        x[i] = scale * v;
                        buf.setSample (0, i, (float) x[i]);
                    }
                    amp.process (buf);
                    if (base >= warm)
                        for (int i = 0; i < 64; ++i) { sIn += x[i] * x[i]; sOut += (double) buf.getSample (0, i) * buf.getSample (0, i); }
                }
                return 10.0 * std::log10 (sOut / sIn);
            };
            {
                // Faithful reproduction of PedalUnityLevelTests.cpp itself (via the real registry factory: oversampling +
                // trim included), to cross-check the hand-rolled gainAt() lambdas above against the actual failing number.
                EffectRegistry registry;
                registerBuiltInEffects (registry);
                auto pedal = registry.create ("SuperLeadStyleAmplifier");
                pedal->prepare (sr, 512, 1);
                const auto pedalPages = pedal->getParameterPages();
                for (auto* f : pedalPages[0])
                    if (f->range.interval < 1.0f)
                        *f = juce::jlimit (f->range.start, f->range.end, 0.5f);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi, f0 = 164.81, rmsIn = 0.1;
                double norm = 0.0;
                for (int k = 1; k <= 10; ++k) norm += 0.5 / (double) (k * k);
                const double scale = rmsIn / std::sqrt (norm);
                const int warm = (int) (5.0 * sr), len = (int) (1.0 * sr);
                juce::AudioBuffer<float> buf (1, 64);
                double sIn = 0.0, sOut = 0.0;
                for (long long base = 0; base < warm + len; base += 64)
                {
                    double x[64];
                    for (int i = 0; i < 64; ++i)
                    {
                        double v = 0.0;
                        for (int k = 1; k <= 10; ++k) v += std::sin (twoPi * f0 * k * (double) (base + i) / sr) / (double) k;
                        x[i] = scale * v;
                        buf.setSample (0, i, (float) x[i]);
                    }
                    pedal->process (buf);
                    if (base >= warm)
                        for (int i = 0; i < 64; ++i) { sIn += x[i] * x[i]; sOut += (double) buf.getSample (0, i) * buf.getSample (0, i); }
                }
                logMessage ("via registry.create(): " + juce::String (10.0 * std::log10 (sOut / sIn), 2) + " dB, reducedOrder="
                            + juce::String ((int) SuperLeadStyleAmplifierProcessor::reducedOrder));
            }
            logMessage ("ref presence=0.5: " + juce::String (gainAt (false, 0.5f), 2) + " dB");
            logMessage ("red presence=0.5: " + juce::String (gainAt (true, 0.5f), 2) + " dB");
            logMessage ("ref presence=0.0: " + juce::String (gainAt (false, 0.0f), 2) + " dB");
            logMessage ("red presence=0.0: " + juce::String (gainAt (true, 0.0f), 2) + " dB");

            const auto gainAtInput = [] (bool reduced, float input)
            {
                SuperLeadStyleAmplifierProcessor::reducedOrder = reduced;
                SuperLeadStyleAmplifierProcessor amp;
                const auto pages = amp.getParameterPages();
                for (auto* f : pages[0])
                    *f = juce::jlimit (f->range.start, f->range.end, 0.5f);
                setParam (amp, "sl_input", input);
                amp.prepare (sr, 512, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi, f0 = 164.81, rmsIn = 0.1;
                double norm = 0.0;
                for (int k = 1; k <= 10; ++k) norm += 0.5 / (double) (k * k);
                const double scale = rmsIn / std::sqrt (norm);
                const int warm = (int) (5.0 * sr), len = (int) (1.0 * sr);
                juce::AudioBuffer<float> buf (1, 64);
                double sIn = 0.0, sOut = 0.0;
                for (long long base = 0; base < warm + len; base += 64)
                {
                    double x[64];
                    for (int i = 0; i < 64; ++i)
                    {
                        double v = 0.0;
                        for (int k = 1; k <= 10; ++k) v += std::sin (twoPi * f0 * k * (double) (base + i) / sr) / (double) k;
                        x[i] = scale * v;
                        buf.setSample (0, i, (float) x[i]);
                    }
                    amp.process (buf);
                    if (base >= warm)
                        for (int i = 0; i < 64; ++i) { sIn += x[i] * x[i]; sOut += (double) buf.getSample (0, i) * buf.getSample (0, i); }
                }
                return 10.0 * std::log10 (sOut / sIn);
            };
            for (float inp : { 0.0f, 1.0f, 2.0f })
            {
                logMessage ("input=" + juce::String (inp, 0) + " ref: " + juce::String (gainAtInput (false, inp), 2)
                            + " dB, red: " + juce::String (gainAtInput (true, inp), 2) + " dB");
            }

            // Isolate: is the gap about Loudness-knob setting, or about the richer/higher-frequency test signal?
            const auto pureToneGainAt = [] (bool reduced, float loud, double freq)
            {
                SuperLeadStyleAmplifierProcessor::reducedOrder = reduced;
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 0.0f);
                setParam (amp, "sl_loud1", loud);
                setParam (amp, "sl_loud2", loud);
                setParam (amp, "sl_power", 1.0f);
                amp.prepare (sr, 512, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi, rmsIn = 0.1;
                const int warm = (int) (2.0 * sr), len = (int) (1.0 * sr);
                juce::AudioBuffer<float> buf (1, 64);
                double sIn = 0.0, sOut = 0.0;
                for (long long base = 0; base < warm + len; base += 64)
                {
                    double x[64];
                    for (int i = 0; i < 64; ++i)
                    {
                        x[i] = rmsIn * std::sqrt (2.0) * std::sin (twoPi * freq * (double) (base + i) / sr);
                        buf.setSample (0, i, (float) x[i]);
                    }
                    amp.process (buf);
                    if (base >= warm)
                        for (int i = 0; i < 64; ++i) { sIn += x[i] * x[i]; sOut += (double) buf.getSample (0, i) * buf.getSample (0, i); }
                }
                return 10.0 * std::log10 (sOut / sIn);
            };
            for (double freq : { 100.0, 165.0, 500.0, 1000.0, 1650.0 })
                logMessage ("pure tone " + juce::String (freq, 0) + " Hz, loud=0.5: ref " + juce::String (pureToneGainAt (false, 0.5f, freq), 2)
                            + " dB, red " + juce::String (pureToneGainAt (true, 0.5f, freq), 2) + " dB");
            logMessage ("pure tone 165 Hz, loud=0.8: ref " + juce::String (pureToneGainAt (false, 0.8f, 165.0), 2)
                        + " dB, red " + juce::String (pureToneGainAt (true, 0.8f, 165.0), 2) + " dB");
            SuperLeadStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            // Permanent regression test for behavioralPowerStage() (2026-09-27, the shipped default -- EffectRegistry.cpp
            // turns it on for the real app): drives BOTH a reference (full netlist) and a reducedOrder amp with the SAME
            // signal, asserting level tracks the reference within 1 dB across the whole dynamic range, and reducedOrder
            // has zero failures/recoveries by construction (no Newton solve left to have a bad day).
            beginTest ("reducedOrder power stage tracks the reference (level, and zero failures by construction)");
            SuperLeadStyleAmplifierProcessor::reducedOrder = false;
            SuperLeadStyleAmplifierProcessor ref;
            setParam (ref, "sl_input", 0.0f);
            setParam (ref, "sl_loud1", 0.8f);
            setParam (ref, "sl_loud2", 0.8f);
            setParam (ref, "sl_power", 1.0f);
            ref.prepare (sr, 128, 2);
            SuperLeadStyleAmplifierProcessor::reducedOrder = true;
            SuperLeadStyleAmplifierProcessor red;
            setParam (red, "sl_input", 0.0f);
            setParam (red, "sl_loud1", 0.8f);
            setParam (red, "sl_loud2", 0.8f);
            setParam (red, "sl_power", 1.0f);
            red.prepare (sr, 128, 2);

            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 5.0e-4, 2.0e-3, 6.0e-3, 0.012, 0.02, 0.05, 0.1 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double refOutPeak = 0.0, refOutSumSq = 0.0, redOutPeak = 0.0, redOutSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                const auto tRefStart = juce::Time::getHighResolutionTicks();
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    ref.process (one);
                    if (n >= measureFrom)
                    {
                        const double y = ref.debugVoltage (P::speaker);
                        refOutPeak = juce::jmax (refOutPeak, std::abs (y));
                        refOutSumSq += y * y;
                        ++n2;
                    }
                }
                const double refSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - tRefStart);
                n2 = 0;
                const auto tRedStart = juce::Time::getHighResolutionTicks();
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    red.process (one);
                    if (n >= measureFrom)
                    {
                        const double y = red.debugVoltage (P::speaker);
                        redOutPeak = juce::jmax (redOutPeak, std::abs (y));
                        redOutSumSq += y * y;
                        ++n2;
                    }
                }
                const double redSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - tRedStart);
                const double refRms = std::sqrt (refOutSumSq / (double) juce::jmax (1LL, n2));
                const double redRms = std::sqrt (redOutSumSq / (double) juce::jmax (1LL, n2));
                const double dB = 20.0 * std::log10 (juce::jmax (1.0e-9, redRms) / juce::jmax (1.0e-9, refRms));
                logMessage ("level " + juce::String (level, 4) + ": ref " + juce::String (refRms, 3) + " Vrms (" + juce::String (100.0 * refSeconds / 1.5, 2)
                            + "% cpu, fails " + juce::String (ref.getSolveFailureRate(), 6) + ", recov " + juce::String (ref.debugRecoveries())
                            + ") vs reduced " + juce::String (redRms, 3) + " Vrms (" + juce::String (100.0 * redSeconds / 1.5, 2) + "% cpu, fails "
                            + juce::String (red.getSolveFailureRate(), 6) + ", recov " + juce::String (red.debugRecoveries()) + "), diff " + juce::String (dB, 2) + " dB");
                expectLessThan (std::abs (dB), 1.0);
                expectLessThan (red.getSolveFailureRate(), 1.0e-6);
                expect (red.debugRecoveries() == 0);
            }
            SuperLeadStyleAmplifierProcessor::reducedOrder = false; // restore the default for every other test in this suite
        }

        {
            // Regression test for the 2026-09-27 speaker-level bug the user found by ear: reducedOrder must NOT apply the
            // reference netlist's z^-0.8 impedance compensation (there's no physical mismatch left for it to cancel) --
            // 4 / 8 / 16 ohm must sound equally loud in the shipped default, not a ~9.6 dB jump.
            beginTest ("reducedOrder: 4 / 8 / 16 ohm sound equally loud (the shipped default has no physical speaker to mismatch)");
            SuperLeadStyleAmplifierProcessor::reducedOrder = true;
            const auto rmsAt = [] (float speaker)
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 0.0f);
                setParam (amp, "sl_loud2", 0.8f);
                setParam (amp, "sl_speaker", speaker);
                amp.prepare (sr, 128, 2);
                double r, pk;
                sineRun (amp, 200.0, 0.02, P::speaker, r, pk, 0.6);
                return r;
            };
            const double r4 = rmsAt (0.0f), r8 = rmsAt (1.0f), r16 = rmsAt (2.0f);
            logMessage ("reducedOrder speaker volts at 4 / 8 / 16 ohm: " + juce::String (r4, 2) + " / " + juce::String (r8, 2) + " / " + juce::String (r16, 2) + " V rms");
            expectLessThan (std::abs (20.0 * std::log10 (r4 / r16)), 0.1);
            expectLessThan (std::abs (20.0 * std::log10 (r8 / r16)), 0.1);
            SuperLeadStyleAmplifierProcessor::reducedOrder = false;
        }

        beginTest ("a triode's plate below its cathode passes no current, whatever the grid does (regression: a phantom 0.5-3.7 mA below the table's low edge)");
        {
            KorenTriode t;
            for (double vpk : { -5.0, -20.0, -60.0, -180.0 })
                for (double vgk : { 0.0, 5.0, 20.0 })
                    expectLessThan (t.evaluate (vgk, vpk).ip, vpk == -5.0 ? 0.3e-3 : 1.0e-6);
        }

        beginTest ("page 2: Speaker is a real load -- resonance, voice-coil inductance, 4 / 8 / 16 ohm");
        {
            const auto gainDbWith = [] (float speaker, double f, bool resistive)
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 0.0f);
                setParam (amp, "sl_loud2", 0.3f);
                setParam (amp, "sl_speaker", speaker);
                amp.prepare (sr, 128, 2);
                if (resistive)
                    amp.debugSetResistiveLoad (16.0);
                double r, pk;
                sineRun (amp, f, 0.0005, P::speaker, r, pk, 0.8);
                return 20.0 * std::log10 (r / 0.0005);
            };
            const double resonanceBump = (gainDbWith (2.0f, 85.0, false) - gainDbWith (2.0f, 200.0, false)) - (gainDbWith (2.0f, 85.0, true) - gainDbWith (2.0f, 200.0, true));
            const double topLift = (gainDbWith (2.0f, 6000.0, false) - gainDbWith (2.0f, 500.0, false)) - (gainDbWith (2.0f, 6000.0, true) - gainDbWith (2.0f, 500.0, true));
            logMessage ("16 ohm speaker vs a resistor: resonance bump " + juce::String (resonanceBump, 1) + " dB at 85 Hz, top-end lift " + juce::String (topLift, 1) + " dB at 6 kHz");
            expect (resonanceBump > 1.0, "the cone resonance is audible in the amp's response");
            expect (topLift > 1.0, "the voice coil's inductance lifts the top end");

            const auto rmsFor = [] (float speaker)
            {
                SuperLeadStyleAmplifierProcessor amp;
                setParam (amp, "sl_input", 0.0f);
                setParam (amp, "sl_loud2", 1.0f);
                setParam (amp, "sl_speaker", speaker);
                amp.prepare (sr, 128, 2);
                double r, pk;
                sineRun (amp, 200.0, 0.02, P::speaker, r, pk, 0.6);
                return std::pair<double, double> { r * (speaker == 0.0f ? std::pow (0.25, -0.8) : speaker == 1.0f ? std::pow (0.5, -0.8) : 1.0), amp.getSolveFailureRate() };
            };
            const auto r4 = rmsFor (0.0f), r8 = rmsFor (1.0f), r16 = rmsFor (2.0f);
            logMessage ("full-drive speaker volts at 4 / 8 / 16 ohm (level-compensated): " + juce::String (r4.first, 1) + " / " + juce::String (r8.first, 1) + " / " + juce::String (r16.first, 1) + " V rms");
            expect (std::abs (20.0 * std::log10 (r4.first / r16.first)) < 4.0 && std::abs (20.0 * std::log10 (r8.first / r16.first)) < 4.0, "loudness stays comparable across the loads");
            expect (r4.second < 5.0e-4 && r8.second < 5.0e-4 && r16.second < 5.0e-4, "stable into all three");
        }

        beginTest ("page 2 lives in a sub-group; the Speaker choice is a preset parameter");
        {
            SuperLeadStyleAmplifierProcessor amp;
            int page2Count = 0;
            for (auto* g : amp.getParameters()->getSubgroups (false))
                page2Count += (int) g->getParameters (false).size();
            expectEquals (page2Count, 5); // Power Drive, Bias, Tube Feel, Speaker, Output
        }

        beginTest ("stereo: identical channels are processed once and stay identical; different channels are independent");
        {
            SuperLeadStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            const int n = 4096;
            juce::AudioBuffer<float> buf (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float v = (float) (0.02 * std::sin (0.05 * (double) i));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            amp.process (buf);
            bool same = true;
            for (int i = 0; i < n; ++i)
                same = same && juce::exactlyEqual (buf.getSample (0, i), buf.getSample (1, i));
            expect (same, "dual-mono output identical");

            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, (float) (0.02 * std::sin (0.05 * (double) i)));
                buf.setSample (1, i, 0.0f);
            }
            amp.process (buf);
            double left = 0.0, right = 0.0;
            for (int i = 0; i < n; ++i)
            {
                left += std::abs (buf.getSample (0, i));
                right += std::abs (buf.getSample (1, i));
            }
            expect (std::isfinite (left) && std::isfinite (right) && left > 2.0 * right, "the channels are independent once they differ");
        }
    }
};

static SuperLeadStyleAmplifierProcessorTests superLeadStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
