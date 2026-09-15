#pragma once

namespace openguitarmultifx
{

/**
    A Thevenin-equivalent branch (a resistance plus a source voltage) --
    the universal currency every circuit-modelled processor in this
    project passes around when reducing a node's surrounding passive
    network (fixed resistors, capacitor companion models, other already-
    reduced Thevenin branches) down to what a nonlinear device solver
    (EbersMollBJT, ShichmanHodgesJFET, AsymmetricDiodePair) or a closed-
    form linear stage actually needs. Extracted here (rather than
    duplicated per processor, as the first two circuit-modelled
    distortion pedals originally did) once a second processor needed the
    exact same combine logic -- see Source/Effects/AGENTS.md's decision
    log.
*/
struct Thevenin
{
    double rth;
    double vth;
};

/** Combines two independent Thevenin-equivalent branches feeding the same
    node in parallel -- the standard conductance-weighted average. */
inline Thevenin combineParallel (const Thevenin& a, const Thevenin& b) noexcept
{
    const double ga = 1.0 / a.rth;
    const double gb = 1.0 / b.rth;
    const double g = ga + gb;
    return { 1.0 / g, (a.vth * ga + b.vth * gb) / g };
}

inline Thevenin combineParallel (const Thevenin& a, const Thevenin& b, const Thevenin& c) noexcept
{
    return combineParallel (combineParallel (a, b), c);
}

} // namespace openguitarmultifx
