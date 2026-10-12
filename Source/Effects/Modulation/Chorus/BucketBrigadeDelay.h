#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace openguitarmultifx
{

/**
    A bucket-brigade delay chip (MN3007, MN3207, MN3002, SAD1024...) together with the low-pass filters that sit in
    front of and behind it in every real application circuit -- M. Holters & J. D. Parker, "A Combined Model for a
    Bucket Brigade Device and its Input and Output Filters", DAFx-18 (Algorithm 1).

    The chip itself is what it physically is: a queue of N/2 samples that moves on the chip's own clock (N clock edges,
    N/2 periods of delay), at whatever rate the LFO-driven clock runs. The resampling between the audio rate and that
    clock is done BY the circuit's own anti-alias / reconstruction filters, turned into digital form with a modified
    impulse-invariant transform that lets the input filter be read, and the output filter be driven, at any instant
    between two audio samples. So there is no interpolator of our own, the sample-and-hold "sinc" droop and the BBD's
    own aliasing are reproduced, and a clock change bends the pitch smoothly the way the chip does.

    Filters are given as their poles and zeros (analog, rad/s) and DC gain -- analog::ladderTransfer() derives them
    from a schematic's part values -- and the partial-fraction residues are derived here. Poles must be simple and come
    in conjugate pairs or be real; zeros must be fewer than the poles. Not modelled (as in the paper): the chip's noise, its gain error and its
    soft clipping near the rails; a pedal adds those around this class where its circuit needs them.
*/
class BucketBrigadeDelay
{
public:
    using Complex = std::complex<double>;

    struct Filter
    {
        std::vector<Complex> poles; // analog, rad/s, all in the left half-plane
        double dcGain = 1.0;
        std::vector<Complex> zeros {}; // fewer than the poles (strictly proper) -- e.g. a cap across a series resistor
    };

    /** stages = the chip's stage count (1024 for an MN3007/MN3207, 512 for an MN3002). */
    void prepare (double sampleRate, int stages, const Filter& input, const Filter& output)
    {
        fs = sampleRate;
        ts = 1.0 / sampleRate;
        queue.assign ((size_t) std::max (1, stages / 2), 0.0);
        head = 0;
        setupSide (input, in, true);
        setupSide (output, out, false);
        nextEdge = 0.0;
        edgeIndex = 0;
        yOld = 0.0;
    }

    void reset()
    {
        std::fill (queue.begin(), queue.end(), 0.0);
        head = 0;
        for (auto& s : in) s.x = 0.0;
        for (auto& s : out) s.x = 0.0;
        nextEdge = 0.0;
        edgeIndex = 0;
        yOld = 0.0;
    }

    /** One audio sample. clockHz = the chip's clock frequency for this sample (edges come at twice that rate). */
    double process (double u, double clockHz)
    {
        const double edgeStep = fs / (2.0 * std::max (1.0, clockHz)); // audio samples between two clock edges

        // Clock edges inside (k-1, k] -- nextEdge is kept relative to sample k-1, so d = nextEdge. The exponentials
        // exp(p Ts d) and exp(p Ts (1-d)) are stepped edge to edge by a fixed ratio instead of evaluated per edge:
        // a chorus clock puts several edges in every audio sample and std::exp dominated the cost.
        if (nextEdge < 1.0 || ((edgeIndex & 1) != 0 && nextEdge <= 1.0))
        {
            for (auto& s : in)
            {
                s.e = std::exp (s.pTs * nextEdge);
                s.step = std::exp (s.pTs * edgeStep);
            }
            for (auto& s : out)
            {
                s.e = std::exp (s.pTs * (1.0 - nextEdge));
                s.step = std::exp (-s.pTs * edgeStep);
            }

            while (nextEdge < 1.0 || ((edgeIndex & 1) != 0 && nextEdge <= 1.0))
            {
                if ((edgeIndex & 1) == 0)
                {
                    // Input transistor closes: sample the input filter's output at this instant.
                    double v = 0.0;
                    for (const auto& s : in)
                        v += s.weight * (s.g * s.e * s.x).real();
                    queue[head] = v;
                    if (++head >= queue.size())
                        head = 0;
                }
                else
                {
                    // Output switches to the next stored sample (the oldest one in the queue).
                    const double yBbd = queue[head];
                    const double delta = yBbd - yOld;
                    yOld = yBbd;
                    for (auto& s : out)
                        s.x += s.g * s.e * delta;
                }
                for (auto& s : in)
                    s.e *= s.step;
                for (auto& s : out)
                    s.e *= s.step;
                ++edgeIndex;
                nextEdge += edgeStep;
            }
        }
        nextEdge -= 1.0;

        for (auto& s : in)
            s.x = s.pBar * s.x + u;

        double y = h0 * yOld;
        for (auto& s : out)
        {
            y += s.weight * s.x.real();
            s.x *= s.pBar;
        }
        return y;
    }

    /** The chip's delay at a constant clock: N/2 clock periods. */
    double delaySeconds (double clockHz) const noexcept { return (double) queue.size() / clockHz; }

    /** The clock that gives a delay (seconds) -- what an LFO-driven VCO is usually specified by. */
    double clockForDelay (double seconds) const noexcept { return (double) queue.size() / seconds; }

private:
    struct Section
    {
        Complex p, r, pTs, pBar, g, x, e, step;
        double weight = 1.0; // 2 for a pole standing in for its conjugate pair (the pair's states are conjugates)
    };

    void setupSide (const Filter& f, std::vector<Section>& sections, bool isInput)
    {
        sections.clear();
        const size_t m = f.poles.size();
        // H(s) = K prod(s - z) / prod(s - p_i), K chosen for the requested DC gain: H(0) = K prod(-z) / prod(-p_i).
        Complex prodNeg = 1.0, zeroNeg = 1.0;
        for (const auto& p : f.poles)
            prodNeg *= -p;
        for (const auto& z : f.zeros)
            zeroNeg *= -z;
        const Complex k = f.dcGain * prodNeg / zeroNeg;
        for (size_t i = 0; i < m; ++i)
        {
            Complex denom = 1.0;
            for (const auto& z : f.zeros)
                denom /= f.poles[i] - z;
            for (size_t j = 0; j < m; ++j)
                if (j != i)
                    denom *= f.poles[i] - f.poles[j];
            Section s;
            s.p = f.poles[i];
            s.r = k / denom;
            s.pTs = s.p * ts;
            s.pBar = std::exp (s.pTs);
            // input: g(d) = Ts r pBar^d ; output: g(d) = r/p pBar^(1-d) -- the pBar^... factor is applied per edge.
            s.g = isInput ? ts * s.r : s.r / s.p;
            s.x = 0.0;
            sections.push_back (s);
        }

        if (! isInput)
        {
            h0 = 0.0;
            for (const auto& s : sections)
                h0 -= (s.r / s.p).real();
        }

        // Keep one pole of each conjugate pair (the one with positive imaginary part), counted twice.
        std::vector<Section> kept;
        for (auto s : sections)
        {
            if (std::abs (s.p.imag()) <= 1.0e-9 * std::abs (s.p))
                kept.push_back (s);
            else if (s.p.imag() > 0.0)
            {
                s.weight = 2.0;
                kept.push_back (s);
            }
        }
        sections = std::move (kept);
    }

    double fs = 48000.0, ts = 1.0 / 48000.0;
    std::vector<double> queue;
    size_t head = 0;
    std::vector<Section> in, out;
    double h0 = 0.0;
    double nextEdge = 0.0;
    long long edgeIndex = 0;
    double yOld = 0.0;
};

} // namespace openguitarmultifx
