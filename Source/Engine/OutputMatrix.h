#pragma once

#include "SignalGraph.h"

#include <array>

namespace openguitarmultifx
{

/**
    Immutable lane -> physical output-pair routing table, published to the
    audio thread through DeferredReclaimer (see AudioEngine). Phase 1 keeps
    the current RowRouting model: at most one pair per lane, gain 1, no pan
    or channel map -- but the table is already a list of routes so later
    phases can add multiple destinations and per-route DSP without changing
    the realtime contract.

    render() is the only audio-thread entry point: it clears every device
    channel, then sums each route's lane buffer into the two channels of its
    output pair. Channels with no route stay silent; several routes to the
    same pair sum like a merge.
*/
class OutputMatrix
{
public:
    static constexpr int maxRoutes = 32;

    struct Route
    {
        int sourceLane = 0;
        int outputPairStart = 0; // first channel of the pair: 0, 2, 4, ...
    };

    /** Control thread, before publication. Returns false for an invalid
        lane or pair start; the route is ignored rather than clamped here so
        a stale preset can't silently retarget audio. */
    bool addRoute (int sourceLane, int outputPairStart) noexcept;

    bool hasRoutes() const noexcept { return routeCount > 0; }
    int numRoutes() const noexcept { return routeCount; }

    /** Audio thread. `deviceBuffer` is the full multi-channel device output;
        `graph` must have just run processLanes() on the same block. */
    void render (juce::AudioBuffer<float>& deviceBuffer, const SignalGraph& graph) const noexcept;

private:
    std::array<Route, maxRoutes> routes {};
    int routeCount = 0;
};

} // namespace openguitarmultifx
