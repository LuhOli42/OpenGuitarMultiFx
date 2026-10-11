#pragma once

#include <cmath>

namespace openguitarmultifx
{

/** The device boundary's full scale: the DAC hard-clips whatever crosses 1.0, so the last thing the
    output bus does is saturate into this soft limiter instead. Linear under ceilKnee, asymptotic to
    ceilCeiling (the audit's clip meter counts |x| > 0.98, so the ceiling sits under it -- 0.88, not
    0.93, because oversampling decimation rings a few percent past the bound on clipped edges). Also
    used by amp models (`tubeamp::outputCeiling`) for the same "cannot swing past the rails" rule. */
constexpr double ceilKnee = 0.6, ceilCeiling = 0.88;
inline double softCeiling (double x) noexcept
{
    const double a = std::abs (x);
    return a <= ceilKnee ? x
                         : std::copysign (ceilKnee + (ceilCeiling - ceilKnee) * std::tanh ((a - ceilKnee) / (ceilCeiling - ceilKnee)),
                                          x);
}

} // namespace openguitarmultifx
