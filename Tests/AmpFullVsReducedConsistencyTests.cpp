#include "Effects/AC15StyleAmplifierProcessor.h"
#include "Effects/AC30StyleAmplifierProcessor.h"
#include "Effects/Acoustic360StyleAmplifierProcessor.h"
#include "Effects/AmpegB15StyleAmplifierProcessor.h"
#include "Effects/AmpegV4BStyleAmplifierProcessor.h"
#include "Effects/Astroverb16StyleAmplifierProcessor.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"
#include "Effects/BognerUberschallStyleAmplifierProcessor.h"
#include "Effects/CarmenGhiaStyleAmplifierProcessor.h"
#include "Effects/CarrRamblerStyleAmplifierProcessor.h"
#include "Effects/DeluxeReverbStyleAmplifierProcessor.h"
#include "Effects/DividedBy13FTR37StyleAmplifierProcessor.h"
#include "Effects/DiezelStyleAmplifierProcessor.h"
#include "Effects/DualRectifierStyleAmplifierProcessor.h"
#include "Effects/DumbleSteelStringStyleAmplifierProcessor.h"
#include "Effects/ENGLPowerballStyleAmplifierProcessor.h"
#include "Effects/EVH5150StyleAmplifierProcessor.h"
#include "Effects/FryetteDeliveranceD120StyleAmplifierProcessor.h"
#include "Effects/GarnetHerzogStyleAmplifierProcessor.h"
#include "Effects/GibsonEH150StyleAmplifierProcessor.h"
#include "Effects/HotCat30StyleAmplifierProcessor.h"
#include "Effects/JCM800StyleAmplifierProcessor.h"
#include "Effects/JTM45StyleAmplifierProcessor.h"
#include "Effects/MarkIICPlusStyleAmplifierProcessor.h"
#include "Effects/MatchlessHC30StyleAmplifierProcessor.h"
#include "Effects/MesaBass400PlusStyleAmplifierProcessor.h"
#include "Effects/RiveraKnuckleheadStyleAmplifierProcessor.h"
#include "Effects/RockerverbStyleAmplifierProcessor.h"
#include "Effects/SLO100StyleAmplifierProcessor.h"
#include "Effects/SVTStyleAmplifierProcessor.h"
#include "Effects/KometConcordeStyleAmplifierProcessor.h"
#include "Effects/SunnModelTStyleAmplifierProcessor.h"
#include "Effects/SuperLeadStyleAmplifierProcessor.h"
#include "Effects/TrainwreckExpressStyleAmplifierProcessor.h"
#include "Effects/TwinReverbStyleAmplifierProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/**
    Full-vs-reduced consistency sweep for every modeled amp that ships a reducedOrder path.
    After the PowerStageTail work, the fitted bmAsym curves and the built-in CabSim voicing, each
    amp's reduced-order path should track its own full-netlist reference. Both paths emit to the
    same delivered-level scale (outputScale vs fullOutputScale are independent fits), so the emitted
    buffer is the right comparison: what a listener hears.

    Measurements: RMS level delta (dB), per-band spectral delta after removing the level offset
    (bass 60-300, mid 300-1600, treble 1.6k-12k -- a tonal-balance error), THD and even:odd
    harmonic-ratio delta on a coherent sine (sweep mode only).

    Two modes:
      * always-on: a ~1 s decaying-chord render at two drive levels per amp, asserted against the
        achieved per-amp bounds in the table below -- this is the regression lock.
        (Category "Probe": full-netlist renders are minutes of DSP -- too heavy for the emulated
        aarch64 shards, which skip this category; the serial native suite still runs it.)
      * AMP_SWEEP=1 (dev only): the full report -- the guitar DI fixture through a drive sweep plus
        a coherent sine for THD/even-odd, one parseable line per setting. AMP_ONLY=name1,name2
        restricts to a subset so several shards can run in parallel.
*/
class AmpFullVsReducedConsistencyTests : public juce::UnitTest
{
public:
    AmpFullVsReducedConsistencyTests() : juce::UnitTest ("AmpFullVsReducedConsistency", "Probe") {}

    static constexpr double sr = 48000.0;
    static constexpr int nWin = 8192;
    static constexpr int fftOrder = 15, fftN = 1 << fftOrder;

    /** A decaying three-note chord (E2, B2, E3 with harmonics), `rms` before the amp -- the same
        stimulus ReducedOrderEquivalenceTests uses for the pedals. */
    static std::vector<float> chord (double rms, double seconds, double fs)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const int total = (int) (seconds * fs);
        std::vector<float> in ((size_t) total);
        double norm = 0.0;
        for (int k = 1; k <= 8; ++k)
            norm += 0.5 / (double) (k * k);
        norm *= 3.0;
        const double scale = rms / std::sqrt (norm);
        for (int n = 0; n < total; ++n)
        {
            const double t = (double) n / fs;
            double v = 0.0;
            for (double f0 : { 82.41, 123.47, 164.81 })
                for (int k = 1; k <= 8; ++k)
                    v += std::sin (twoPi * f0 * k * t + 0.3 * k) / (double) k * std::exp (-t * (0.6 + 0.15 * k));
            in[(size_t) n] = (float) (scale * v * 1.6);
        }
        return in;
    }

    static std::vector<float> sine (double amp, double hz, double seconds, double fs)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const int total = (int) (seconds * fs);
        std::vector<float> in ((size_t) total);
        for (int n = 0; n < total; ++n)
            in[(size_t) n] = (float) (amp * std::sin (twoPi * hz * (double) n / fs));
        return in;
    }

    /** The guitar DI fixture (Tests/fixtures/di/guitar-di.wav), mono-summed, truncated. */
    static bool loadDi (double seconds, std::vector<float>& mono, double& fsOut)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        const auto f = juce::File (OGMFX_TEST_FIXTURES_DIR).getChildFile ("fixtures").getChildFile ("di").getChildFile ("guitar-di.wav");
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
            return false;
        const int n = (int) juce::jmin ((juce::int64) (seconds * r->sampleRate), r->lengthInSamples);
        juce::AudioBuffer<float> buf ((int) r->numChannels, n);
        r->read (&buf, 0, n, 0, true, true);
        mono.assign ((size_t) n, 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();
        fsOut = r->sampleRate;
        return true;
    }

    /** Knob settings each amp's own "tracks the reference" calibration used: drive the master/power
        stage to its fitted operating point. Suffix rules cover every amp (param ids end in
        _power/_master/_input); a few amps get explicit overrides from their own suite below. */
    static void applyCalibParams (EffectProcessor& fx)
    {
        const auto pages = fx.getParameterPages();
        static const std::pair<const char*, float> rules[] = {
            { "_power", 1.0f }, { "_master", 1.0f }, { "_input", 0.0f },
            { "_volume", 0.8f }, { "_loud1", 0.7f }, { "_loud2", 0.7f },
            { "_vol_normal", 0.8f }, { "_vol_bright", 0.8f },
        };
        for (const auto& page : pages)
            for (auto* f : page)
                for (const auto& r : rules)
                    if (juce::String (f->paramID).endsWith (r.first))
                        *f = juce::jlimit (f->range.start, f->range.end, r.second);
        // AMP_PARAMS="id=v,id=v": extra knob settings applied to both paths (dev sweeps).
        static const juce::String over = std::getenv ("AMP_PARAMS") != nullptr ? std::getenv ("AMP_PARAMS") : "";
        for (const auto& kv : juce::StringArray::fromTokens (over, ",", {}))
        {
            const auto id = kv.upToFirstOccurrenceOf ("=", false, false).trim();
            if (id.isEmpty()) continue;
            const float v = (float) kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            bool found = false;
            for (const auto& page : pages)
                for (auto* f : page)
                    if (f->paramID == id) { *f = v; found = true; }
            if (! found)
                juce::Logger::writeToLog ("AMP_PARAMS: unknown param " + id);
        }
    }

    /** Renders `in` through a fresh amp instance under the given mode at the calibration operating
        point. Returns the volts-domain speaker signal (debugVoltage(Probe::speaker)): bmOutput for
        the reduced path, wOut for the full netlist -- the exact quantity the reduced stage was
        fitted against (the per-amp "tracks the reference" tests measure this same way). Falls back
        to the emitted buffer for amps without a Probe enum. The flag must hold for the whole
        render: process() dispatches on it per sample, so the flag is flipped around the amp's
        entire lifetime, never mid-stream. */
    template <typename Amp>
    static std::vector<float> renderMode (const std::vector<float>& in, double fs, bool reduced, double* failRate)
    {
        const bool prev = Amp::reducedOrder;
        Amp::reducedOrder = reduced;
        std::vector<float> out;
        out.reserve (in.size());
        {
            Amp amp;
            applyCalibParams (amp);
            amp.prepare (fs, 128, 2);
            // Probe-domain comparison needs a per-sample debugVoltage() read -- a process() call only
            // exposes the model state at call boundaries, so the volts-domain render pushes one-sample
            // buffers (the same shape the per-amp "tracks the reference" tests use).
            constexpr bool probeDomain = requires { std::declval<Amp&>().debugVoltage (Amp::Probe::speaker); };
            juce::AudioBuffer<float> buf (2, probeDomain ? 1 : 128);
            // The full netlist's supply/rail caps charge from zero-state: the reference-level tests
            // measure after ~1.4 s of drive. Give both paths the same settle time so the comparison
            // lands in the model's steady state, not its power-on transient. AMP_WARM overrides.
            const long long warmSamples = (long long) ((std::getenv ("AMP_WARM") != nullptr
                                                          ? std::atof (std::getenv ("AMP_WARM")) : 1.5) * fs);
            const int blk = buf.getNumSamples();
            // Warmup drives the input signal (looped), not silence: several reference netlists settle
            // into a dead-NFB low-gain state when the solver only ever sees silence, and only reach
            // the fitted healthy operating point under continuous drive (same reason the per-amp
            // level-tracking tests ramp drive for seconds before measuring).
            for (long long n0 = 0; n0 < warmSamples; n0 += blk)
            {
                buf.clear();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blk; ++i)
                        buf.setSample (ch, i, in[(size_t) (n0 + i) % in.size()]);
                amp.process (buf);
            }
            for (size_t n0 = 0; n0 < in.size(); n0 += (size_t) blk)
            {
                const int len = (int) juce::jmin ((size_t) blk, in.size() - n0);
                buf.clear();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < len; ++i)
                        buf.setSample (ch, i, in[n0 + (size_t) i]);
                amp.process (buf);
                for (int i = 0; i < len; ++i)
                {
                    double y;
                    if constexpr (probeDomain)
                        y = amp.debugVoltage (Amp::Probe::speaker);
                    else
                        y = buf.getSample (0, i);
                    out.push_back ((float) y);
                }
            }
            if (failRate != nullptr)
                *failRate = amp.getSolveFailureRate();
            if (std::getenv ("AMP_DEBUG") != nullptr)
            {
                juce::String dbg = "  mode " + juce::String (reduced ? "red" : "full")
                                 + " failRate " + juce::String (amp.getSolveFailureRate(), 6);
                if constexpr (requires { amp.debugSanityRejects(); })
                    dbg += " sanityRejects " + juce::String (amp.debugSanityRejects());
                if constexpr (requires { amp.debugWorstSaneVolts(); })
                    dbg += " worstSaneV " + juce::String (amp.debugWorstSaneVolts(), 1);
                if constexpr (requires { amp.debugRecoveries(); })
                    dbg += " recoveries " + juce::String (amp.debugRecoveries());
                juce::Logger::writeToLog (dbg);
            }
        }
        Amp::reducedOrder = prev;
        return out;
    }

    /** Octave-band energies (dB) of the steady part of a render, plus the total over 60-12000 Hz. */
    struct Spectrum
    {
        double bandDb[8];
        double totalDb;
        double mag[fftN / 2];
        double fs = sr;
    };

    static Spectrum spectrum (const std::vector<float>& x, double fs)
    {
        Spectrum s {};
        s.fs = fs;
        std::vector<float> data ((size_t) fftN * 2, 0.0f);
        const size_t start = (size_t) (0.25 * fs); // skip the attack's first 250 ms
        for (int i = 0; i < fftN && start + (size_t) i < x.size(); ++i)
            data[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) fftN));
        juce::dsp::FFT fft (fftOrder);
        fft.performFrequencyOnlyForwardTransform (data.data());
        for (int k = 0; k < fftN / 2; ++k)
            s.mag[k] = data[(size_t) k] * data[(size_t) k];
        const double edges[9] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000 };
        double total = 0.0;
        for (int band = 0; band < 8; ++band)
        {
            double e = 0.0;
            for (int k = (int) (edges[band] / fs * fftN); k < (int) (edges[band + 1] / fs * fftN); ++k)
                e += s.mag[k];
            s.bandDb[band] = 10.0 * std::log10 (e + 1.0e-30);
            total += e;
        }
        // Total in the three report bands (60-12k) rather than the octave edges: the level delta
        // should cover what the band deltas cover.
        double t3 = 0.0;
        for (int k = (int) (60.0 / fs * fftN); k < (int) (12000.0 / fs * fftN); ++k)
            t3 += s.mag[k];
        s.totalDb = 10.0 * std::log10 (t3 + 1.0e-30);
        (void) total;
        return s;
    }

    static double bandDb (const Spectrum& s, double lo, double hi)
    {
        double e = 0.0;
        for (int k = (int) (lo / s.fs * fftN); k < (int) (hi / s.fs * fftN); ++k)
            e += s.mag[k];
        return 10.0 * std::log10 (e + 1.0e-30);
    }

    /** The three report bands: bass 60-300, mid 300-1600, treble 1.6k-12k. */
    static void reportBands (const Spectrum& s, double out[3])
    {
        out[0] = bandDb (s, 60.0, 300.0);
        out[1] = bandDb (s, 300.0, 1600.0);
        out[2] = bandDb (s, 1600.0, 12000.0);
    }

    struct Pair
    {
        double levelDb = 0.0;     // emitted RMS delta, reduced - full
        double fullDb = 0.0, redDb = 0.0; // absolute emitted level of each path
        double shapeDb[3] = {};   // per-band delta with the level offset removed (tonal error)
        double failFull = 0.0, failRed = 0.0;
    };

    template <typename Amp>
    static Pair compareRender (const std::vector<float>& in, double fs)
    {
        Pair p;
        const auto full = renderMode<Amp> (in, fs, false, &p.failFull);
        const auto red = renderMode<Amp> (in, fs, true, &p.failRed);
        const auto sf = spectrum (full, fs), sb = spectrum (red, fs);
        p.levelDb = sb.totalDb - sf.totalDb;
        p.fullDb = sf.totalDb;
        p.redDb = sb.totalDb;
        double bf[3], bb[3];
        reportBands (sf, bf);
        reportBands (sb, bb);
        for (int i = 0; i < 3; ++i)
            p.shapeDb[i] = (bb[i] - sb.totalDb) - (bf[i] - sf.totalDb);
        return p;
    }

    // THD + even:odd on a coherent-window single tone (same DFT approach as PowerStageAsymBench).
    struct Harm { double thd = 0.0, evenOdd = 0.0; };

    static Harm harmonics (const std::vector<float>& yAll)
    {
        // last nWin samples, rectangular window: the tone frequency is chosen coherent in nWin.
        std::vector<double> y (yAll.end() - juce::jmin ((int) yAll.size(), nWin), yAll.end());
        const int n = (int) y.size();
        const int nBins = juce::jmin (n / 2, 512);
        double bestF = 0.0, bestMag = -1.0;
        std::vector<double> mag ((size_t) nBins, 0.0);
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
        for (int k = 2; k * f0 < nBins && k <= 10; ++k)
            ss += h (k) * h (k);
        return { bestMag > 1.0e-12 ? std::sqrt (ss) / bestMag : 0.0,
                 odd > 1.0e-12 ? even / odd : 0.0 };
    }

    /** Per-amp regression bounds measured by the sweep at the calibration operating point
        (power/master = 1.0, volts domain = Probe::speaker, the domain the reduced stages are
        fitted in). `levelDb`/`bandDb` bound |delta| against the full-netlist reference (all previously-dead
        netlists were healed -- see CircuitFamilies.md's dead-reference repair section -- so every
        amp now carries a real tracking bound). A negative levelDb marks a DOCUMENTED-dead reference
        netlist (dead NFB / gain-collapse, ~32-57 dB below the reduced path): for those,
        `redAbsDb` locks the reduced path's own absolute level (sine 0.2, dB) so the shipped
        behavioural stage can drift but never silently die, and |level| is only checked to still
        indicate the collapsed regime. Re-measure with AMP_SWEEP=1 after changing a reduced path. */
    struct Bound { const char* ampName; double levelDb; double bandDb; double redAbsDb; };
    static const Bound* boundFor (const char* ampName)
    {
        static constexpr Bound b[] = {
            { "superlead",   4.0,  8.0, 0.0 }, { "bassman",      4.0, 16.0, 0.0 },
            { "twin",        3.0,  5.0, 0.0 }, { "deluxe",       7.0, 26.0, 0.0 },
            { "jtm45",       7.0, 26.0, 0.0 }, { "jcm800",       4.0,  6.0, 0.0 },
            { "ac15",        5.0, 10.0, 0.0 }, { "ac30",         6.0,  6.0, 0.0 },
            { "matchless",   3.0,  5.0, 0.0 }, { "hotcat",       3.0, 11.0, 0.0 },
            { "carmenghia",  4.0,  8.0, 0.0 }, { "astroverb",    4.0,  7.0, 0.0 },
            { "slo100",       1.0,  4.0, 0.0 }, { "markiic",      5.0,  6.0, 0.0 },
            { "dualrect",     4.0,  5.0, 0.0 }, { "evh5150",      2.0,  2.0, 0.0 },
            { "powerball",    7.0,  3.0, 0.0 }, { "rockerverb",  17.0,  8.0, 0.0 },
            { "eh150",       2.0,  4.0, 0.0 }, { "herzog",       4.0,  7.0, 0.0 },
            { "knucklehead", 4.0, 12.0, 0.0 }, { "diezel",       4.0,  6.0, 0.0 },
            { "carr",        1.5,  5.5, 0.0 },
            { "ftr37",       2.0,  4.0, 0.0 }, { "dumble",       4.0,  9.0, 0.0 },
            { "sunn",         1.5,  3.0, 0.0 }, { "deliverance",  3.0,  4.0, 0.0 },
            { "uberschall",   3.0, 14.0, 0.0 }, { "b15",         2.0,  4.0, 0.0 },
            { "v4b",         2.0,  4.0, 0.0 }, { "mesa400",     2.0,  4.0, 0.0 },
            { "acoustic360", 4.0,  9.0, 0.0 }, { "svt",         2.5,  5.0, 0.0 },
            { "trainwreck",   1.5,  1.5, 0.0 }, { "komet",       1.5,  5.0, 0.0 },
        };
        for (const auto& e : b)
            if (juce::String (e.ampName) == ampName)
                return &e;
        return nullptr;
    }

    /** Locked-mode check: the chord at a quiet and a driven level; the worst delta across both
        must stay inside the amp's measured bounds. */
    template <typename Amp>
    void lockAmp (const char* ampName)
    {
        if (std::getenv ("AMP_SILENCE") != nullptr)
        {
            const std::vector<float> z (48000, 0.0f);
            for (bool reduced : { false, true })
            {
                const auto y = renderMode<Amp> (z, sr, reduced, nullptr);
                double ss = 0.0;
                for (float v : y) ss += (double) v * v;
                const auto s = spectrum (y, sr);
                double bf2[3];
                reportBands (s, bf2);
                logMessage (juce::String (ampName) + " SILENCE " + juce::String (reduced ? "red" : "full")
                            + " rms " + juce::String (20.0 * std::log10 (std::sqrt (ss / y.size()) + 1.0e-30), 1)
                            + " bands " + juce::String (bf2[0], 1) + "/" + juce::String (bf2[1], 1)
                            + "/" + juce::String (bf2[2], 1));
            }
        }
        double worstLevel = 0.0, worstBand = 0.0;
        for (double rms : { 0.03, 0.4 })
        {
            const auto in = chord (rms, 1.0, sr);
            double ff = 0.0, fr = 0.0;
            const auto a = renderMode<Amp> (in, sr, false, &ff);
            const auto b = renderMode<Amp> (in, sr, true, &fr);
            const auto voltsRms = [] (const std::vector<float>& v, size_t skip)
            {
                double s = 0.0;
                for (size_t i = skip; i < v.size(); ++i) s += (double) v[i] * v[i];
                return 20.0 * std::log10 (std::sqrt (s / (double) (v.size() - skip)) + 1.0e-30);
            };
            logMessage (juce::String (ampName) + " voltsRms full " + juce::String (voltsRms (a, (size_t) (0.25 * sr)), 2)
                        + " red " + juce::String (voltsRms (b, (size_t) (0.25 * sr)), 2));
            const auto sf = spectrum (a, sr), sb = spectrum (b, sr);
            const double level = sb.totalDb - sf.totalDb;
            double bf[3], bb[3];
            reportBands (sf, bf);
            reportBands (sb, bb);
            double band = 0.0;
            for (int i = 0; i < 3; ++i)
                if (bf[i] > sf.totalDb - 40.0) // a band the reference leaves 40 dB under its total is inaudible: its dB
                                               // ratio is noise (Acoustic 360's clean treble band sat 122 dB down)
                    band = juce::jmax (band, std::abs ((bb[i] - sb.totalDb) - (bf[i] - sf.totalDb)));
            logMessage (juce::String (ampName) + " rms " + juce::String (rms, 2)
                        + ": level " + juce::String (level, 2) + " dB, worst shape band "
                        + juce::String (band, 2) + " dB (bass " + juce::String (bb[0] - bf[0], 2)
                        + ", mid " + juce::String (bb[1] - bf[1], 2)
                        + ", treb " + juce::String (bb[2] - bf[2], 2) + ")"
                        + (std::getenv ("AMP_BANDS") != nullptr
                               ? " [full " + juce::String (bf[0], 1) + "/" + juce::String (bf[1], 1)
                                 + "/" + juce::String (bf[2], 1) + " red " + juce::String (bb[0], 1)
                                 + "/" + juce::String (bb[1], 1) + "/" + juce::String (bb[2], 1) + "]"
                               : ""));
            worstLevel = juce::jmax (worstLevel, std::abs (level));
            worstBand = juce::jmax (worstBand, band);
            expectLessThan (ff, 2.0e-2, juce::String (ampName) + ": full path solver fails");
            expectLessThan (fr, 2.0e-2, juce::String (ampName) + ": reduced path solver fails");
        }
        const Bound* bd = boundFor (ampName);
        if (bd == nullptr)
        {
            expect (false, juce::String (ampName) + ": no bound entry");
            return;
        }
        if (bd->levelDb < 0.0)
        {
            // Documented dead reference: the full netlist is known to collapse ~32-57 dB below the
            // shipped reduced stage at real knob settings (see the amp header's reduced-order note).
            // "Fit reduced to own netlist" is meaningless here -- lock the reduced path's absolute
            // level instead so the shipped stage never silently dies, and check the reference still
            // occupies the collapsed regime (if someone repairs the netlist, |level| shrinks and this
            // amp graduates to a real tracking bound).
            const auto in = sine (0.2, 100.0, 1.0, sr);
            double fr = 0.0;
            const auto b = renderMode<Amp> (in, sr, true, &fr);
            double ss = 0.0;
            for (size_t i = (size_t) (0.25 * sr); i < b.size(); ++i) ss += (double) b[i] * b[i];
            const double redAbs = 20.0 * std::log10 (std::sqrt (ss / (b.size() - (size_t) (0.25 * sr))) + 1.0e-30);
            logMessage (juce::String (ampName) + " dead-reference amp: reduced level " + juce::String (redAbs, 1)
                        + " dB (expected ~" + juce::String (bd->redAbsDb, 0) + "), full-vs-reduced "
                        + juce::String (worstLevel, 1) + " dB (collapsed regime)");
            expectWithinAbsoluteError (redAbs, bd->redAbsDb, 12.0,
                                       juce::String (ampName) + ": reduced path level drifted");
            expect (worstLevel > 20.0, juce::String (ampName) + ": reference no longer collapsed -- retighten this bound");
            return;
        }
        logMessage (juce::String (ampName) + " worst: level " + juce::String (worstLevel, 2)
                    + " dB (bound " + juce::String (bd->levelDb, 1) + "), band "
                    + juce::String (worstBand, 2) + " dB (bound " + juce::String (bd->bandDb, 1) + ")");
        expectLessThan (worstLevel, bd->levelDb, juce::String (ampName) + ": level delta exceeds bound");
        expectLessThan (worstBand, bd->bandDb, juce::String (ampName) + ": band delta exceeds bound");
    }

    /** Sweep mode: the DI fixture at three input scales (drive sweep) plus a coherent sine for
        THD and even:odd. One parseable line per setting. */
    template <typename Amp>
    void sweepAmp (const char* ampName)
    {
        static std::vector<float> di;
        static double diFs = 0.0;
        if (di.empty())
        {
            if (! loadDi (5.0, di, diFs))
            {
                expect (false, "DI fixture missing -- check Tests/fixtures/di/guitar-di.wav");
                return;
            }
        }
        for (double scale : { 1.0, 2.5, 6.3 })
        {
            std::vector<float> in (di.size());
            for (size_t i = 0; i < di.size(); ++i)
                in[i] = di[i] * (float) scale;
            const auto p = compareRender<Amp> (in, diFs);
            logMessage ("SWEEP " + juce::String (ampName) + " di x" + juce::String (scale, 1)
                        + " lvl " + juce::String (p.levelDb, 2)
                        + " abs " + juce::String (p.fullDb, 1) + "/" + juce::String (p.redDb, 1)
                        + " band " + juce::String (p.shapeDb[0], 2) + " " + juce::String (p.shapeDb[1], 2)
                        + " " + juce::String (p.shapeDb[2], 2)
                        + (p.failFull > 1.0e-6 || p.failRed > 1.0e-6
                               ? " FAIL " + juce::String (p.failFull, 4) + "/" + juce::String (p.failRed, 4)
                               : ""));
        }
        // THD + even:odd on a coherent ~111 Hz tone at three drive levels.
        const double tone = 19.0 * sr / (double) nWin;
        for (double lvl : { 0.05, 0.2, 0.6 })
        {
            const auto in = sine (lvl, tone, 1.0, sr);
            const auto p = compareRender<Amp> (in, sr);
            const Harm hf = harmonics (renderMode<Amp> (in, sr, false, nullptr));
            const Harm hb = harmonics (renderMode<Amp> (in, sr, true, nullptr));
            logMessage ("SWEEP " + juce::String (ampName) + " sine " + juce::String (lvl, 2)
                        + " lvl " + juce::String (p.levelDb, 2)
                        + " abs " + juce::String (p.fullDb, 1) + "/" + juce::String (p.redDb, 1)
                        + " thd " + juce::String (hf.thd * 100.0, 2) + "/" + juce::String (hb.thd * 100.0, 2)
                        + " evenodd " + juce::String (hf.evenOdd, 3) + "/" + juce::String (hb.evenOdd, 3));
        }
    }


    void runTest() override
    {
        juce::ScopedNoDenormals noDenormals;
        const bool sweep = std::getenv ("AMP_SWEEP") != nullptr;
        const juce::String only = std::getenv ("AMP_ONLY") != nullptr ? std::getenv ("AMP_ONLY") : "";
        const auto wanted = [&] (const char* n)
        {
            if (only.isEmpty())
                return true;
            for (const auto& w : juce::StringArray::fromTokens (only, ",", {}))
                if (w.trim().equalsIgnoreCase (n))
                    return true;
            return false;
        };

        if (sweep)
            beginTest ("sweep: every reducedOrder amp vs its full netlist (DI drive sweep + THD)");
        else
            beginTest ("every reducedOrder amp tracks its full-netlist reference within the locked bounds");

#define AMP_ENTRY(shortName, AmpClass) \
        if (wanted (shortName)) { if (sweep) sweepAmp<AmpClass> (shortName); else lockAmp<AmpClass> (shortName); }

        AMP_ENTRY ("superlead",   SuperLeadStyleAmplifierProcessor)
        AMP_ENTRY ("bassman",     BassmanStyleAmplifierProcessor)
        AMP_ENTRY ("twin",        TwinReverbStyleAmplifierProcessor)
        AMP_ENTRY ("deluxe",      DeluxeReverbStyleAmplifierProcessor)
        AMP_ENTRY ("jtm45",       JTM45StyleAmplifierProcessor)
        AMP_ENTRY ("jcm800",      JCM800StyleAmplifierProcessor)
        AMP_ENTRY ("ac15",        AC15StyleAmplifierProcessor)
        AMP_ENTRY ("ac30",        AC30StyleAmplifierProcessor)
        AMP_ENTRY ("matchless",   MatchlessHC30StyleAmplifierProcessor)
        AMP_ENTRY ("hotcat",      HotCat30StyleAmplifierProcessor)
        AMP_ENTRY ("carmenghia",  CarmenGhiaStyleAmplifierProcessor)
        AMP_ENTRY ("astroverb",   Astroverb16StyleAmplifierProcessor)
        AMP_ENTRY ("slo100",      SLO100StyleAmplifierProcessor)
        AMP_ENTRY ("markiic",     MarkIICPlusStyleAmplifierProcessor)
        AMP_ENTRY ("dualrect",    DualRectifierStyleAmplifierProcessor)
        AMP_ENTRY ("evh5150",     EVH5150StyleAmplifierProcessor)
        AMP_ENTRY ("powerball",   ENGLPowerballStyleAmplifierProcessor)
        AMP_ENTRY ("rockerverb",  RockerverbStyleAmplifierProcessor)
        AMP_ENTRY ("eh150",       GibsonEH150StyleAmplifierProcessor)
        AMP_ENTRY ("herzog",      GarnetHerzogStyleAmplifierProcessor)
        AMP_ENTRY ("knucklehead", RiveraKnuckleheadStyleAmplifierProcessor)
        AMP_ENTRY ("diezel",      DiezelStyleAmplifierProcessor)
        AMP_ENTRY ("carr",        CarrRamblerStyleAmplifierProcessor)
        AMP_ENTRY ("ftr37",       DividedBy13FTR37StyleAmplifierProcessor)
        AMP_ENTRY ("dumble",      DumbleSteelStringStyleAmplifierProcessor)
        AMP_ENTRY ("sunn",        SunnModelTStyleAmplifierProcessor)
        AMP_ENTRY ("deliverance", FryetteDeliveranceD120StyleAmplifierProcessor)
        AMP_ENTRY ("uberschall",  BognerUberschallStyleAmplifierProcessor)
        AMP_ENTRY ("b15",         AmpegB15StyleAmplifierProcessor)
        AMP_ENTRY ("v4b",         AmpegV4BStyleAmplifierProcessor)
        AMP_ENTRY ("mesa400",     MesaBass400PlusStyleAmplifierProcessor)
        AMP_ENTRY ("acoustic360", Acoustic360StyleAmplifierProcessor)
        AMP_ENTRY ("svt",        SVTStyleAmplifierProcessor)
        AMP_ENTRY ("trainwreck", TrainwreckExpressStyleAmplifierProcessor)
        AMP_ENTRY ("komet",      KometConcordeStyleAmplifierProcessor)

#undef AMP_ENTRY
    }
};

static AmpFullVsReducedConsistencyTests ampFullVsReducedConsistencyTests;

} // namespace openguitarmultifx
