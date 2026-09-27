#include "EffectRegistry.h"
#include "Effects/IRLoaderProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/**
    What a "Mix" knob has to be, on every effect that has one: a crossfade between the dry signal and a wet signal that is as loud as
    the dry one. So Mix = 0 must be the input at unity (not louder, not quieter), Mix = 1 must be about as loud as the input
    (a wet path 8 dB louder or 20 dB quieter makes noon sound like 90% or 5% wet), and therefore Mix = 0.5 is a real 50 / 50.
    The measurement is energy over a run of plucked notes (a reverb tail counts: it is part of what is heard).
*/
class MixLawTests : public juce::UnitTest
{
public:
    MixLawTests() : juce::UnitTest ("MixLaw", "Effects") {}

    static constexpr double sr = 48000.0;

    static std::vector<float> pluckedNotes (double seconds)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        std::vector<float> x ((size_t) (seconds * sr), 0.0f);
        const double notes[] = { 82.41, 110.0, 146.83, 196.0, 164.81, 123.47 };
        for (size_t n = 0; n < x.size(); ++n)
        {
            const double t = (double) n / sr;
            const int index = (int) (t / 0.6);
            const double local = t - 0.6 * index, f0 = notes[index % 6];
            double v = 0.0;
            for (int k = 1; k <= 6; ++k)
                v += std::sin (twoPi * f0 * k * local) / (double) k;
            x[n] = (float) (0.2 * v * std::exp (-local / 0.35));
        }
        return x;
    }

    /** Energy gain (dB) of the effect at the given Mix over the second half of the run, and the same for a second knob position. */
    static double energyGainDb (EffectProcessor& p, juce::AudioParameterFloat& mix, float mixValue, const std::vector<float>& in)
    {
        mix = mixValue;
        p.prepare (sr, 512, 2);
        p.reset();
        juce::AudioBuffer<float> buf (2, 512);
        double eIn = 0.0, eOut = 0.0;
        for (size_t base = 0; base + 512 <= in.size(); base += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample (0, i, in[base + (size_t) i]);
                buf.setSample (1, i, in[base + (size_t) i]);
            }
            p.process (buf);
            if (base >= in.size() / 2)
                for (int i = 0; i < 512; ++i)
                {
                    eIn += (double) in[base + (size_t) i] * in[base + (size_t) i];
                    eOut += (double) buf.getSample (0, i) * buf.getSample (0, i);
                }
        }
        return 10.0 * std::log10 (juce::jmax (1.0e-20, eOut) / juce::jmax (1.0e-20, eIn));
    }

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto in = pluckedNotes (40.0); // long: the wet-level matcher averages over ~10 s

        if (std::getenv ("MIX_TRACE") != nullptr)
        {
            // Dev only: the wet path's energy gain (Mix 1) in 4 s windows over the run of one effect (MIX_TRACE=<registry key>).
            beginTest ("mix trace (dev only)");
            auto fx = registry.create (std::getenv ("MIX_TRACE"));
            juce::AudioParameterFloat* mix = nullptr;
            for (auto* prm : fx->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                    if (f->getName (32) == "Mix")
                        mix = f;
            *mix = 1.0f;
            fx->prepare (sr, 512, 2);
            juce::AudioBuffer<float> buf (2, 512);
            double eIn = 0.0, eOut = 0.0;
            int window = 0;
            for (size_t base = 0; base + 512 <= in.size(); base += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    buf.setSample (0, i, in[base + (size_t) i]);
                    buf.setSample (1, i, in[base + (size_t) i]);
                }
                fx->process (buf);
                for (int i = 0; i < 512; ++i)
                {
                    eIn += (double) in[base + (size_t) i] * in[base + (size_t) i];
                    eOut += (double) buf.getSample (0, i) * buf.getSample (0, i);
                }
                if ((int) ((base + 512) / (size_t) (4 * sr)) != window)
                {
                    window = (int) ((base + 512) / (size_t) (4 * sr));
                    logMessage ("t=" + juce::String (window * 4) + " s: " + juce::String (10.0 * std::log10 (eOut / eIn), 2) + " dB");
                    eIn = eOut = 0.0;
                }
            }
        }

        if (std::getenv ("MIX_SWEEP") != nullptr)
        {
            // Dev only: the wet path's energy gain (Mix 1) over a grid of every other knob at 0 / 0.5 / 1, for one effect
            // (MIX_SWEEP=<registry key>), to calibrate a reverb's wet level against its own parameters.
            beginTest ("mix sweep (dev only)");
            const juce::String key (std::getenv ("MIX_SWEEP"));
            auto fx = registry.create (key);
            std::vector<juce::AudioParameterFloat*> knobs;
            juce::AudioParameterFloat* mix = nullptr;
            for (auto* prm : fx->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                {
                    if (f->getName (32) == "Mix")
                        mix = f;
                    else
                        knobs.push_back (f);
                }
            const int n = (int) knobs.size();
            int combos = 1;
            for (int i = 0; i < n; ++i)
                combos *= 3;
            for (int c = 0; c < combos; ++c)
            {
                juce::String label;
                int r = c;
                for (auto* k : knobs)
                {
                    const float v = (float) (r % 3) * 0.5f;
                    r /= 3;
                    *k = k->range.convertFrom0to1 (v);
                    label += k->getName (12) + " " + juce::String (v, 1) + "  ";
                }
                logMessage (label + ": Mix 1 = " + juce::String (energyGainDb (*fx, *mix, 1.0f, in), 2) + " dB");
            }
        }

        // Every algorithmic reverb, across the grid of its other knobs at 0 / 0.5 / 1: the wet path is as loud as the dry. (The
        // spread that remains is the signal: a plucked note through a comb bank is not white noise.)
        beginTest ("reverbs: Mix 1 is as loud as the dry signal at every Decay / Tone / Size setting");
        for (const char* key : { "Ambient", "Hall", "Plate", "Room", "Spring", "Shimmer", "GatedReverb" })
        {
            auto fx = registry.create (key);
            if (fx == nullptr)
                continue;
            std::vector<juce::AudioParameterFloat*> knobs;
            juce::AudioParameterFloat* mix = nullptr;
            for (auto* prm : fx->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                {
                    if (f->getName (32) == "Mix")
                        mix = f;
                    else
                        knobs.push_back (f);
                }
            expect (mix != nullptr);
            if (mix == nullptr)
                continue;
            int combos = 1;
            for (size_t i = 0; i < knobs.size(); ++i)
                combos *= 3;
            double worst = 0.0;
            for (int c = 0; c < combos; ++c)
            {
                int r = c;
                for (auto* k : knobs)
                {
                    *k = k->range.convertFrom0to1 ((float) (r % 3) * 0.5f);
                    r /= 3;
                }
                worst = juce::jmax (worst, std::abs (energyGainDb (*fx, *mix, 1.0f, in)));
            }
            logMessage (juce::String (key).paddedRight (' ', 14) + "worst |Mix 1 - dry| over " + juce::String (combos) + " knob settings: " + juce::String (worst, 2) + " dB");
            expectLessThan (worst, 2.5);
        }

        beginTest ("every effect with a Mix knob: Mix 0 is the input at unity, Mix 1 is about as loud as the input");
        for (const auto& key : registry.getRegisteredNames())
        {
            auto fx = registry.create (key);
            if (fx == nullptr || fx->getParameters() == nullptr)
                continue;
            juce::AudioParameterFloat* mix = nullptr;
            for (auto* prm : fx->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                    if (f->getName (32) == "Mix")
                        mix = f;
            if (mix == nullptr)
                continue;

            const double g0 = energyGainDb (*fx, *mix, 0.0f, in);
            const double g1 = energyGainDb (*fx, *mix, 1.0f, in);
            const double g5 = energyGainDb (*fx, *mix, 0.5f, in);
            logMessage (key.paddedRight (' ', 26) + "Mix 0: " + juce::String (g0, 2).paddedLeft (' ', 7) + " dB   Mix 1: "
                        + juce::String (g1, 2).paddedLeft (' ', 7) + " dB   Mix 0.5: " + juce::String (g5, 2).paddedLeft (' ', 7) + " dB");
            // the UniVibe's own throb gain sits at -0.5 dB whatever the Mix
            expectWithinAbsoluteError (g0, 0.0, key == "UniVibe" ? 0.7 : 0.5);
            // modulation effects lose or gain a few dB by their nature (a rotary's amplitude modulation, a phaser's resonance)
            expectWithinAbsoluteError (g1, 0.0, 4.0);
        }

        beginTest ("the IR Reverb: an IR loaded, Mix 0 is the input and Mix 1 is as loud as the input (JUCE normalises an IR to -18 dB)");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ogmfx_mixlaw_ir.wav");
            file.deleteFile();
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
                if (writer != nullptr)
                {
                    stream.release();
                    juce::Random rng (99);
                    const int n = (int) (1.5 * sr);
                    juce::AudioBuffer<float> ir (2, n);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < n; ++i)
                            ir.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / (0.25f * (float) sr)));
                    writer->writeFromAudioSampleBuffer (ir, 0, n);
                }
            }
            auto fx = registry.create ("Reverb");
            auto* loader = dynamic_cast<IRLoaderProcessor*> (fx.get());
            expect (loader != nullptr);
            if (loader != nullptr)
            {
                fx->prepare (sr, 512, 2);
                loader->loadImpulseResponse (file);
                juce::AudioBuffer<float> silence (2, 512);
                for (int i = 0; i < 400; ++i) // the convolution swaps its IR in on a background thread
                {
                    silence.clear();
                    fx->process (silence);
                    juce::Thread::sleep (2);
                }
                juce::AudioParameterFloat* mix = nullptr;
                for (auto* prm : fx->getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                        if (f->getName (32) == "Mix")
                            mix = f;
                expect (mix != nullptr);
                if (mix != nullptr)
                {
                    // energyGainDb() prepares (which reloads the IR) -- run the blocks here instead
                    auto run = [&] (float m)
                    {
                        *mix = m;
                        double eIn = 0.0, eOut = 0.0;
                        juce::AudioBuffer<float> buf (2, 512);
                        for (size_t base = 0; base + 512 <= in.size(); base += 512)
                        {
                            for (int i = 0; i < 512; ++i)
                            {
                                buf.setSample (0, i, in[base + (size_t) i]);
                                buf.setSample (1, i, in[base + (size_t) i]);
                            }
                            fx->process (buf);
                            if (base >= in.size() / 2)
                                for (int i = 0; i < 512; ++i)
                                {
                                    eIn += (double) in[base + (size_t) i] * in[base + (size_t) i];
                                    eOut += (double) buf.getSample (0, i) * buf.getSample (0, i);
                                }
                        }
                        return 10.0 * std::log10 (juce::jmax (1.0e-20, eOut) / juce::jmax (1.0e-20, eIn));
                    };
                    const double g1 = run (1.0f), g0 = run (0.0f), g5 = run (0.5f);
                    logMessage ("IR Reverb  Mix 1: " + juce::String (g1, 2) + " dB   Mix 0: " + juce::String (g0, 2) + " dB   Mix 0.5: " + juce::String (g5, 2) + " dB");
                    expectWithinAbsoluteError (g1, 0.0, 2.5);
                    expectWithinAbsoluteError (g0, 0.0, 0.5);
                }
            }
            file.deleteFile();
        }
    }
};

static MixLawTests mixLawTests;

} // namespace openguitarmultifx
