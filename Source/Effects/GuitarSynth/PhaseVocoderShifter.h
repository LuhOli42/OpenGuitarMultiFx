#pragma once

#include "RealFft.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace openguitarmultifx::gs
{

/** Polyphonic fixed-ratio pitch shifter: a phase vocoder with identity phase locking (Laroche & Dolson).
    Each spectral peak moves to ratio x its measured frequency with its window lobe intact, and the bins around it
    keep their phase offset to the peak, so every note of a chord lands in tune (the splice shifter detunes the
    thirds of a chord by ~14 cents). The window has to split a major third at A2 (~28 Hz apart), which takes
    ~85 ms: that window length is also the latency. No allocation after prepare(). */
class PhaseVocoderShifter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        order = 1;
        while ((1 << order) < (int) std::lround (0.085 * fs)) ++order;
        size = 1 << order;
        mask = size - 1;
        hop = size / 8;
        bins = size / 2 + 1;
        fft.prepare (order);
        spec.assign ((size_t) size + 2, 0.0f);
        in.assign ((size_t) size, 0.0f);
        out.assign ((size_t) size, 0.0f);
        frame.assign ((size_t) size, 0.0f);
        window.resize ((size_t) size);
        double sumSq = 0.0;
        for (int i = 0; i < size; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / size));
            sumSq += (double) window[(size_t) i] * window[(size_t) i];
        }
        norm = (float) (hop / sumSq);
        mag.assign ((size_t) bins, 0.0f);
        phase.assign ((size_t) bins, 0.0f);
        prevPhase.assign ((size_t) bins, 0.0f);
        trueFreq.assign ((size_t) bins, 0.0f);
        acc.assign ((size_t) bins, 0.0);
        newAcc.assign ((size_t) bins, 0.0);
        accSet.assign ((size_t) bins, 0);
        peaks.assign ((size_t) bins, 0);
        reset();
    }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        std::fill (out.begin(), out.end(), 0.0f);
        std::fill (prevPhase.begin(), prevPhase.end(), 0.0f);
        std::fill (acc.begin(), acc.end(), 0.0);
        pos = 0;
        count = 0;
    }

    int getLatencySamples() const noexcept { return size; }

    /** Push one input sample and return one shifted sample, getLatencySamples() late. */
    float process (float x, double ratio)
    {
        in[(size_t) pos] = x;
        const float y = out[(size_t) pos];
        out[(size_t) pos] = 0.0f;
        pos = (pos + 1) & mask;
        if (++count == hop)
        {
            count = 0;
            runFrame (ratio);
        }
        return y;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static double princArg (double a) { return a - 2.0 * kPi * std::floor ((a + kPi) / (2.0 * kPi)); }

    void runFrame (double ratio)
    {
        for (int i = 0; i < size; ++i)
            frame[(size_t) i] = in[(size_t) ((pos + i) & mask)] * window[(size_t) i];
        fft.forward (frame.data(), spec.data());

        float top = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const float re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
            mag[(size_t) k] = std::sqrt (re * re + im * im);
            phase[(size_t) k] = std::atan2 (im, re);
            const double omega = 2.0 * kPi * k / size;
            const double dev = princArg (phase[(size_t) k] - prevPhase[(size_t) k] - omega * hop);
            trueFreq[(size_t) k] = (float) (omega + dev / hop);
            prevPhase[(size_t) k] = phase[(size_t) k];
            top = std::max (top, mag[(size_t) k]);
        }

        int numPeaks = 0;
        const float floorMag = std::max (top * 1e-3f, 1e-9f);
        for (int k = 1; k < bins - 1; ++k)
            if (mag[(size_t) k] > floorMag && mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1])
                peaks[(size_t) numPeaks++] = k;

        std::fill (spec.begin(), spec.end(), 0.0f);
        std::fill (accSet.begin(), accSet.end(), (char) 0);
        for (int i = 0; i < numPeaks; ++i)
        {
            const int p = peaks[(size_t) i];
            const int lo = i > 0 ? (peaks[(size_t) i - 1] + p) / 2 + 1 : 0;
            const int hi = i + 1 < numPeaks ? (p + peaks[(size_t) i + 1]) / 2 : bins - 1;
            const int q = (int) std::lround (p * ratio);
            const double phq = acc[(size_t) q] + ratio * trueFreq[(size_t) p] * hop;
            newAcc[(size_t) q] = phq;
            accSet[(size_t) q] = 1;
            for (int k = lo; k <= hi; ++k)
            {
                const int o = q + k - p;
                if (o < 0 || o >= bins) continue;
                const double ph = phq + phase[(size_t) k] - phase[(size_t) p];
                spec[(size_t) (2 * o)] += (float) (mag[(size_t) k] * std::cos (ph));
                spec[(size_t) (2 * o + 1)] += (float) (mag[(size_t) k] * std::sin (ph));
            }
        }
        for (int k = 0; k < bins; ++k)
            if (accSet[(size_t) k] != 0) acc[(size_t) k] = princArg (newAcc[(size_t) k]);
        fft.inverse (spec.data(), frame.data());

        for (int i = 0; i < size; ++i)
            out[(size_t) ((pos + i) & mask)] += frame[(size_t) i] * window[(size_t) i] * norm;
    }

    double fs = 48000.0;
    int order = 12, size = 4096, mask = 4095, hop = 512, bins = 2049, pos = 0, count = 0;
    float norm = 1.0f;
    RealFft fft;
    std::vector<float> in, out, frame, spec, window, mag, phase, prevPhase, trueFreq;
    std::vector<double> acc, newAcc;
    std::vector<char> accSet;
    std::vector<int> peaks;
};

} // namespace openguitarmultifx::gs
