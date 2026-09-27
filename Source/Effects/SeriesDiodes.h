#pragma once

#include <cmath>
#include <initializer_list>

namespace openguitarmultifx
{

/** One diode's Shockley parameters: i = Is (exp (v / nVt) - 1). */
struct DiodeModel
{
    double Is;
    double nVt; // emission coefficient times the thermal voltage
};

/**
    N junctions in series behave as ONE diode when nothing else touches the nodes between them (the same current flows through
    every one): v = sum_k nVt_k ln (i / Is_k) = nVt_eq ln (i / Is_eq) with nVt_eq = sum nVt_k and
    ln Is_eq = (sum nVt_k ln Is_k) / nVt_eq. Exact for i >> Is (the "-1" of each junction is dropped: a pA of leakage), which
    holds from a few nA up. One device instead of N, and the middle nodes (which a Newton solver has trouble with while every
    junction is reverse-biased) disappear. Two antiparallel chains then merge into the solver's single-port clipper.
    Used by the Zendrive's BAT41 + 1N34A + 2N7000 body-diode chain (docs/circuits/ZendriveStyleOverdrive.md); checked against
    the explicit netlist in ZendriveStyleOverdriveProcessorTests.
*/
inline DiodeModel seriesDiodes (std::initializer_list<DiodeModel> chain) noexcept
{
    double sumNVt = 0.0, sumNVtLnIs = 0.0;
    for (const auto& d : chain)
    {
        sumNVt += d.nVt;
        sumNVtLnIs += d.nVt * std::log (d.Is);
    }
    return { std::exp (sumNVtLnIs / sumNVt), sumNVt };
}

} // namespace openguitarmultifx
