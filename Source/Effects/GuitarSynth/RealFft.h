#pragma once

#include <cmath>
#include <utility>
#include <vector>

namespace openguitarmultifx::gs
{

/** Power-of-two real FFT: an n/2-point radix-2 complex FFT plus the real-signal split.
    juce::dsp::FFT's fallback engine runs a full n-point complex transform through std::complex multiplies
    (__mulsc3 calls), ~10x slower at the 4096 points the phase vocoder needs. No allocation after prepare(). */
class RealFft
{
public:
    void prepare (int order)
    {
        n = 1 << order;
        h = n / 2;
        int bits = 0;
        while ((1 << bits) < h) ++bits;
        rev.resize ((size_t) h);
        for (int i = 0; i < h; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        twr.resize ((size_t) std::max (1, h / 2));
        twi.resize (twr.size());
        for (int k = 0; k < h / 2; ++k)
        {
            twr[(size_t) k] = (float) std::cos (-2.0 * kPi * k / h);
            twi[(size_t) k] = (float) std::sin (-2.0 * kPi * k / h);
        }
        wr.resize ((size_t) h + 1);
        wi.resize ((size_t) h + 1);
        for (int k = 0; k <= h; ++k)
        {
            wr[(size_t) k] = (float) std::cos (-2.0 * kPi * k / n);
            wi[(size_t) k] = (float) std::sin (-2.0 * kPi * k / n);
        }
        re.assign ((size_t) h, 0.0f);
        im.assign ((size_t) h, 0.0f);
    }

    int size() const noexcept { return n; }

    /** x: n samples -> spec: bins 0..n/2 as interleaved re, im (n + 2 floats). */
    void forward (const float* x, float* spec)
    {
        for (int m = 0; m < h; ++m)
        {
            re[(size_t) m] = x[2 * m];
            im[(size_t) m] = x[2 * m + 1];
        }
        transform (1.0f);
        for (int k = 0; k <= h; ++k)
        {
            const int a = k % h, b = (h - k) % h;
            const float er = 0.5f * (re[(size_t) a] + re[(size_t) b]), ei = 0.5f * (im[(size_t) a] - im[(size_t) b]);
            const float orr = 0.5f * (im[(size_t) a] + im[(size_t) b]), oi = -0.5f * (re[(size_t) a] - re[(size_t) b]);
            spec[2 * k] = er + wr[(size_t) k] * orr - wi[(size_t) k] * oi;
            spec[2 * k + 1] = ei + wr[(size_t) k] * oi + wi[(size_t) k] * orr;
        }
    }

    /** spec: bins 0..n/2 interleaved (n + 2 floats) -> x: n samples, scaled so inverse(forward(x)) == x. */
    void inverse (const float* spec, float* x)
    {
        for (int k = 0; k < h; ++k)
        {
            const float ar = spec[2 * k], ai = spec[2 * k + 1];
            const float br = spec[2 * (h - k)], bi = -spec[2 * (h - k) + 1];
            const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
            const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
            const float orr = dr * wr[(size_t) k] + di * wi[(size_t) k], oi = di * wr[(size_t) k] - dr * wi[(size_t) k];
            re[(size_t) k] = er - oi;
            im[(size_t) k] = ei + orr;
        }
        transform (-1.0f);
        const float scale = 1.0f / (float) h;
        for (int m = 0; m < h; ++m)
        {
            x[2 * m] = re[(size_t) m] * scale;
            x[2 * m + 1] = im[(size_t) m] * scale;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void transform (float sign)
    {
        for (int i = 0; i < h; ++i)
            if (const int j = rev[(size_t) i]; i < j)
            {
                std::swap (re[(size_t) i], re[(size_t) j]);
                std::swap (im[(size_t) i], im[(size_t) j]);
            }
        for (int len = 2; len <= h; len <<= 1)
        {
            const int half = len / 2, step = h / len;
            for (int i = 0; i < h; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float cr = twr[(size_t) (j * step)], ci = sign * twi[(size_t) (j * step)];
                    const int a = i + j, b = a + half;
                    const float tr = re[(size_t) b] * cr - im[(size_t) b] * ci;
                    const float ti = re[(size_t) b] * ci + im[(size_t) b] * cr;
                    re[(size_t) b] = re[(size_t) a] - tr;
                    im[(size_t) b] = im[(size_t) a] - ti;
                    re[(size_t) a] += tr;
                    im[(size_t) a] += ti;
                }
        }
    }

    int n = 2, h = 1;
    std::vector<int> rev;
    std::vector<float> twr, twi, wr, wi, re, im;
};

} // namespace openguitarmultifx::gs
