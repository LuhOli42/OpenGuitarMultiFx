#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx::dyn
{

inline double toDb (double linear) noexcept { return 20.0 * std::log10 (std::max (linear, 1.0e-9)); }
inline double fromDb (double db) noexcept { return std::pow (10.0, db / 20.0); }

/** The static curve of a compressor as gain reduction in dB (>= 0) for an input level: hard knee when kneeDb is 0, otherwise the quadratic
    soft knee of Giannoulis, Massberg and Reiss (JAES 2012), centred on the threshold. */
inline double staticReductionDb (double levelDb, double thresholdDb, double ratio, double kneeDb) noexcept
{
    const double over = levelDb - thresholdDb;
    const double slope = 1.0 - 1.0 / std::max (1.0, ratio);
    if (kneeDb <= 0.0)
        return over > 0.0 ? over * slope : 0.0;
    if (2.0 * over < -kneeDb)
        return 0.0;
    if (2.0 * std::abs (over) <= kneeDb)
    {
        const double t = over + 0.5 * kneeDb;
        return slope * t * t / (2.0 * kneeDb);
    }
    return over * slope;
}

/** Coefficient of a one-pole smoother with time constant `seconds` at `sampleRate`: y += (x - y) * coefficient. */
inline double smoothingCoefficient (double seconds, double sampleRate) noexcept
{
    return seconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (seconds * sampleRate));
}

} // namespace openguitarmultifx::dyn
