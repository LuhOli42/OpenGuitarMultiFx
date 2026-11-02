#pragma once

#include "SynthParts.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <functional>
#include <vector>

namespace openguitarmultifx::gs
{

/** Polyphonic note tracker (chords, double stops, single notes) for the tracked synth.

    The input is decimated to ~16 kHz and analysed every ~6 ms with a 96 ms Hann window (zero-padded FFT; long enough to split a major third at A2). Spectral peaks become f0
    candidates (the 24 strongest peaks / 1..3); each candidate's salience is the weighted sum of the peaks sitting on its harmonics
    (Klapuri-style 1/h-ish weights). Notes are taken greedily, strongest first, and after each pick the partials it
    explains are subtracted down to a spectrally smooth envelope, so a shared partial (the fifth's 2nd harmonic on the
    root's 3rd) is left for the other note while plain harmonics of the picked note cannot come back as notes. A
    candidate needs energy AT its fundamental, which rules out the sub-octave / missing-root ghosts that make
    autocorrelation trackers jump.

    Detected notes feed kMaxNotes persistent slots: a slot becomes active after two consecutive hits, follows its
    note through bends (nearest match within 1.5 semitones), and frees itself after a few missed frames. Slot
    indices are stable, so a synth can keep one voice per slot.

    Cost: one 2048-point real FFT plus a few thousand multiply-adds per frame (~160 frames/s). No allocation after
    prepare(). */
class PolyPitchTracker
{
public:
    static constexpr int kMaxNotes = 6;

    struct Slot
    {
        double hz = 0.0, strength = 0.0;
        uint32_t id = 0; // changes whenever the slot starts a new note
        int hits = 0, misses = 0;
        bool active = false, used = false;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dec = juce::jmax (1, (int) std::lround (fs / 16000.0));
        rate = fs / dec;
        winLen = juce::jmax (64, (int) std::lround (0.096 * rate));
        order = 1;
        while ((1 << order) < winLen) ++order;
        size = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        ring.assign ((size_t) size, 0.0f);
        work.assign ((size_t) size * 2, 0.0f);
        window.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            // asymmetric Hann: slow rise over the older 70 %, fast fall over the newest 30 %, so the analysis
            // centre sits ~19 ms behind "now" instead of ~48 ms (note changes land sooner) at nearly Hann resolution
            const double t = (i + 0.5) / winLen, split = 0.7;
            const double u = t < split ? 0.5 * t / split : 0.5 + 0.5 * (t - split) / (1.0 - split);
            window[(size_t) i] = i < winLen ? (float) (0.5 - 0.5 * std::cos (2.0 * kPi * u)) : 0.0f;
        }
        hop = juce::jmax (1, (int) std::lround (0.006 * rate));
        binHz = rate / size;
        aa[0].lowpass (0.42 * rate, fs, 0.54);
        aa[1].lowpass (0.42 * rate, fs, 1.31);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        aa[0].reset(); aa[1].reset();
        writePos = 0; decCount = 0; hopCount = 0;
        slots = {};
        frameIndex = 0;
        nextId = 0;
    }

    /** Feeds one input-rate sample. Returns true when a new analysis frame updated the slots. */
    bool push (float x)
    {
        const double y = aa[1].process (aa[0].process (x));
        if (++decCount < dec) return false;
        decCount = 0;
        ring[(size_t) writePos] = (float) y;
        writePos = (writePos + 1) & (size - 1);
        if (++hopCount < hop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    const std::array<Slot, kMaxNotes>& getSlots() const noexcept { return slots; }

    /** Raw notes from the last frame (strongest first), for tests and diagnostics. */
    int getNumDetected() const noexcept { return numDetected; }
    double getDetectedHz (int i) const noexcept { return detected[(size_t) i].hz; }

    double minHz = 38.0, maxHz = 1350.0;

private:
    static constexpr int kMaxPeaks = 96, kMaxHarm = 16, kCandPeaks = 24, kMaxCand = kCandPeaks * 3;
    struct Peak { double hz, amp; };
    struct Cand { double f0, salience; int n; std::array<int16_t, kMaxHarm> peak; std::array<uint8_t, kMaxHarm> harm; bool alive; };
    struct Found { double hz, strength; };

    /** Interpolated FFT magnitude at a frequency. */
    double magAt (double hz) const
    {
        const double b = hz / binHz;
        const int k = (int) std::floor (b);
        if (k < 1 || k + 1 >= size / 2) return 0.0;
        const double f = b - k;
        return (1.0 - f) * work[(size_t) k] + f * work[(size_t) k + 1];
    }

    int nearestPeak (double hz, double rel) const
    {
        int lo = 0, hi = numPeaks;
        while (lo < hi) { const int mid = (lo + hi) / 2; if (peaks[(size_t) mid].hz < hz) lo = mid + 1; else hi = mid; }
        int best = -1; double bestD = 1e9;
        for (int k = lo - 1; k <= lo; ++k)
            if (k >= 0 && k < numPeaks)
            {
                const double d = std::abs (peaks[(size_t) k].hz - hz);
                if (d < bestD) { bestD = d; best = k; }
            }
        const double tol = std::max (0.4 * binHz, hz * rel);
        return bestD <= tol ? best : -1;
    }

    static double weight (double f0, int h) { return (f0 + 52.0) / (h * f0 + 320.0); }

    void analyse()
    {
        ++frameIndex;
        for (int i = 0; i < size; ++i)
            work[(size_t) i] = ring[(size_t) ((writePos + size - winLen + i) & (size - 1))] * window[(size_t) i];
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (work.data(), true);

        // peaks
        const int maxBin = std::min (size / 2 - 2, (int) (5500.0 / binHz));
        float top = 0.0f;
        for (int k = 1; k <= maxBin; ++k) top = std::max (top, work[(size_t) k]);
        numPeaks = 0;
        numDetected = 0;
        const double floorAmp = std::max ((double) top * 0.01, 1e-6 * winLen);
        if (top > floorAmp)
        {
            for (int k = 2; k < maxBin && numPeaks < kMaxPeaks; ++k)
            {
                const double m0 = work[(size_t) k - 1], m1 = work[(size_t) k], m2 = work[(size_t) k + 1];
                if (m1 < floorAmp || m1 < m0 || m1 <= m2) continue;
                const double l0 = std::log (m0 + 1e-12), l1 = std::log (m1), l2 = std::log (m2 + 1e-12);
                const double den = l0 - 2.0 * l1 + l2;
                const double d = den < 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (l0 - l2) / den) : 0.0;
                peaks[(size_t) numPeaks++] = { (k + d) * binHz, std::exp (l1 - 0.25 * (l0 - l2) * d) };
            }
        }

        // candidates: each of the strongest peaks as harmonic 1..3 of a fundamental in range
        double candFloor = 0.0;
        if (numPeaks > kCandPeaks)
        {
            for (int p = 0; p < numPeaks; ++p) sortedAmp[(size_t) p] = peaks[(size_t) p].amp;
            std::nth_element (sortedAmp.begin(), sortedAmp.begin() + (kCandPeaks - 1), sortedAmp.begin() + numPeaks, std::greater<>());
            candFloor = sortedAmp[(size_t) kCandPeaks - 1];
        }
        int numCand = 0;
        for (int p = 0; p < numPeaks; ++p)
            for (int h = 1; h <= 3 && numCand < kMaxCand && peaks[(size_t) p].amp >= candFloor; ++h)
            {
                const double f0 = peaks[(size_t) p].hz / h;
                if (f0 < minHz || f0 > maxHz) continue;
                bool dup = false;
                for (int c = 0; c < numCand && ! dup; ++c) dup = std::abs (cands[(size_t) c].f0 - f0) < f0 * 0.012;
                if (dup) continue;
                auto& c = cands[(size_t) numCand];
                c.n = 0;
                double num = 0.0, den = 0.0, strongest = 0.0;
                int low = 0;
                bool h1 = false, h2 = false, h3 = false;
                for (int hh = 1; hh <= kMaxHarm && hh * f0 < 5500.0; ++hh)
                {
                    const int idx = nearestPeak (hh * f0, 0.015);
                    if (idx < 0) continue;
                    bool taken = false;
                    for (int j = 0; j < c.n && ! taken; ++j) taken = c.peak[(size_t) j] == idx;
                    if (taken) continue;
                    c.peak[(size_t) c.n] = (int16_t) idx;
                    c.harm[(size_t) c.n] = (uint8_t) hh;
                    ++c.n;
                    const double a = peaks[(size_t) idx].amp;
                    strongest = std::max (strongest, a);
                    h1 = h1 || hh == 1; h2 = h2 || hh == 2; h3 = h3 || hh == 3;
                    if (hh <= 6) { ++low; num += a * hh * peaks[(size_t) idx].hz; den += a * hh * hh; }
                }
                if (c.n == 0 || den <= 0.0) continue;
                c.f0 = num / den;
                // the root has to be there, so a note's harmonics can never add up to a ghost sub-octave: either
                // as its own peak, or (two low roots closer than the FFT can split) as energy under a merged lobe
                // backed by both the 2nd and 3rd harmonics
                const bool rootPeak = h1 && peaks[(size_t) c.peak[0]].amp >= 0.08 * strongest;
                const bool mergedRoot = ! rootPeak && h2 && h3 && magAt (c.f0) >= 0.2 * strongest;
                if (! rootPeak && ! mergedRoot) continue;
                if (c.f0 < 600.0 && low < 2) continue;
                c.alive = true;
                ++numCand;
            }

        for (int p = 0; p < numPeaks; ++p) residual[(size_t) p] = peaks[(size_t) p].amp;

        double first = 0.0;
        while (numDetected < kMaxNotes)
        {
            int best = -1; double bestS = 0.0;
            for (int c = 0; c < numCand; ++c)
            {
                auto& cd = cands[(size_t) c];
                if (! cd.alive) continue;
                double s = 0.0;
                for (int j = 0; j < cd.n; ++j) s += weight (cd.f0, cd.harm[(size_t) j]) * residual[(size_t) cd.peak[(size_t) j]];
                cd.salience = s;
                if (s > bestS) { bestS = s; best = c; }
            }
            if (best < 0 || bestS < 0.18 * first) break;
            auto& cd = cands[(size_t) best];
            cd.alive = false;
            if (first == 0.0) first = bestS;

            bool clash = false;
            for (int i = 0; i < numDetected && ! clash; ++i)
                clash = std::abs (1200.0 * std::log2 (cd.f0 / detected[(size_t) i].hz)) < 70.0;
            // its partials, ordered by harmonic number, are subtracted down to a smooth envelope
            std::array<double, kMaxHarm + 2> a {};
            for (int j = 0; j < cd.n; ++j) a[(size_t) cd.harm[(size_t) j]] = residual[(size_t) cd.peak[(size_t) j]];
            for (int j = 0; j < cd.n; ++j)
            {
                const int h = cd.harm[(size_t) j];
                const double nb = h == 1 ? a[2] : 0.5 * (a[(size_t) h - 1] + a[(size_t) h + 1]);
                const double take = std::min (a[(size_t) h], std::max (nb, 0.5 * a[(size_t) h]) * 1.15);
                auto& r = residual[(size_t) cd.peak[(size_t) j]];
                r = std::max (0.0, r - take);
            }
            if (! clash) detected[(size_t) numDetected++] = { cd.f0, bestS };
            for (int c = 0; c < numCand; ++c) // octave-up / sub-harmonic duplicates of what we just took
            {
                auto& o = cands[(size_t) c];
                if (o.alive && std::abs (1200.0 * std::log2 (o.f0 / cd.f0)) < 70.0) o.alive = false;
            }
        }
        updateSlots();
    }

    void updateSlots()
    {
        std::array<bool, kMaxNotes> matched {};
        std::array<bool, kMaxNotes> usedDet {};
        // greedy nearest matching, closest pairs first
        for (int pass = 0; pass < kMaxNotes; ++pass)
        {
            int bs = -1, bd = -1; double bc = 150.0;
            for (int s = 0; s < kMaxNotes; ++s)
            {
                if (! slots[(size_t) s].used || matched[(size_t) s]) continue;
                for (int d = 0; d < numDetected; ++d)
                {
                    if (usedDet[(size_t) d]) continue;
                    const double c = std::abs (1200.0 * std::log2 (detected[(size_t) d].hz / slots[(size_t) s].hz));
                    if (c < bc) { bc = c; bs = s; bd = d; }
                }
            }
            if (bs < 0) break;
            matched[(size_t) bs] = usedDet[(size_t) bd] = true;
            auto& sl = slots[(size_t) bs];
            sl.hz = detected[(size_t) bd].hz;
            sl.strength = detected[(size_t) bd].strength;
            sl.misses = 0;
            if (++sl.hits >= 2) sl.active = true;
        }
        for (int s = 0; s < kMaxNotes; ++s)
        {
            auto& sl = slots[(size_t) s];
            if (! sl.used || matched[(size_t) s]) continue;
            if (++sl.misses > (sl.active ? 3 : 0)) sl = {};
        }
        for (int d = 0; d < numDetected; ++d)
        {
            if (usedDet[(size_t) d]) continue;
            for (auto& sl : slots)
                if (! sl.used)
                {
                    sl.used = true; sl.active = false; sl.hits = 1; sl.misses = 0; sl.id = ++nextId;
                    sl.hz = detected[(size_t) d].hz; sl.strength = detected[(size_t) d].strength;
                    break;
                }
        }
    }

    double fs = 48000.0, rate = 16000.0, binHz = 15.625;
    int dec = 3, order = 11, size = 2048, winLen = 1536, hop = 96;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, work, window;
    mt::Biquad aa[2];
    int writePos = 0, decCount = 0, hopCount = 0;
    int64_t frameIndex = 0;
    uint32_t nextId = 0;
    std::array<Peak, kMaxPeaks> peaks {};
    std::array<double, kMaxPeaks> residual {}, sortedAmp {};
    std::array<Cand, kMaxCand> cands {};
    std::array<Found, kMaxNotes> detected {};
    int numPeaks = 0, numDetected = 0;
    std::array<Slot, kMaxNotes> slots {};
};

} // namespace openguitarmultifx::gs
