#include "Effects/BigMuffStyleFuzzProcessor.h"
#include "Effects/BassBigMuffStyleFuzzProcessor.h"
#include "Effects/MetalZoneStyleDistortionProcessor.h"
#include "Effects/DT1StyleDistortionProcessor.h"
#include "Effects/HM2StyleDistortionProcessor.h"
#include "PipelineEngineEquivalence.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/**
    A pipeline engine (linear network + fixed-cost nonlinear ports, pipeline::LinearPortNetwork) must sound like the
    full Newton netlist it replaces: same level, same timbre (octave-band shape with the level removed), finite output,
    and it must actually be cheaper. Same chord, levels and bounds as ReducedOrderEquivalenceTests.
*/
class PipelineEquivalenceTests : public juce::UnitTest
{
public:
    PipelineEquivalenceTests() : juce::UnitTest ("PipelineEquivalence", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setParams (EffectProcessor& p, const std::vector<std::pair<juce::String, float>>& values)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                for (auto& v : values)
                    if (f->paramID == v.first)
                        *f = v.second;
    }

    static std::vector<float> render (EffectProcessor& p, double rms, double seconds, double* cpuSeconds = nullptr)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const int block = 256, total = (int) (seconds * sr);
        std::vector<float> out;
        out.reserve ((size_t) total);
        juce::AudioBuffer<float> buf (1, block);
        double norm = 0.0;
        for (int k = 1; k <= 8; ++k) norm += 0.5 / (double) (k * k);
        norm *= 3.0;
        const double scale = rms / std::sqrt (norm);
        double cpu = 0.0;
        for (int n0 = 0; n0 < total; n0 += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const double t = (double) (n0 + i) / sr;
                double v = 0.0;
                for (double f0 : { 82.41, 123.47, 164.81 })
                    for (int k = 1; k <= 8; ++k)
                        v += std::sin (twoPi * f0 * k * t + 0.3 * k) / (double) k * std::exp (-t * (0.6 + 0.15 * k));
                buf.setSample (0, i, (float) (scale * v * 1.6));
            }
            const auto t0 = std::chrono::steady_clock::now();
            p.process (buf);
            cpu += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
        }
        if (cpuSeconds != nullptr) *cpuSeconds = cpu;
        return out;
    }

    struct Bands { double db[8]; double totalDb; };
    static Bands analyse (const std::vector<float>& x)
    {
        constexpr int order = 15, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) n * 2, 0.0f);
        const size_t start = 12000;
        for (int i = 0; i < n && start + (size_t) i < x.size(); ++i)
            data[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) n));
        fft.performFrequencyOnlyForwardTransform (data.data());
        Bands b {};
        double total = 0.0;
        const double edges[9] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000 };
        for (int band = 0; band < 8; ++band)
        {
            double e = 0.0;
            for (int k = (int) (edges[band] / sr * n); k < (int) (edges[band + 1] / sr * n); ++k)
                e += (double) data[(size_t) k] * data[(size_t) k];
            b.db[band] = 10.0 * std::log10 (e + 1.0e-30);
            total += e;
        }
        b.totalDb = 10.0 * std::log10 (total + 1.0e-30);
        return b;
    }

    void runTest() override
    {
        beginTest ("Big Muff pipeline engine sounds like the full netlist and costs less");
        struct M { BigMuffStyleFuzzProcessor::Model model; const char* tag; juce::String prefix; };
        const M models[] = { { BigMuffStyleFuzzProcessor::Model::usV3, "USA", "bmp" },
                             { BigMuffStyleFuzzProcessor::Model::russianGreen, "Russian", "bmpru" },
                             { BigMuffStyleFuzzProcessor::Model::sovtekFirstEdition, "Sovtek", "bmpsv" } };
        double worstLevel = 0.0, worstBand = 0.0, cpuFull = 0.0, cpuPipe = 0.0;
        BigMuffStyleFuzzProcessor::reducedOrder = false;
        for (const auto& m : models)
            for (float sus : { 0.3f, 0.8f })
                for (float tone : { 0.2f, 0.8f })
                    for (double rms : { 0.05, 0.3 })
                    {
                        std::vector<float> out[2];
                        for (int engine = 0; engine < 2; ++engine)
                        {
                            BigMuffStyleFuzzProcessor pedal (m.model, engine == 1);
                            setParams (pedal, { { m.prefix + "_sustain", sus }, { m.prefix + "_tone", tone }, { m.prefix + "_volume", 0.7f } });
                            pedal.prepare (sr, 256, 1);
                            double cpu = 0.0;
                            out[engine] = render (pedal, rms, 3.0, &cpu);
                            (engine == 0 ? cpuFull : cpuPipe) += cpu;
                        }
                        bool finite = true;
                        for (auto v : out[1]) finite = finite && std::isfinite (v);
                        expect (finite, "non-finite pipeline output");
                        const auto a = analyse (out[0]), b = analyse (out[1]);
                        const double level = b.totalDb - a.totalDb;
                        double band = 0.0;
                        for (int i = 0; i < 8; ++i)
                            if (a.db[i] > a.totalDb - 30.0)
                                band = std::max (band, std::abs ((b.db[i] - b.totalDb) - (a.db[i] - a.totalDb)));
                        juce::String bandsTxt;
                        for (int i = 0; i < 8; ++i)
                            bandsTxt << juce::String ((b.db[i] - b.totalDb) - (a.db[i] - a.totalDb), 1) << (a.db[i] > a.totalDb - 30.0 ? "* " : " ");
                        logMessage ("    bands (pipeline - full, * audible): " + bandsTxt);
                        logMessage ("  " + juce::String (m.tag) + " sus " + juce::String (sus) + " tone " + juce::String (tone) + " in "
                                    + juce::String (rms) + ": level " + juce::String (level, 2) + " dB, worst band " + juce::String (band, 2) + " dB");
                        worstLevel = std::max (worstLevel, std::abs (level));
                        worstBand = std::max (worstBand, band);
                    }
        logMessage ("  worst level " + juce::String (worstLevel, 2) + " dB, worst band " + juce::String (worstBand, 2)
                    + " dB; CPU full " + juce::String (cpuFull, 3) + " s vs pipeline " + juce::String (cpuPipe, 3) + " s (x"
                    + juce::String (cpuFull / juce::jmax (1e-9, cpuPipe), 1) + ")");
        expect (worstLevel < 2.0, "level within 2 dB");
        expect (worstBand < 1.2, "timbre within 1.2 dB per audible octave band");
        expect (cpuPipe < cpuFull, "the pipeline engine must be cheaper");

        beginTest ("Big Muff pipeline: transients -- peaks, attack/decay envelopes and onset timing match the netlist");
        {
            double worstPeak = 0.0, worstEnv = 0.0;
            int worstOnset = 0;
            for (const auto& m : models)
                for (float sus : { 0.2f, 0.9f })
                    for (double amp : { 0.08, 0.5 })
                    {
                        // three hard-gated chord bursts (0.3 s on, 0.3 s off): abrupt onsets and cut-offs
                        std::vector<float> in ((size_t) (1.8 * sr));
                        for (size_t i = 0; i < in.size(); ++i)
                        {
                            const double t = (double) i / sr;
                            const bool on = std::fmod (t, 0.6) < 0.3;
                            double v = 0.0;
                            for (double f0 : { 110.0, 164.81, 220.0 })
                                for (int k = 1; k <= 6; ++k) v += std::sin (6.283185307 * f0 * k * t + k) / k;
                            in[i] = on ? (float) (amp * v / 3.0) : 0.0f;
                        }
                        std::vector<float> out[2];
                        for (int engine = 0; engine < 2; ++engine)
                        {
                            BigMuffStyleFuzzProcessor pedal (m.model, engine == 1);
                            setParams (pedal, { { m.prefix + "_sustain", sus }, { m.prefix + "_tone", 0.5f }, { m.prefix + "_volume", 0.7f } });
                            pedal.prepare (sr, 256, 1);
                            juce::AudioBuffer<float> buf (1, 256);
                            for (size_t pos = 0; pos < in.size(); pos += 256)
                            {
                                const int n = (int) std::min<size_t> (256, in.size() - pos);
                                for (int i = 0; i < n; ++i) buf.setSample (0, i, in[pos + (size_t) i]);
                                pedal.process (buf);
                                for (int i = 0; i < n; ++i) out[engine].push_back (buf.getSample (0, i));
                            }
                        }
                        auto peakDb = [] (const std::vector<float>& x) { float p = 1e-9f; for (auto v : x) p = std::max (p, std::abs (v)); return 20.0 * std::log10 (p); };
                        const int frame = (int) (0.005 * sr);
                        auto envelope = [frame] (const std::vector<float>& x) {
                            std::vector<double> e;
                            for (size_t s0 = 0; s0 + (size_t) frame <= x.size(); s0 += (size_t) frame)
                            {
                                double acc = 0.0;
                                for (int i = 0; i < frame; ++i) acc += (double) x[s0 + (size_t) i] * x[s0 + (size_t) i];
                                e.push_back (10.0 * std::log10 (acc / frame + 1e-20));
                            }
                            return e;
                        };
                        const auto ea = envelope (out[0]), eb = envelope (out[1]);
                        const double top = *std::max_element (ea.begin(), ea.end());
                        double envDiff = 0.0;
                        size_t worstFrame = 0;
                        for (size_t k = 0; k < ea.size(); ++k)
                            if (ea[k] > top - 40.0 && std::abs (eb[k] - ea[k]) > envDiff) { envDiff = std::abs (eb[k] - ea[k]); worstFrame = k; }
                        logMessage ("    worst frame at " + juce::String ((double) worstFrame * 0.005, 3) + " s, full " + juce::String (ea[worstFrame] - top, 1)
                                    + " dB re top, pipeline " + juce::String (eb[worstFrame] - top, 1));
                        auto onset = [top] (const std::vector<double>& e) { for (size_t k = 0; k < e.size(); ++k) if (e[k] > top - 10.0) return (int) k; return -1; };
                        const double peakDiff = std::abs (peakDb (out[1]) - peakDb (out[0]));
                        const int onsetDiff = std::abs (onset (eb) - onset (ea));
                        logMessage ("  " + juce::String (m.tag) + " sus " + juce::String (sus) + " amp " + juce::String (amp)
                                    + ": peak diff " + juce::String (peakDiff, 2) + " dB, envelope diff " + juce::String (envDiff, 2)
                                    + " dB, onset diff " + juce::String (onsetDiff) + " frames");
                        worstPeak = std::max (worstPeak, peakDiff);
                        worstEnv = std::max (worstEnv, envDiff);
                        worstOnset = std::max (worstOnset, onsetDiff);
                    }
            logMessage ("  transients: worst peak " + juce::String (worstPeak, 2) + " dB, worst 5 ms envelope " + juce::String (worstEnv, 2)
                        + " dB, worst onset " + juce::String (worstOnset) + " frames");
            expect (worstPeak < 0.6, "peaks within 0.6 dB (the HM-2 pop was +1 dB on hard edges)");
            expect (worstEnv < 1.5, "every 5 ms frame within 1.5 dB through attack, sustain and cut-off");
            expect (worstOnset <= 1, "onsets land in the same 5 ms frame");
        }

        beginTest ("Big Muff pipeline: random knob moves with 3 V bursts stay finite; silence decays to silence");
        for (const auto& m : models)
        {
            BigMuffStyleFuzzProcessor pedal (m.model, true);
            pedal.prepare (sr, 256, 2);
            juce::Random rng (11);
            juce::AudioBuffer<float> buf (2, 256);
            float peak = 0.0f;
            bool finite = true;
            for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
            {
                if (blockIndex % 10 == 0)
                    setParams (pedal, { { m.prefix + "_sustain", rng.nextFloat() }, { m.prefix + "_tone", rng.nextFloat() }, { m.prefix + "_volume", rng.nextFloat() } });
                const float amp = (blockIndex % 37) < 3 ? 3.0f : 0.2f;
                for (int i = 0; i < 256; ++i)
                {
                    const float x = amp * std::sin (0.07f * (float) (blockIndex * 256 + i)) + 0.01f * (rng.nextFloat() - 0.5f);
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, blockIndex > 200 ? -x : x); // diverge the channels half way (dual-mono shortcut exit)
                }
                pedal.process (buf);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                    {
                        const float y = buf.getSample (c, i);
                        finite = finite && std::isfinite (y);
                        peak = std::max (peak, std::abs (y));
                    }
            }
            expect (finite, juce::String (m.tag) + ": non-finite output");
            expect (peak < 20.0f, juce::String (m.tag) + ": peak " + juce::String (peak));

            float tail = 0.0f;
            for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
            {
                buf.clear();
                pedal.process (buf);
                if (blockIndex > 300)
                    for (int i = 0; i < 256; ++i) tail = std::max (tail, std::abs (buf.getSample (0, i)));
            }
            logMessage ("  " + juce::String (m.tag) + ": hot peak " + juce::String (peak, 2) + ", silent tail after 1.6 s " + juce::String (tail, 8));
            expect (tail < 1.0e-4f, juce::String (m.tag) + ": the circuit should settle to silence, got " + juce::String (tail));
        }
    }
};

static PipelineEquivalenceTests pipelineEquivalenceTests;

/** Every pedal converted with the netlist compiler (pipeline::CompiledCircuit), against its own Newton netlist. */
class CompiledPipelineTests : public juce::UnitTest
{
public:
    CompiledPipelineTests() : juce::UnitTest ("CompiledPipeline", "Effects") {}

    void expectAccepted (const pipelinetest::Verdict& v, double envelopeBound = 1.5)
    {
        expect (v.finite, "finite and bounded");
        expect (v.level < 2.0, "level within 2 dB, got " + juce::String (v.level));
        expect (v.band < 1.2, "timbre within 1.2 dB, got " + juce::String (v.band));
        expect (v.peak < 0.6, "peaks within 0.6 dB, got " + juce::String (v.peak));
        expect (v.envelope < envelopeBound, "5 ms envelope within " + juce::String (envelopeBound) + " dB, got " + juce::String (v.envelope));
        expect (v.onset <= 1, "onsets in the same 5 ms frame");
        expect (v.tail < 1.0e-3, "settles to silence, got " + juce::String (v.tail));
        expect (v.speedup > 1.0, "cheaper than the netlist, got x" + juce::String (v.speedup));
    }

    void runTest() override
    {
        auto log = [this] (const juce::String& s) { logMessage (s); };

        beginTest ("Bass Big Muff: compiled pipeline vs netlist");
        expectAccepted (pipelinetest::compare (
            [] (bool pipe) { auto p = std::make_unique<BassBigMuffStyleFuzzProcessor>(); p->setPipelineEngine (pipe); return std::unique_ptr<EffectProcessor> (std::move (p)); },
            { { { "bbmp_sustain", 0.3f }, { "bbmp_tone", 0.2f }, { "bbmp_volume", 0.7f }, { "bbmp_bass_boost", 0.0f }, { "bbmp_dry", 0.0f } },
              { { "bbmp_sustain", 0.8f }, { "bbmp_tone", 0.8f }, { "bbmp_volume", 0.7f }, { "bbmp_bass_boost", 1.0f }, { "bbmp_dry", 0.0f } },
              { { "bbmp_sustain", 0.6f }, { "bbmp_tone", 0.5f }, { "bbmp_volume", 0.7f }, { "bbmp_bass_boost", 0.0f }, { "bbmp_dry", 1.0f } } },
            log));

        beginTest ("Metal Zone: compiled pipeline vs the full netlist");
        expectAccepted (pipelinetest::compare (
            [] (bool pipe) {
                MetalZoneStyleDistortionProcessor::reducedOrder = false; // the reference is the full netlist
                auto p = std::make_unique<MetalZoneStyleDistortionProcessor>();
                p->setPipelineEngine (pipe);
                return std::unique_ptr<EffectProcessor> (std::move (p));
            },
            { { { "mt2_dist", 0.2f }, { "mt2_high", 0.5f }, { "mt2_middle", 0.5f }, { "mt2_midfreq", 0.5f }, { "mt2_low", 0.5f }, { "mt2_level", 0.5f } },
              { { "mt2_dist", 0.9f }, { "mt2_high", 0.8f }, { "mt2_middle", 0.2f }, { "mt2_midfreq", 0.7f }, { "mt2_low", 0.7f }, { "mt2_level", 0.5f } },
              { { "mt2_dist", 0.6f }, { "mt2_high", 0.2f }, { "mt2_middle", 0.9f }, { "mt2_midfreq", 0.3f }, { "mt2_low", 0.3f }, { "mt2_level", 0.5f } } },
            log), 1.7); // the recovery after a rail-pinned Dist stage: 1.6 dB here vs 9.0 dB for the shipped reducedOrder
                        // (op-amp macro's integrator clamps become the eco bleed: docs/PerformancePipeline.md)
        MetalZoneStyleDistortionProcessor::reducedOrder = true; // restore the shipped default

        beginTest ("DT-1: compiled pipeline vs netlist");
        expectAccepted (pipelinetest::compare (
            [] (bool pipe) { auto p = std::make_unique<DT1StyleDistortionProcessor>(); p->setPipelineEngine (pipe); return std::unique_ptr<EffectProcessor> (std::move (p)); },
            { { { "dt1_distortion", 0.2f }, { "dt1_tone", 0.5f }, { "dt1_level", 0.5f } },
              { { "dt1_distortion", 0.9f }, { "dt1_tone", 0.8f }, { "dt1_level", 0.5f } },
              { { "dt1_distortion", 0.6f }, { "dt1_tone", 0.2f }, { "dt1_level", 0.5f } } },
            log));

        beginTest ("HM-2: compiled pipeline vs netlist (measured only: HM-2 stays on the netlist, see its header)");
        (void) (pipelinetest::compare (
            [] (bool pipe) { auto p = std::make_unique<HM2StyleDistortionProcessor>(); p->setPipelineEngine (pipe); return std::unique_ptr<EffectProcessor> (std::move (p)); },
            { { { "hm2_dist", 0.2f }, { "hm2_low", 0.5f }, { "hm2_high", 0.5f }, { "hm2_level", 0.5f } },
              { { "hm2_dist", 1.0f }, { "hm2_low", 1.0f }, { "hm2_high", 1.0f }, { "hm2_level", 0.5f } },
              { { "hm2_dist", 0.6f }, { "hm2_low", 0.2f }, { "hm2_high", 0.8f }, { "hm2_level", 0.5f } } },
            log));
    }
};

static CompiledPipelineTests compiledPipelineTests;

} // namespace openguitarmultifx
