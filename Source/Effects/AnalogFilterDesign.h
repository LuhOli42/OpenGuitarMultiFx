#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace openguitarmultifx::analog
{

/**
    Linear analog filter sections described by their schematic part values, turned into exact s-domain transfer
    functions -- so a pedal states "R9 10K, C6 3.3n to ground, C7 8.2n to the follower's emitter..." instead of a
    hand-derived cutoff, and the poles/zeros come out of the circuit itself.

    Polynomials are in ascending powers of s. Everything here runs on the control thread (prepare()).
*/
using Complex = std::complex<double>;
using Poly = std::vector<Complex>;

inline Complex evaluate (const Poly& p, Complex s)
{
    Complex v = 0.0;
    for (size_t k = p.size(); k-- > 0;)
        v = v * s + p[k];
    return v;
}

/** Roots of a polynomial (ascending coefficients), Durand-Kerner. `frequencyScale` is where the roots are expected
    (rad/s): the polynomial is solved in s / frequencyScale, where its coefficients are comparable, and negligible
    leading terms are judged there -- in raw powers of s an RC filter's s^3 term is ~1e-26 and only looks negligible. */
inline std::vector<Complex> roots (Poly raw, double frequencyScale = 1.0)
{
    Poly p (raw.size());
    for (size_t j = 0; j < raw.size(); ++j)
        p[j] = raw[j] * std::pow (frequencyScale, (double) j);
    double scale = 0.0;
    for (const auto& c : p)
        scale = std::max (scale, std::abs (c));
    while (p.size() > 1 && std::abs (p.back()) <= 1.0e-12 * scale)
        p.pop_back();
    const int n = (int) p.size() - 1;
    if (n < 1)
        return {};

    std::vector<Complex> monic (p.size());
    for (size_t k = 0; k < p.size(); ++k)
        monic[k] = p[k] / p.back();

    // Start on a circle of the Cauchy bound's order, rotated off the real axis.
    double radius = 0.0;
    for (int k = 0; k < n; ++k)
        radius = std::max (radius, std::pow (std::abs (monic[(size_t) k]), 1.0 / (double) (n - k)));
    radius = std::max (radius, 1.0e-12);
    std::vector<Complex> z ((size_t) n);
    for (int k = 0; k < n; ++k)
        z[(size_t) k] = std::polar (radius, 0.4 + 2.0 * 3.14159265358979323846 * k / n);

    for (int iter = 0; iter < 2000; ++iter)
    {
        double change = 0.0;
        for (int i = 0; i < n; ++i)
        {
            Complex denom = 1.0;
            for (int j = 0; j < n; ++j)
                if (j != i)
                    denom *= z[(size_t) i] - z[(size_t) j];
            const Complex step = evaluate (monic, z[(size_t) i]) / denom;
            z[(size_t) i] -= step;
            change = std::max (change, std::abs (step) / std::max (1.0e-300, std::abs (z[(size_t) i])));
        }
        if (change < 1.0e-15)
            break;
    }
    // Snap numerically-real roots onto the axis so conjugate pairing downstream is exact.
    for (auto& r : z)
    {
        if (std::abs (r.imag()) < 1.0e-9 * std::abs (r))
            r = Complex (r.real(), 0.0);
        r *= frequencyScale;
    }
    return z;
}

/**
    A ladder of nodes driven from a source on the left and read by a unity-gain follower (emitter/source follower or
    op-amp buffer) on the last node -- the shape of every BBD anti-alias / reconstruction filter (Sallen-Key with a
    transistor follower, plus passive RC poles in front).

    Node i is reached from node i-1 (or the input, for i = 0) through `seriesR`, optionally with `seriesC` across it,
    and has optional elements to ground (`capToGround`, `resToGround`) and an optional capacitor to the follower's
    OUTPUT (`capToOutput`, the Sallen-Key feedback capacitor). The follower is ideal: it draws no current from the last
    node and its output equals that node's voltage.
*/
struct LadderNode
{
    double seriesR = 0.0;
    double seriesC = 0.0;
    double capToGround = 0.0;
    double capToOutput = 0.0;
    double resToGround = 0.0;
};

struct Rational
{
    Poly num, den; // H(s) = num / den
    double scale = 1.0; // rad/s where its poles/zeros live (for root finding); set by the network solvers
    Complex at (Complex s) const { return evaluate (num, s) / evaluate (den, s); }
};

namespace detail
{
    inline Complex determinant (std::vector<std::vector<Complex>> a)
    {
        const size_t n = a.size();
        Complex det = 1.0;
        for (size_t c = 0; c < n; ++c)
        {
            size_t pivot = c;
            for (size_t r = c + 1; r < n; ++r)
                if (std::abs (a[r][c]) > std::abs (a[pivot][c]))
                    pivot = r;
            if (! (std::abs (a[pivot][c]) > 0.0))
                return 0.0;
            if (pivot != c)
            {
                std::swap (a[pivot], a[c]);
                det = -det;
            }
            det *= a[c][c];
            for (size_t r = c + 1; r < n; ++r)
            {
                const Complex f = a[r][c] / a[c][c];
                for (size_t k = c; k < n; ++k)
                    a[r][k] -= f * a[c][k];
            }
        }
        return det;
    }

    /** Coefficients (in t, s = w0 t) of a polynomial of degree <= m-1 known only by its values -- sampled on the unit
        circle so the interpolation is a well-conditioned DFT. */
    template <typename Fn>
    Poly interpolate (int m, double w0, Fn&& valueAt)
    {
        const double twoPi = 2.0 * 3.14159265358979323846;
        std::vector<Complex> values ((size_t) m);
        for (int k = 0; k < m; ++k)
            values[(size_t) k] = valueAt (w0 * std::polar (1.0, twoPi * k / m));
        Poly coeffs ((size_t) m);
        for (int j = 0; j < m; ++j)
        {
            Complex acc = 0.0;
            for (int k = 0; k < m; ++k)
                acc += values[(size_t) k] * std::polar (1.0, -twoPi * j * k / m);
            coeffs[(size_t) j] = acc / (double) m / std::pow (w0, (double) j); // back to powers of s
        }
        return coeffs;
    }
} // namespace detail

inline Rational ladderTransfer (const std::vector<LadderNode>& nodes)
{
    const size_t n = nodes.size();
    auto matrix = [&nodes, n] (Complex s)
    {
        std::vector<std::vector<Complex>> y (n, std::vector<Complex> (n, 0.0));
        for (size_t i = 0; i < n; ++i)
        {
            const auto& nd = nodes[i];
            const Complex ySeries = 1.0 / nd.seriesR + s * nd.seriesC;
            y[i][i] += ySeries;
            if (i > 0)
            {
                y[i - 1][i - 1] += ySeries;
                y[i][i - 1] -= ySeries;
                y[i - 1][i] -= ySeries;
            }
            y[i][i] += s * nd.capToGround;
            if (nd.resToGround > 0.0)
                y[i][i] += 1.0 / nd.resToGround;
            if (nd.capToOutput > 0.0 && i + 1 < n)
            {
                // Current into a capacitor whose far end is the follower output (= the last node's voltage, but driven).
                y[i][i] += s * nd.capToOutput;
                y[i][n - 1] -= s * nd.capToOutput;
            }
        }
        return y;
    };

    // A scale near the filter's own corner keeps the interpolation well conditioned.
    double logSum = 0.0;
    int count = 0;
    for (const auto& nd : nodes)
        for (double c : { nd.capToGround, nd.capToOutput, nd.seriesC })
            if (c > 0.0)
            {
                logSum += std::log (1.0 / (nd.seriesR * c));
                ++count;
            }
    const double w0 = count > 0 ? std::exp (logSum / count) : 1.0;
    const int m = (int) n + 1;

    Rational h;
    h.scale = w0;
    h.den = detail::interpolate (m, w0, [&] (Complex s) { return detail::determinant (matrix (s)); });
    h.num = detail::interpolate (m, w0, [&] (Complex s)
    {
        auto y = matrix (s);
        const Complex b0 = 1.0 / nodes[0].seriesR + s * nodes[0].seriesC;
        for (size_t r = 0; r < n; ++r)
            y[r][n - 1] = r == 0 ? b0 : Complex (0.0);
        return detail::determinant (y);
    });
    return h;
}

/**
    Any passive R/C network: two-terminal elements between numbered nodes (0 = ground, 1 = the driven input), and the
    voltage transfer from node 1 to `outputNode`. An element is a resistor (r > 0, c == 0), a capacitor (c > 0, r == 0)
    or a resistor in series with a capacitor (both > 0). For networks that are not a simple ladder -- a tone control
    with its pot in the middle.
*/
struct Element
{
    int a = 0, b = 0;
    double r = 0.0, c = 0.0;
};

inline Rational networkTransfer (const std::vector<Element>& elements, int numNodes, int outputNode)
{
    // Unknowns: nodes 2 .. numNodes-1 (index node - 2). Node 1 is driven with 1 V.
    const int n = numNodes - 2;
    auto admittance = [] (const Element& e, Complex s) -> Complex
    {
        if (e.c > 0.0 && e.r > 0.0)
            return s * e.c / (1.0 + s * e.c * e.r);
        if (e.c > 0.0)
            return s * e.c;
        return 1.0 / e.r;
    };
    auto build = [&] (Complex s, std::vector<std::vector<Complex>>& y, std::vector<Complex>& b)
    {
        y.assign ((size_t) n, std::vector<Complex> ((size_t) n, 0.0));
        b.assign ((size_t) n, 0.0);
        for (const auto& e : elements)
        {
            const Complex g = admittance (e, s);
            const int ia = e.a - 2, ib = e.b - 2;
            if (ia >= 0) y[(size_t) ia][(size_t) ia] += g;
            if (ib >= 0) y[(size_t) ib][(size_t) ib] += g;
            if (ia >= 0 && ib >= 0)
            {
                y[(size_t) ia][(size_t) ib] -= g;
                y[(size_t) ib][(size_t) ia] -= g;
            }
            if (e.a == 1 && ib >= 0) b[(size_t) ib] += g;
            if (e.b == 1 && ia >= 0) b[(size_t) ia] += g;
        }
    };

    // Each series-RC element contributes a pole of its own, so clear them by multiplying through: the determinant
    // times prod(1 + s c r) is a polynomial.
    int degree = 0;
    double logSum = 0.0;
    int count = 0;
    for (const auto& e : elements)
        if (e.c > 0.0)
        {
            degree += (e.r > 0.0) ? 2 : 1;
            logSum += std::log (1.0 / (std::max (e.r, 1.0e3) * e.c));
            ++count;
        }
    const double w0 = count > 0 ? std::exp (logSum / count) : 1.0;
    auto clear = [&] (Complex s)
    {
        Complex f = 1.0;
        for (const auto& e : elements)
            if (e.c > 0.0 && e.r > 0.0)
                f *= 1.0 + s * e.c * e.r;
        return f;
    };

    Rational h;
    h.scale = w0;
    const int m = degree + 1;
    h.den = detail::interpolate (m, w0, [&] (Complex s)
    {
        std::vector<std::vector<Complex>> y;
        std::vector<Complex> b;
        build (s, y, b);
        return detail::determinant (y) * clear (s);
    });
    h.num = detail::interpolate (m, w0, [&] (Complex s)
    {
        std::vector<std::vector<Complex>> y;
        std::vector<Complex> b;
        build (s, y, b);
        const size_t col = (size_t) (outputNode - 2);
        for (size_t r = 0; r < (size_t) n; ++r)
            y[r][col] = b[r];
        return detail::determinant (y) * clear (s);
    });
    return h;
}

/** Poles, zeros and DC gain of a rational H(s) -- the form BucketBrigadeDelay::Filter takes. */
struct PoleZero
{
    std::vector<Complex> poles, zeros;
    double dcGain = 1.0;
};

inline PoleZero poleZero (const Rational& h)
{
    PoleZero pz;
    pz.poles = roots (h.den, h.scale);
    pz.zeros = roots (h.num, h.scale);
    pz.dcGain = h.at (0.0).real();
    return pz;
}

inline Poly multiply (const Poly& a, const Poly& b)
{
    Poly r (a.size() + b.size() - 1, 0.0);
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j)
            r[i + j] += a[i] * b[j];
    return r;
}

/**
    A real rational H(s) of low order run as a digital filter: bilinear transform, prewarped at `warpHz` (pick the
    frequency whose position matters most -- a shelf's corner), transposed direct form II in double precision.
*/
class Filter
{
public:
    void prepare (const Rational& hIn, double sampleRate, double warpHz)
    {
        const double k = warpHz > 0.0 ? 2.0 * 3.14159265358979323846 * warpHz
                                            / std::tan (3.14159265358979323846 * warpHz / sampleRate)
                                      : 2.0 * sampleRate;
        // Drop numerically-zero top coefficients (judged at the bilinear's own frequency scale): a spurious s^n term
        // would become a pole sitting on z = -1.
        Rational h = hIn;
        auto trim = [k] (Poly& p)
        {
            double peak = 0.0;
            for (size_t j = 0; j < p.size(); ++j)
                peak = std::max (peak, std::abs (p[j]) * std::pow (k, (double) j));
            while (p.size() > 1 && std::abs (p.back()) * std::pow (k, (double) (p.size() - 1)) <= 1.0e-10 * peak)
                p.pop_back();
        };
        trim (h.num);
        trim (h.den);
        size_t order = std::max (h.num.size(), h.den.size()) - 1;
        auto toZ = [order, k] (const Poly& p)
        {
            // sum_j p_j k^j (1 - z^-1)^j (1 + z^-1)^(order - j)
            Poly acc (order + 1, 0.0);
            for (size_t j = 0; j < p.size(); ++j)
            {
                Poly term { std::pow (k, (double) j) * p[j] };
                for (size_t m = 0; m < j; ++m)
                    term = multiply (term, Poly { 1.0, -1.0 });
                for (size_t m = j; m < order; ++m)
                    term = multiply (term, Poly { 1.0, 1.0 });
                for (size_t q = 0; q < term.size(); ++q)
                    acc[q] += term[q];
            }
            return acc;
        };
        const Poly bz = toZ (h.num), az = toZ (h.den);
        b.assign (order + 1, 0.0);
        a.assign (order + 1, 0.0);
        for (size_t q = 0; q <= order; ++q)
        {
            b[q] = (bz[q] / az[0]).real();
            a[q] = (az[q] / az[0]).real();
        }
        state.assign (order + 1, 0.0);
    }

    void reset() { std::fill (state.begin(), state.end(), 0.0); }

    /** Coefficients blended between two prepared filters of the same order (a knob between two table entries).
        Allocation-free, so audio-thread safe; the state is kept, so a moving knob does not click. */
    void interpolateFrom (const Filter& x, const Filter& y, double t) noexcept
    {
        for (size_t q = 0; q < b.size() && q < x.b.size() && q < y.b.size(); ++q)
        {
            b[q] = x.b[q] + t * (y.b[q] - x.b[q]);
            a[q] = x.a[q] + t * (y.a[q] - x.a[q]);
        }
    }

    size_t order() const noexcept { return b.size() - 1; }

    double process (double x)
    {
        const size_t n = b.size() - 1;
        const double y = b[0] * x + state[0];
        for (size_t q = 1; q <= n; ++q)
            state[q - 1] = b[q] * x - a[q] * y + (q < n ? state[q] : 0.0);
        return y;
    }

private:
    std::vector<double> b { 1.0 }, a { 1.0 }, state { 0.0 };
};

/** First-order building blocks, in s. */
inline Rational ratio (Poly num, Poly den) { return { std::move (num), std::move (den) }; }

/** Inverting op-amp stage: -Zf / Zin, each impedance given as num/den in s. */
inline Rational inverting (const Rational& zf, const Rational& zin)
{
    // -(zf.num / zf.den) * (zin.den / zin.num)
    Poly num = multiply (zf.num, zin.den);
    for (auto& c : num)
        c = -c;
    return { num, multiply (zf.den, zin.num) };
}

/** Impedances: R, 1/(sC), R || 1/(sC), R + 1/(sC), and parallel / series combinations of rationals. */
inline Rational resistor (double r) { return { { r }, { 1.0 } }; }
inline Rational seriesRC (double r, double c) { return { { 1.0, r * c }, { 0.0, c } }; }          // R + 1/sC
inline Rational parallelRC (double r, double c) { return { { r }, { 1.0, r * c } }; }             // R || 1/sC
inline Rational parallel (const Rational& x, const Rational& y)
{
    // (xn/xd)(yn/yd) / (xn/xd + yn/yd) = xn yn / (xn yd + yn xd)
    const Poly num = multiply (x.num, y.num);
    const Poly den1 = multiply (x.num, y.den), den2 = multiply (y.num, x.den);
    Poly den (std::max (den1.size(), den2.size()), 0.0);
    for (size_t i = 0; i < den1.size(); ++i) den[i] += den1[i];
    for (size_t i = 0; i < den2.size(); ++i) den[i] += den2[i];
    return { num, den };
}
inline Rational capacitor (double c) { return { { 1.0 }, { 0.0, c } }; }
inline Rational series (const Rational& x, const Rational& y)
{
    // xn/xd + yn/yd = (xn yd + yn xd) / (xd yd)
    const Poly a = multiply (x.num, y.den), b = multiply (y.num, x.den);
    Poly num (std::max (a.size(), b.size()), 0.0);
    for (size_t i = 0; i < a.size(); ++i) num[i] += a[i];
    for (size_t i = 0; i < b.size(); ++i) num[i] += b[i];
    return { num, multiply (x.den, y.den) };
}
/** x + y for transfer functions as well as impedances (the same arithmetic as series()). */
inline Rational sum (const Rational& x, const Rational& y) { return series (x, y); }
/** 1 / z: an impedance as an admittance and back. */
inline Rational reciprocal (const Rational& z) { return { z.den, z.num }; }
/** Series C into a resistive load R (a coupling capacitor): the high-pass voltage transfer sCR / (1 + sCR). */
inline Rational couplingHighpass (double c, double r) { return { { 0.0, c * r }, { 1.0, c * r } }; }

/** H1 * H2. */
inline Rational cascade (const Rational& x, const Rational& y) { return { multiply (x.num, y.num), multiply (x.den, y.den) }; }

/** Gain constant. */
inline Rational gain (double g) { return { { g }, { 1.0 } }; }

} // namespace openguitarmultifx::analog
