#include "Effects/TwinReverbStyleAmplifierProcessor.h"
#include "Effects/DeluxeReverbStyleAmplifierProcessor.h"
#include "Effects/CarrRamblerStyleAmplifierProcessor.h"
#include "Effects/DividedBy13FTR37StyleAmplifierProcessor.h"
#include "Effects/SVTStyleAmplifierProcessor.h"
#include "Effects/TrainwreckExpressStyleAmplifierProcessor.h"
#include "Effects/KometConcordeStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only (ASYM_BENCH=1): fits the behavioural power stage's static asymmetry
    constant `bmAsym` against the amp's own full-order netlist. Real push-pull /
    cathode-biased output stages never clip exactly symmetrically at centre bias;
    the behavioural saturator is a signed ODD function, so without a baseline
    offset it produces zero even harmonics at noon -- the shipped models did.

    For each amp: render a sine through the FULL power stage (reducedOrder=false),
    capture the power-stage drive waveform and the speaker output, measure the
    reference even:odd harmonic ratio, then sweep the offset s of the behavioural
    curve  raw(x) = sat(x + s) - sat(s)  and report the s (as a fraction of k or
    bmYmax) whose even:odd matches the reference. The DC-blocking one-pole the
    behavioural stage gets alongside bmAsym is included in the sweep. */
class PowerStageAsymBench : public juce::UnitTest
{
public:
    PowerStageAsymBench() : juce::UnitTest ("PowerStageAsymBench", "Probe") {}

    static constexpr double sr = 48000.0;
    static constexpr double dcHz = 8.0;   // the bmDcHz the behavioural stages get (JTM45's value)
    static constexpr int nWin = 8192;
    // coherent stimulus: an integer number of cycles inside nWin so the rectangular
    // DFT puts each harmonic exactly on a bin (a literal 110 Hz leaks into neighbours).
    static constexpr double toneHz = 19.0 * sr / nWin; // ~111.3 Hz, bin 19

    struct H { double evenOdd = 0.0, h2rel = 0.0, thd = 0.0; };

    static H measure (const std::vector<double>& y)
    {
        // single-tone FFT, rectangular window over an integer number of cycles
        const int n = (int) y.size();
        double bestF = 0.0, bestMag = -1.0;
        std::vector<double> mag (n / 2);
        // only the first ~10 harmonics of a ~110 Hz tone matter -- the naive DFT is O(n)
        // per bin, so searching the top of the spectrum is the dominant cost for nothing.
        const int nBins = juce::jmin (n / 2, 512);
        for (int bin = 1; bin < nBins; ++bin)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double ph = -2.0 * juce::MathConstants<double>::pi * (double) bin * (double) i / n;
                re += y[(size_t) i] * std::cos (ph);
                im += y[(size_t) i] * std::sin (ph);
            }
            mag[(size_t) bin] = std::sqrt (re * re + im * im) / n * 2.0;
            if (mag[(size_t) bin] > bestMag) { bestMag = mag[(size_t) bin]; bestF = (double) bin; }
        }
        const int f0 = (int) bestF;
        const auto h = [&] (int k) { return k * f0 < nBins ? mag[(size_t) (k * f0)] : 0.0; };
        const double even = std::sqrt (h (2) * h (2) + h (4) * h (4) + h (6) * h (6) + h (8) * h (8));
        const double odd = std::sqrt (h (3) * h (3) + h (5) * h (5) + h (7) * h (7));
        double ss = 0.0;
        for (int k = 2; k * f0 < nBins && k <= 10; ++k) ss += h (k) * h (k);
        return { odd > 1.0e-12 ? even / odd : 0.0, bestMag > 1.0e-12 ? h (2) / bestMag : 0.0,
                 bestMag > 1.0e-12 ? std::sqrt (ss) / bestMag : 0.0 };
    }

    // behavioural curve replica (the form Twin/Deluxe/CarrRambler/Div13 share)
    static double sat (double x, double k, double rail, double ymax, double kneeN)
    {
        const double ux = std::abs (x) / juce::jmax (1.0e-9, k);
        return std::copysign (ymax * ux / std::pow (1.0 + std::pow (ux, kneeN), 1.0 / kneeN) * rail, x);
    }

    template <typename SetParams, typename DriveProbe, typename SpkProbe, typename Proc>
    void bench (const char* name, Proc& amp, bool* reducedFlag,
                SetParams setParams, DriveProbe driveProbe, SpkProbe spkProbe,
                double level, double gain0, double ymax, double kneeN, double railNom,
                bool asymInKUnits)
    {
        *reducedFlag = false;
        setParams (amp);
        amp.prepare (sr, 128, 2);
        juce::AudioBuffer<float> one (2, 1);
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) (1.5 * sr), keep = nWin;
        std::vector<double> x, y;
        x.reserve ((size_t) keep);
        y.reserve ((size_t) keep);
        for (long long n = 0; n < total; ++n)
        {
            const float v = (float) (level * std::sin (twoPi * toneHz * (double) n / sr));
            one.setSample (0, 0, v);
            one.setSample (1, 0, v);
            amp.process (one);
            if (n >= total - keep)
            {
                x.push_back (amp.debugVoltage (driveProbe));
                y.push_back (amp.debugVoltage (spkProbe));
            }
        }
        const H ref = measure (y);
        // CarrRambler's behavioural drive is DC-blocked (driveRaw - driveDc): centre x the same way
        double mean = 0.0;
        for (double v : x) mean += v;
        mean /= (double) x.size();
        for (auto& v : x) v -= mean;

        // Same capture at a clean level: an offset big enough to match a large crunch
        // even/odd parks the small-signal operating point deep in the knee (FTR37: -21 dB
        // clean gain at s = 1.37k), which the crunch-level objective alone cannot see.
        std::vector<double> xl, yl;
        xl.reserve ((size_t) keep);
        yl.reserve ((size_t) keep);
        for (long long n = 0; n < total; ++n)
        {
            const float v = (float) (0.02 * level * std::sin (twoPi * toneHz * (double) n / sr));
            one.setSample (0, 0, v);
            one.setSample (1, 0, v);
            amp.process (one);
            if (n >= total - keep)
            {
                xl.push_back (amp.debugVoltage (driveProbe));
                yl.push_back (amp.debugVoltage (spkProbe));
            }
        }
        const H refLow = measure (yl);
        double meanL = 0.0;
        for (double v : xl) meanL += v;
        meanL /= (double) xl.size();
        for (auto& v : xl) v -= meanL;

        double refRms = 0.0;
        for (double v : y) refRms += v * v;
        refRms = std::sqrt (refRms / (double) y.size());
        const double k = railNom * ymax / gain0;
        const double dcCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * dcHz / sr);
        // level-preserving objective: a big enough static offset matches even/odd
        // trivially but shoves the operating point into the low-gain part of the
        // knee (measured: FTR37 noon level dropped 10dB). Penalise candidates
        // whose output level moves vs the s=0 baseline of the same model.
        double baseRms = 1.0;
        double bestS = 0.0, bestErr = 1.0e9;
        for (double s = 0.0; s <= 2.5 * k; s += 0.005 * k)
        {
            std::vector<double> m;
            m.resize (x.size());
            double dcState = 0.0, ss = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                // same rail clamp the shipped stages apply before DC blocking
                const double raw = juce::jlimit (-railNom * ymax, railNom * ymax,
                                                 sat (x[i] + s, k, railNom, ymax, kneeN) - sat (s, k, railNom, ymax, kneeN));
                dcState += dcCoeff * (raw - dcState);
                m[i] = raw - dcState;
                ss += m[i] * m[i];
            }
            const double rms = std::sqrt (ss / (double) m.size());
            if (s == 0.0) baseRms = rms;
            const double lvlDb = std::abs (20.0 * std::log10 (juce::jmax (1.0e-9, rms) / juce::jmax (1.0e-9, baseRms)));
            const H hmod = measure (m);
            // clean-level terms: the same s must not flatten the small-signal gain
            // (level) nor invent even content the reference does not have (even/odd).
            dcState = 0.0;
            double ssl = 0.0;
            std::vector<double> ml;
            ml.resize (xl.size());
            for (size_t i = 0; i < xl.size(); ++i)
            {
                const double raw = juce::jlimit (-railNom * ymax, railNom * ymax,
                                                 sat (xl[i] + s, k, railNom, ymax, kneeN) - sat (s, k, railNom, ymax, kneeN));
                dcState += dcCoeff * (raw - dcState);
                ml[i] = raw - dcState;
                ssl += ml[i] * ml[i];
            }
            double baseRmsL = 1.0;
            {
                double ss0 = 0.0, dc0 = 0.0;
                for (size_t i = 0; i < xl.size(); ++i)
                {
                    const double raw = juce::jlimit (-railNom * ymax, railNom * ymax,
                                                     sat (xl[i], k, railNom, ymax, kneeN) - sat (0.0, k, railNom, ymax, kneeN));
                    dc0 += dcCoeff * (raw - dc0);
                    const double b = raw - dc0;
                    ss0 += b * b;
                }
                baseRmsL = std::sqrt (ss0 / (double) xl.size());
            }
            const double rmsL = std::sqrt (ssl / (double) ml.size());
            const double lvlDbL = std::abs (20.0 * std::log10 (juce::jmax (1.0e-9, rmsL) / juce::jmax (1.0e-9, baseRmsL)));
            const H hmodL = measure (ml);
            const double err = std::abs (hmod.evenOdd - ref.evenOdd) + 0.5 * lvlDb
                             + 0.5 * std::abs (hmodL.evenOdd - refLow.evenOdd) + 1.0 * lvlDbL;
            if (err < bestErr) { bestErr = err; bestS = s; }
        }
        const double bmAsym = asymInKUnits ? bestS / k : bestS / ymax;
        logMessage (juce::String (name) + " level " + juce::String (level, 3)
                    + ": ref even/odd " + juce::String (ref.evenOdd, 4) + " (clean " + juce::String (refLow.evenOdd, 4) + ")"
                    + " h2 " + juce::String (ref.h2rel * 100.0, 2)
                    + "% thd " + juce::String (ref.thd * 100.0, 2) + "%  -> s " + juce::String (bestS, 5)
                    + " V, bmAsym " + juce::String (bmAsym, 4) + (asymInKUnits ? " (k units)" : " (bmYmax units)"));
        *reducedFlag = false;
    }

    template <typename Proc>
    void setById (Proc& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        if (std::getenv ("ASYM_BENCH") == nullptr)
            return;
        beginTest ("behavioural power-stage asymmetry vs the full netlist");
        juce::ScopedNoDenormals noDenormals;
        const double lv = std::getenv ("ASYM_LEVEL") != nullptr ? std::atof (std::getenv ("ASYM_LEVEL")) : 0.2;

        const juce::String only = std::getenv ("ASYM_ONLY") != nullptr ? std::getenv ("ASYM_ONLY") : "";
        const auto wanted = [&] (const char* n) { return only.isEmpty() || only == n; };
        if (wanted ("twin"))
        {
            TwinReverbStyleAmplifierProcessor amp;
            bench ("twin", amp, &TwinReverbStyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "tr_input", 1.0f); setById (a, "tr_volume", 0.8f); setById (a, "tr_power", 1.0f); },
                   TwinReverbStyleAmplifierProcessor::Probe::mixNode,
                   TwinReverbStyleAmplifierProcessor::Probe::speaker,
                   lv, TwinReverbStyleAmplifierProcessor::bmGain0, TwinReverbStyleAmplifierProcessor::bmYmax,
                   TwinReverbStyleAmplifierProcessor::bmKneeN, 459.48, true);
        }
        if (wanted ("deluxe"))
        {
            DeluxeReverbStyleAmplifierProcessor amp;
            bench ("deluxe", amp, &DeluxeReverbStyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "dr_input", 1.0f); setById (a, "dr_volume", 0.8f); setById (a, "dr_power", 1.0f); },
                   DeluxeReverbStyleAmplifierProcessor::Probe::mixNode,
                   DeluxeReverbStyleAmplifierProcessor::Probe::speaker,
                   lv, DeluxeReverbStyleAmplifierProcessor::bmGain0, DeluxeReverbStyleAmplifierProcessor::bmYmax,
                   DeluxeReverbStyleAmplifierProcessor::bmKneeN, 415.08, true);
        }
        if (wanted ("carr"))
        {
            CarrRamblerStyleAmplifierProcessor amp;
            bench ("carr", amp, &CarrRamblerStyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "cr_volume", 0.8f); setById (a, "cr_power", 1.0f); },
                   CarrRamblerStyleAmplifierProcessor::Probe::pOut,
                   CarrRamblerStyleAmplifierProcessor::Probe::speaker,
                   lv, CarrRamblerStyleAmplifierProcessor::bmGain0, CarrRamblerStyleAmplifierProcessor::bmYmax,
                   CarrRamblerStyleAmplifierProcessor::bmKneeN, 405.0, false);
        }
        if (wanted ("ftr37"))
        {
            DividedBy13FTR37StyleAmplifierProcessor amp;
            bench ("ftr37", amp, &DividedBy13FTR37StyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "f37_input", 1.0f); setById (a, "f37_ch1_volume", 0.8f); setById (a, "f37_ch2_volume", 0.8f); setById (a, "f37_power", 1.0f); },
                   DividedBy13FTR37StyleAmplifierProcessor::Probe::pOut,
                   DividedBy13FTR37StyleAmplifierProcessor::Probe::speaker,
                   lv, DividedBy13FTR37StyleAmplifierProcessor::bmGain0, DividedBy13FTR37StyleAmplifierProcessor::bmYmax,
                   DividedBy13FTR37StyleAmplifierProcessor::bmKneeN, 410.0, false);
        }
        if (wanted ("svt"))
        {
            SVTStyleAmplifierProcessor amp;
            bench ("svt", amp, &SVTStyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "svt_input", 0.0f); setById (a, "svt_gain", 0.8f); setById (a, "svt_master", 1.0f); setById (a, "svt_power", 1.0f); },
                   SVTStyleAmplifierProcessor::Probe::powerInput,
                   SVTStyleAmplifierProcessor::Probe::speaker,
                   lv, SVTStyleAmplifierProcessor::bmGain0, SVTStyleAmplifierProcessor::bmYmax,
                   SVTStyleAmplifierProcessor::bmKneeN, 690.0, true);
        }
        if (wanted ("trainwreck"))
        {
            TrainwreckExpressStyleAmplifierProcessor amp;
            bench ("trainwreck", amp, &TrainwreckExpressStyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "twx_volume", 0.8f); setById (a, "twx_power", 1.0f); },
                   TrainwreckExpressStyleAmplifierProcessor::Probe::powerInput,
                   TrainwreckExpressStyleAmplifierProcessor::Probe::speaker,
                   lv, 15.0 /* Spec::bmGain0 */, 0.10 /* Spec::bmYmax */,
                   TrainwreckExpressStyleAmplifierProcessor::bmKneeN, 395.0, true);
        }
        if (wanted ("komet"))
        {
            KometConcordeStyleAmplifierProcessor amp;
            bench ("komet", amp, &KometConcordeStyleAmplifierProcessor::reducedOrder,
                   [&] (auto& a) { setById (a, "kcd_volume", 0.8f); setById (a, "kcd_power", 1.0f); },
                   KometConcordeStyleAmplifierProcessor::Probe::powerInput,
                   KometConcordeStyleAmplifierProcessor::Probe::speaker,
                   lv, 15.0 /* Spec::bmGain0 */, 0.10 /* Spec::bmYmax */,
                   KometConcordeStyleAmplifierProcessor::bmKneeN, 460.0, true);
        }
    }
};

static PowerStageAsymBench powerStageAsymBench;

} // namespace openguitarmultifx
