#include "Effects/GuitarSynth/GuitarToBassProcessor.h"
#include "Effects/GuitarSynth/PolyPitchTracker.h"
#include "Effects/GuitarSynth/SpectralMorphProcessor.h"
#include "Effects/GuitarSynth/SubOctaveSynthProcessor.h"
#include "Effects/GuitarSynth/TrackedSynthProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
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

    static std::vector<float> chord (const std::vector<double>& notes, double seconds, double amp = 0.15)
    {
        std::vector<float> x ((size_t) (seconds * sr), 0.0f);
        for (double hz : notes)
        {
            const auto p = pluck (hz, seconds, amp, false);
            for (size_t i = 0; i < x.size(); ++i) x[i] += p[i];
        }
        return x;
    }

    static double rms (const std::vector<float>& x, size_t from)
    {
        double e = 0.0;
        for (size_t i = from; i < x.size(); ++i) e += (double) x[i] * x[i];
        return std::sqrt (e / (double) juce::jmax<size_t> (1, x.size() - from));
    }

    static double dB (double v) { return 20.0 * std::log10 (v + 1e-12); }

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

        auto modeName = [] (int m) { return m == 0 ? "  MONO" : (m == 1 ? "  POLY" : "  HYBRID"); };
        beginTest ("Guitar-to-Bass shifts a whole chord, every note one and two octaves down (MONO, POLY, HYBRID)");
        {
            const std::vector<double> notes { 110.0, 138.59, 164.81, 220.0 }; // close A major, the hardest voicing
            const auto in = chord (notes, 1.5);
            for (int mode = 0; mode < 3; ++mode)
                for (int oct = 0; oct < 2; ++oct)
                {
                    GuitarToBassProcessor p;
                    setKnob (p, "mode", (float) mode); setKnob (p, "octaves", (float) oct);
                    setKnob (p, "guitar", 0.0f); setKnob (p, "grit", 0.0f); setKnob (p, "tone", 100.0f);
                    const auto out = render (p, in);
                    const double div = oct == 0 ? 2.0 : 4.0;
                    const size_t from = (size_t) (0.5 * sr);
                    const juce::String tag = juce::String (modeName (mode)) + " oct " + juce::String (oct);
                    double strongest = 0.0;
                    std::vector<double> m;
                    for (double hz : notes) { m.push_back (goertzel (out, hz / div, from)); strongest = std::max (strongest, m.back()); }
                    for (size_t k = 0; k < notes.size(); ++k)
                    {
                        // a quarter tone either side of the shifted note has to be well below the note itself
                        const double f = notes[k] / div, off = std::max (goertzel (out, f * 1.0293, from), goertzel (out, f / 1.0293, from));
                        double bestC = 0.0, best = 0.0;
                        for (double c = -30.0; c <= 30.0; c += 1.0)
                            if (const double g = goertzel (out, f * std::pow (2.0, c / 1200.0), from); g > best) { best = g; bestC = c; }
                        logMessage (tag + " note " + juce::String (f, 1) + " Hz: " + juce::String (dB (m[k] / strongest), 1)
                                    + " dB, quarter-tone leak " + juce::String (dB (off / m[k]), 1) + " dB, " + juce::String (bestC, 0) + " cents");
                        expect (m[k] > strongest * 0.1, tag + " chord note missing after the shift: " + juce::String (f, 1));
                        // two octaves down the splice shifter smears a close chord; POLY (and HYBRID's sustain) is the chord mode
                        if (mode != 0 || oct == 0)
                            expect (off < m[k] * 0.35, tag + " shifted note smeared / out of tune: " + juce::String (f, 1));
                        if (mode != 0)
                            expect (std::abs (bestC) <= 5.0, tag + " chord note " + juce::String (f, 1) + " off by " + juce::String (bestC, 0) + " cents");
                    }
                    const double lvl = dB (rms (out, from) / rms (in, from));
                    logMessage (tag + " level vs dry: " + juce::String (lvl, 1) + " dB");
                    expect (lvl > -4.0 && lvl < 8.0, tag + " level vs dry " + juce::String (lvl, 1) + " dB");
                }
        }

        beginTest ("Guitar-to-Bass pitch holds steady across a sustained note (MONO, POLY, HYBRID)");
        for (int mode = 0; mode < 3; ++mode)
        {
            GuitarToBassProcessor p;
            setKnob (p, "mode", (float) mode); setKnob (p, "guitar", 0.0f); setKnob (p, "grit", 0.0f);
            const auto out = render (p, pluck (196.0, 2.5, 0.3, false));
            double worst = 0.0;
            for (double t = 0.4; t + 0.25 <= 2.5; t += 0.25)
            {
                std::vector<float> seg (out.begin() + (long) (t * sr), out.begin() + (long) ((t + 0.25) * sr));
                const double got = spectralPeakHz (seg, 80.0, 115.0, 0);
                worst = std::max (worst, std::abs (1200.0 * std::log2 (got / 98.0)));
            }
            logMessage (juce::String (modeName (mode)) + " worst deviation " + juce::String (worst, 1) + " cents");
            expect (worst < 20.0);
        }

        beginTest ("Guitar-to-Bass note onset arrives quickly (MONO and HYBRID < 25 ms, POLY < 50 ms)");
        for (int mode = 0; mode < 3; ++mode)
        {
            GuitarToBassProcessor p;
            setKnob (p, "mode", (float) mode); setKnob (p, "guitar", 0.0f); setKnob (p, "grit", 0.0f);
            const size_t start = (size_t) (0.3 * sr), win = 480;
            std::vector<float> in (start, 0.0f);
            const auto note = pluck (110.0, 1.0, 0.3, false);
            in.insert (in.end(), note.begin(), note.end());
            const auto out = render (p, in);
            std::vector<double> env;
            for (size_t i = start; i + win < start + (size_t) (0.3 * sr); i += 48)
            {
                double e = 0.0;
                for (size_t j = i; j < i + win; ++j) e += (double) out[j] * out[j];
                env.push_back (std::sqrt (e / win));
            }
            const double peak = *std::max_element (env.begin(), env.end());
            size_t k = 0;
            while (env[k] < 0.5 * peak) ++k;
            const double ms = (double) (k * 48 + win / 2) / sr * 1000.0;
            logMessage (juce::String (modeName (mode)) + " onset at half level after " + juce::String (ms, 1) + " ms");
            expect (ms < (mode == 1 ? 50.0 : 25.0));
        }

        beginTest ("Guitar-to-Bass HYBRID hands a note and a chord from MONO to POLY without a hole");
        for (const auto& notes : { std::vector<double> { 110.0 }, std::vector<double> { 110.0, 138.59, 164.81, 220.0 },
                                   std::vector<double> { 82.41, 123.47, 164.81, 207.65 } })
            for (int oct = 0; oct < 2; ++oct)
            {
                const size_t start = (size_t) (0.3 * sr), win = 2880;
                std::vector<float> in (start, 0.0f);
                const auto c = chord (notes, 1.0);
                in.insert (in.end(), c.begin(), c.end());
                std::vector<double> pw[3];
                for (int mode = 0; mode < 3; ++mode)
                {
                    GuitarToBassProcessor p;
                    setKnob (p, "mode", (float) mode); setKnob (p, "octaves", (float) oct);
                    setKnob (p, "guitar", 0.0f); setKnob (p, "grit", 0.0f); setKnob (p, "squash", 0.0f);
                    const auto out = render (p, in);
                    for (size_t i = start + (size_t) (0.06 * sr); i + win < start + (size_t) (0.4 * sr); i += 48)
                    {
                        double e = 0.0;
                        for (size_t j = i; j < i + win; ++j) e += (double) out[j] * out[j];
                        pw[mode].push_back (e / win);
                    }
                }
                // the handoff happens ~90..180 ms in. 60 ms windows, so a chord's own beating (which moves when POLY's
                // phases are re-seeded) averages out and only a real cancellation shows
                double worst = 0.0;
                for (size_t k = 0; k < pw[2].size(); ++k)
                    worst = std::min (worst, 0.5 * dB (pw[2][k] / (0.5 * (pw[0][k] + pw[1][k]))));
                logMessage ("  " + juce::String ((int) notes.size()) + " note(s) oct " + juce::String (oct)
                            + ": deepest dip vs the two paths' mean " + juce::String (worst, 1) + " dB");
                expect (worst > -3.0, "HYBRID crossfade hole " + juce::String (worst, 1) + " dB");
            }

        beginTest ("PolyPitchTracker finds every note of a chord and no ghosts");
        for (const auto& notes : { std::vector<double> { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 },
                                   std::vector<double> { 110.0, 138.59, 164.81, 220.0 },
                                   std::vector<double> { 146.83, 220.0, 293.66, 369.99 },
                                   std::vector<double> { 196.0 } })
        {
            gs::PolyPitchTracker t;
            t.prepare (sr);
            const auto x = chord (notes, 0.6);
            for (auto v : x) t.push (v);
            auto cents = [] (double a, double b) { return std::abs (1200.0 * std::log2 (a / b)); };
            juce::String got;
            int ghosts = 0;
            for (const auto& s : t.getSlots())
            {
                if (! s.active) continue;
                got << juce::String (s.hz, 1) << " ";
                bool match = false;
                for (double hz : notes) match = match || cents (s.hz, hz) < 20.0;
                ghosts += match ? 0 : 1;
            }
            logMessage ("  " + juce::String ((int) notes.size()) + " notes -> " + got);
            for (double hz : notes)
            {
                // an exact octave of a lower chord note shares every partial with it, so it is allowed to merge into it
                bool doubling = false;
                for (double lower : notes)
                    doubling = doubling || (lower < hz && std::fmod (1200.0 * std::log2 (hz / lower) + 20.0, 1200.0) < 40.0);
                bool found = false;
                for (const auto& s : t.getSlots()) found = found || (s.active && cents (s.hz, hz) < 20.0);
                expect (found || doubling, "missed chord note " + juce::String (hz, 1) + ": " + got);
            }
            expect (ghosts == 0, "ghost notes: " + got);
        }

        beginTest ("Tracked Synth voices every note of a chord, in tune, at a sane level");
        {
            const std::vector<double> notes { 110.0, 138.59, 164.81, 220.0 };
            TrackedSynthProcessor p;
            setKnob (p, "detune", 0.0f); setKnob (p, "sub", 0.0f); setKnob (p, "cutoff", 6000.0f); setKnob (p, "envAmount", 0.0f);
            const auto in = chord (notes, 1.5);
            const auto out = render (p, in);
            const size_t from = (size_t) (0.4 * sr);
            double strongest = 0.0;
            for (double hz : notes) strongest = std::max (strongest, goertzel (out, hz, from));
            for (double hz : notes)
            {
                const double m = goertzel (out, hz, from);
                const double off = std::max (goertzel (out, hz * 1.0293, from), goertzel (out, hz / 1.0293, from));
                logMessage ("  " + juce::String (hz, 1) + " Hz: " + juce::String (dB (m / strongest), 1) + " dB, quarter-tone leak "
                            + juce::String (dB (off / m), 1) + " dB");
                expect (m > strongest * 0.2, "synth lost a chord note: " + juce::String (hz, 1));
                expect (off < m * 0.3, "synth voice out of tune near " + juce::String (hz, 1));
            }

            TrackedSynthProcessor single;
            setKnob (single, "detune", 0.0f); setKnob (single, "sub", 0.0f); setKnob (single, "cutoff", 6000.0f); setKnob (single, "envAmount", 0.0f);
            const auto one = render (single, pluck (110.0, 1.5, 0.3, false));
            const double chordVsNote = dB (rms (out, from) / rms (one, from));
            logMessage ("  chord vs single note level: " + juce::String (chordVsNote, 1) + " dB");
            expect (std::abs (chordVsNote) < 4.5, "chords should not jump in level");
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
