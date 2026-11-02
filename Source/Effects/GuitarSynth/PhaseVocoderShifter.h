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
    thirds of a chord by ~14 cents). Splitting a third at A1 takes a ~170 ms analysis window, but the window is
    asymmetric: a slow rise over the past, then the fall of a short synthesis Hann over the newest ~32 ms, and only
    that newest stretch is resynthesised (analysis x synthesis == Hann over it). Latency is the synthesis length,
    not the analysis length (a symmetric 85 ms window cost 85 ms). No allocation after prepare(). */
class PhaseVocoderShifter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        order = 1;
        while ((1 << order) < (int) std::lround (0.17 * fs)) ++order;
        size = 1 << order;
        mask = size - 1;
        synthLen = std::max (16, 4 * (int) std::lround (0.032 * fs / 4.0));
        hop = synthLen / 4;
        bins = size / 2 + 1;
        fft.prepare (order);
        spec.assign ((size_t) size + 2, 0.0f);
        in.assign ((size_t) size, 0.0f);
        out.assign ((size_t) size, 0.0f);
        frame.assign ((size_t) size, 0.0f);
        window.resize ((size_t) size);
        synthWindow.resize ((size_t) synthLen);
        const int rise = size - synthLen / 2;
        for (int i = 0; i < size; ++i)
        {
            const double w = i < rise ? std::sin (kPi * i / (2.0 * rise)) : std::cos (kPi * (i - rise) / synthLen);
            window[(size_t) i] = (float) (w * w);
        }
        for (int m = 0; m < synthLen; ++m)
        {
            const double hann = std::pow (std::sin (kPi * m / synthLen), 2.0);
            synthWindow[(size_t) m] = (float) (hann / std::max (1e-9, (double) window[(size_t) (size - synthLen + m)]));
        }
        norm = (float) (2.0 * hop / synthLen); // Hann at hop synthLen/4 overlap-adds to 2
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

    int getLatencySamples() const noexcept { return synthLen; }

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

        // the frame's newest synthLen samples, starting at the next sample read out: synthLen samples late
        for (int m = 0; m < synthLen; ++m)
            out[(size_t) ((pos + m) & mask)] += frame[(size_t) (size - synthLen + m)] * synthWindow[(size_t) m] * norm;
    }

    double fs = 48000.0;
    int order = 13, size = 8192, mask = 8191, synthLen = 1536, hop = 384, bins = 2049, pos = 0, count = 0;
    float norm = 1.0f;
    RealFft fft;
    std::vector<float> in, out, frame, spec, window, synthWindow, mag, phase, prevPhase, trueFreq;
    std::vector<double> acc, newAcc;
    std::vector<char> accSet;
    std::vector<int> peaks;
};

} // namespace openguitarmultifx::gs
