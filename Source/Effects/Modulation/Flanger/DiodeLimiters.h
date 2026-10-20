#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx::diodes
{

/** A diode type for the limiters below: saturation current and emission coefficient times the thermal voltage. */
struct Diode
{
    double is = 2.52e-9, nVt = 1.752 * 0.02585; // 1N4148
};
inline constexpr Diode silicon1N4148 {};
inline constexpr Diode germanium { 2.0e-7, 1.3 * 0.02585 }; // a typical small-signal Ge (AA119 / 1N34A class)

/**
    The limiter in the feedback of an inverting op-amp stage that so many flangers put in front of the BBD to keep the
    regeneration loop from running away: feedback resistor Rf in parallel with (Rs in series with an anti-parallel
    diode pair). Given the current arriving at the virtual ground (input voltages / their resistors, summed), returns
    the stage's output voltage (negated, as the stage inverts). Solved exactly per sample: Newton on the diode voltage.
*/
class FeedbackLimiter
{
public:
    FeedbackLimiter (double feedbackOhms, double seriesOhms, Diode d) : rf (feedbackOhms), rs (seriesOhms), diode (d) {}

    double process (double inputCurrent) noexcept
    {
        // Unknown: vd (diode-pair voltage). Branch current ib = 2 Is sinh(vd / nVt); |vout| = vd + rs ib.
        // KCL at the virtual ground: vout/rf + ib = iin.
        const double target = inputCurrent;
        double vd = std::clamp (lastVd, -1.5, 1.5);
        for (int k = 0; k < 8; ++k)
        {
            const double e = std::exp (std::clamp (vd / diode.nVt, -60.0, 60.0));
            const double ib = diode.is * (e - 1.0 / e);
            const double dib = diode.is * (e + 1.0 / e) / diode.nVt;
            const double v = vd + rs * ib;
            const double f = v / rf + ib - target;
            const double df = (1.0 + rs * dib) / rf + dib;
            const double step = f / df;
            vd -= std::clamp (step, -0.2, 0.2);
            if (std::abs (step) < 1.0e-9)
                break;
        }
        lastVd = vd;
        const double e = std::exp (std::clamp (vd / diode.nVt, -60.0, 60.0));
        return -(vd + rs * diode.is * (e - 1.0 / e));
    }

    void reset() noexcept { lastVd = 0.0; }

private:
    double rf, rs;
    Diode diode;
    double lastVd = 0.0;
};

/** A source behind Rs feeding an anti-parallel diode pair to ground (a shunt clipper). Returns the clipped node voltage. */
class ShuntClipper
{
public:
    ShuntClipper (double seriesOhms, Diode d) : rs (seriesOhms), diode (d) {}

    double process (double source) noexcept
    {
        double vd = std::clamp (lastVd, -1.5, 1.5);
        for (int k = 0; k < 8; ++k)
        {
            const double e = std::exp (std::clamp (vd / diode.nVt, -60.0, 60.0));
            const double f = (vd - source) / rs + diode.is * (e - 1.0 / e);
            const double df = 1.0 / rs + diode.is * (e + 1.0 / e) / diode.nVt;
            const double step = f / df;
            vd -= std::clamp (step, -0.2, 0.2);
            if (std::abs (step) < 1.0e-9)
                break;
        }
        lastVd = vd;
        return vd;
    }

    void reset() noexcept { lastVd = 0.0; }

private:
    double rs;
    Diode diode;
    double lastVd = 0.0;
};

} // namespace openguitarmultifx::diodes
