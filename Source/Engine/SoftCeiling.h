#pragma once

#include <cmath>

namespace openguitarmultifx
{

/** Two-stage saturator contract. A model (or its trim wrapper) may soft-limit its own emit into the
    knee/ceiling it was voiced for (`modelSoftCeiling`). The device boundary then applies the same
    curve with its knee AT the model ceiling (`deviceSoftCeiling`): an already-limited signal passes
    through untouched, and only genuinely over-scale content -- a boosted last-in-chain pedal, the
    sum of parallel lanes, master gain pushed hot -- saturates instead of hard-clipping the DAC.
    The ceilings stay under the 0.98 the audit's clip meter counts (0.88/0.96, not ~0.93/0.99,
    because the oversampling wrapper's decimation rings a few percent past the bound on clipped
    edges). */
inline double softCeiling (double x, double knee, double ceiling) noexcept
{
    const double a = std::abs (x);
    return a <= knee ? x
                     : std::copysign (knee + (ceiling - knee) * std::tanh ((a - knee) / (ceiling - knee)),
                                      x);
}

constexpr double modelKnee = 0.6, modelCeiling = 0.88;
inline double modelSoftCeiling (double x) noexcept
{
    return softCeiling (x, modelKnee, modelCeiling);
}

constexpr double deviceKnee = modelCeiling, deviceCeiling = 0.96;
inline double deviceSoftCeiling (double x) noexcept
{
    return softCeiling (x, deviceKnee, deviceCeiling);
}

} // namespace openguitarmultifx
