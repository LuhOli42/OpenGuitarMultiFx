#include "Effects/GuitarSynth/GuitarToBassProcessor.h"
#include "Effects/GuitarSynth/SpectralMorphProcessor.h"
#include "Effects/GuitarSynth/SubOctaveSynthProcessor.h"
#include "Effects/GuitarSynth/TrackedSynthProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

class GuitarSynthTests : public juce::UnitTest
{
public:
    GuitarSynthTests() : juce::UnitTest ("GuitarSynth", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnob (EffectProcessor& p, const char* id, float value)
    {
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && f->paramID == id)
                *f = value;
    }

    /** A plucked-string-like tone: harmonics 1..6 with 1/n weights and an exponential decay. */
    static std::vector<float> pluck (double hz, double seconds, double amp = 0.3, bool decay = true)
    {
        std::vector<float> x ((size_t) (seconds * sr));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / sr;
            double v = 0.0;
            for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * 3.141592653589793 * hz * h * t + 0.3 * h) / h;
            x[i] = (float) (amp * v * (decay ? std::exp (-1.2 * t) : 1.0));
        }
        return x;
    }

    static std::vector<float> render (EffectProcessor& p, const std::vector<float>& in)
    {
        p.prepare (sr, 256, 1);
        std::vector<float> out (in.size());
        for (size_t pos = 0; pos < in.size(); pos += 256)
        {
            const int n = (int) std::min<size_t> (256, in.size() - pos);
            juce::AudioBuffer<float> b (1, n);
            for (int i = 0; i < n; ++i) b.setSample (0, i, in[pos + (size_t) i]);
            p.process (b);
            for (int i = 0; i < n; ++i) out[pos + (size_t) i] = b.getSample (0, i);
        }
        return out;
    }

    /** Fundamental of the rendered tail, measured with the same tracker the effects use. */
    static double trackedHz (const std::vector<float>& x, double fromSeconds)
    {
        gs::PitchTracker t;
        t.prepare (sr);
        double hz = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            t.push (x[i]);
            if (i >= (size_t) (fromSeconds * sr) && t.voiced) hz = t.hz;
        }
        return hz;
    }

    static double goertzel (const std::vector<float>& x, double hz, size_t from)
    {
        double re = 0, im = 0;
        for (size_t i = from; i < x.size(); ++i)
        {
            const double a = 2.0 * 3.141592653589793 * hz * (double) i / sr;
            re += x[i] * std::cos (a);
            im += x[i] * std::sin (a);
        }
        return std::sqrt (re * re + im * im) / (double) (x.size() - from);
    }

    /** Strongest spectral line in [lo, hi] Hz over the tail of the render (1 Hz resolution, 0.5 s window). */
    static double spectralPeakHz (const std::vector<float>& x, double lo, double hi, size_t from)
    {
        double best = 0.0, bestHz = 0.0;
        for (double f = lo; f <= hi; f += 1.0)
        {
            const double m = goertzel (x, f, from);
            if (m > best) { best = m; bestHz = f; }
        }
        return bestHz;
    }

    void runTest() override
    {
        beginTest ("pitch tracker follows plucked notes from low E to high A within 1.5%");
        for (double hz : { 82.41, 110.0, 196.0, 329.63, 440.0 })
        {
            const auto x = pluck (hz, 0.6);
            const double got = trackedHz (x, 0.3);
            logMessage ("  tracker " + juce::String (hz, 2) + " Hz -> " + juce::String (got, 2));
            expect (std::abs (got - hz) < hz * 0.015, "tracker off at " + juce::String (hz));
        }

        beginTest ("pitch tracker settles within three periods of a note onset");
        {
            const double hz = 110.0;
            auto x = pluck (hz, 0.4, 0.3, false);
            gs::PitchTracker t;
            t.prepare (sr);
            int firstGood = -1;
            for (size_t i = 0; i < x.size(); ++i)
            {
                t.push (x[i]);
                if (firstGood < 0 && t.voiced && std::abs (t.hz - hz) < hz * 0.02) firstGood = (int) i;
            }
            const double periods = firstGood < 0 ? 99.0 : firstGood / (sr / hz);
            logMessage ("  settled after " + juce::String (periods, 2) + " periods");
            expect (periods < 4.0);
        }

        beginTest ("Guitar-to-Bass puts the note one and two octaves down");
        for (int oct = 0; oct < 2; ++oct)
        {
            GuitarToBassProcessor p;
            setKnob (p, "octaves", (float) oct);
            setKnob (p, "guitar", 0.0f);
            setKnob (p, "grit", 0.0f);
            const double hz = 220.0;
            const auto out = render (p, pluck (hz, 1.2, 0.3, false));
            const double want = hz / (oct == 0 ? 2.0 : 4.0);
            const double got = spectralPeakHz (out, want * 0.6, want * 1.6, out.size() - (size_t) (0.5 * sr));
            logMessage ("  tracker view: " + juce::String (trackedHz (out, 0.7), 2) + " Hz");
            logMessage ("  bass octave " + juce::String (oct) + ": " + juce::String (got, 2) + " Hz (want " + juce::String (want, 2) + ")");
            expect (std::abs (got - want) < want * 0.04);
            float peak = 0; for (auto s : out) peak = std::max (peak, std::abs (s));
            expect (peak > 0.02f && peak < 2.0f, "level out of range: " + juce::String (peak));
        }

        beginTest ("Sub-Octave Synth divides the note by two");
        {
            SubOctaveSynthProcessor p;
            setKnob (p, "sub1", 100.0f); setKnob (p, "sub2", 0.0f); setKnob (p, "dry", 0.0f);
            setKnob (p, "cutoff", 2000.0f); setKnob (p, "sweep", 0.0f);
            const auto out = render (p, pluck (196.0, 1.0, 0.3, false));
            const double got = trackedHz (out, 0.5);
            logMessage ("  sub-octave: " + juce::String (got, 2) + " Hz (want 98)");
            expect (std::abs (got - 98.0) < 98.0 * 0.04);
        }

        beginTest ("Tracked Synth plays the tracked note, octave shift works, silence stays silent");
        for (int oct : { 0, -1 })
        {
            TrackedSynthProcessor p;
            setKnob (p, "octave", (float) oct); setKnob (p, "detune", 0.0f); setKnob (p, "sub", 0.0f);
            setKnob (p, "cutoff", 6000.0f); setKnob (p, "glide", 0.0f); setKnob (p, "dry", 0.0f);
            const double hz = 196.0;
            const auto out = render (p, pluck (hz, 1.0, 0.3, false));
            const double got = trackedHz (out, 0.6);
            const double want = hz * std::pow (2.0, oct);
            logMessage ("  synth octave " + juce::String (oct) + ": " + juce::String (got, 2) + " Hz (want " + juce::String (want, 2) + ")");
            expect (std::abs (got - want) < want * 0.03);
        }
        {
            TrackedSynthProcessor p;
            const auto out = render (p, std::vector<float> ((size_t) sr / 2, 0.0f));
            float peak = 0; for (auto s : out) peak = std::max (peak, std::abs (s));
            expect (peak < 1e-4f, "silence in should be silence out");
        }

        beginTest ("Spectral Morph: EE puts more energy at 2.3 kHz than OO, relative to 870 Hz");
        {
            juce::Random rng (7);
            std::vector<float> noise ((size_t) sr);
            for (auto& v : noise) v = 0.2f * (rng.nextFloat() * 2.0f - 1.0f);
            double ratio[2] {};
            for (int s = 0; s < 2; ++s)
            {
                SpectralMorphProcessor p;
                setKnob (p, "shapeA", s == 0 ? 1.0f : 2.0f); // CHOIR EE / CHOIR OO
                setKnob (p, "flatten", 0.0f); setKnob (p, "dynamics", 0.0f);
                const auto out = render (p, noise);
                ratio[s] = goertzel (out, 2290.0, 24000) / (goertzel (out, 870.0, 24000) + 1e-12);
            }
            logMessage ("  2.3k/870 ratio: EE " + juce::String (ratio[0], 3) + " vs OO " + juce::String (ratio[1], 3));
            expect (ratio[0] > ratio[1] * 2.0);
        }

        beginTest ("every block stays finite on loud input");
        {
            std::vector<float> loud = pluck (110.0, 0.5, 1.0, false);
            for (auto& v : loud) v = juce::jlimit (-1.0f, 1.0f, v * 4.0f);
            GuitarToBassProcessor a; SubOctaveSynthProcessor b; TrackedSynthProcessor c; SpectralMorphProcessor d;
            for (EffectProcessor* p : { (EffectProcessor*) &a, (EffectProcessor*) &b, (EffectProcessor*) &c, (EffectProcessor*) &d })
            {
                const auto out = render (*p, loud);
                bool finite = true; float peak = 0;
                for (auto s : out) { finite = finite && std::isfinite (s); peak = std::max (peak, std::abs (s)); }
                expect (finite, juce::String (p->getName()) + " produced a non-finite sample");
                expect (peak < 8.0f, juce::String (p->getName()) + " peak " + juce::String (peak));
            }
        }
    }
};

static GuitarSynthTests guitarSynthTests;

} // namespace openguitarmultifx
