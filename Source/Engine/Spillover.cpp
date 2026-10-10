#include "Spillover.h"

#include <algorithm>

namespace openguitarmultifx
{

std::unique_ptr<Spillover::Unit> Spillover::build (
    const std::array<LaneSpec, SignalGraph::numLanes>& lanes,
    const std::vector<std::pair<int, int>>& connections,
    std::vector<std::unique_ptr<EffectProcessor>>& retired)
{
    auto graph = std::make_unique<SignalGraph>();
    std::vector<std::unique_ptr<EffectProcessor>> owners;
    bool anyTrails = false;

    for (int lane = 0; lane < SignalGraph::numLanes; ++lane)
    {
        std::vector<EffectProcessor*> kept;
        for (auto* p : lanes[(size_t) lane].processors)
        {
            if (! p->hasTrails())
                continue; // a non-trail block's output dies with the switch

            kept.push_back (p);
            for (auto& slot : retired)
                if (slot.get() == p)
                {
                    owners.push_back (std::move (slot));
                    break;
                }
            anyTrails = true;
        }

        if (! kept.empty())
        {
            graph->setLaneProcessors (lane, std::move (kept));
            graph->setLaneWritesDeviceOutput (lane, lanes[(size_t) lane].writesDeviceOutput);
        }
    }

    if (! anyTrails)
        return nullptr;

    for (const auto& connection : connections)
        if (connection.first >= 0 && connection.first < connection.second
            && connection.second < SignalGraph::numLanes)
            graph->addLaneConnection (connection.first, connection.second);

    auto unit = std::make_unique<Unit>();
    unit->graph = std::move (graph);
    unit->owners = std::move (owners);
    return unit;
}

void Spillover::add (std::unique_ptr<Unit> unit)
{
    if (unit == nullptr)
        return;

    auto* raw = unit.get();
    units.push_back (std::move (unit));

    if (auto* displaced = pending.exchange (raw, std::memory_order_acq_rel))
        displaced->done.store (true, std::memory_order_release); // unreachable as of this exchange
}

void Spillover::sweep()
{
    units.erase (std::remove_if (units.begin(), units.end(),
                                 [] (const std::unique_ptr<Unit>& u)
                                 { return u->done.load (std::memory_order_acquire); }),
                 units.end());
}

void Spillover::process (juce::AudioBuffer<float>& buffer, double sampleRate) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (auto* unit = pending.exchange (nullptr, std::memory_order_acquire))
    {
        if (activeCount == maxActiveUnits)
        {
            // Cap: the oldest spill has been ringing longest -- cut it and
            // let the newest in. The dropped unit is freed by sweep().
            active[0]->done.store (true, std::memory_order_release);
            for (int i = 1; i < activeCount; ++i)
                active[(size_t) (i - 1)] = active[(size_t) i];
            --activeCount;
        }
        active[(size_t) activeCount++] = unit;
    }

    for (int i = 0; i < activeCount; ++i)
    {
        auto* unit = active[(size_t) i];
        const int n = juce::jmin (numSamples, unit->scratch.getNumSamples());
        const int ch = juce::jmin (numChannels, unit->scratch.getNumChannels());

        if (n <= 0 || ch <= 0 || unit->graph == nullptr)
        {
            unit->finished = true;
            continue;
        }

        // A non-owning view over the pre-sized scratch: the outgoing chain
        // is fed silence -- the live input belongs to the new preset.
        juce::AudioBuffer<float> tail (unit->scratch.getArrayOfWritePointers(), ch, n);
        tail.clear();
        unit->graph->process (tail);

        for (int c = 0; c < ch; ++c)
            buffer.addFrom (c, 0, tail, c, 0, n);

        unit->elapsedSamples += n;
        const bool silent = tail.getMagnitude (0, n) < EffectProcessor::trailsSilenceThreshold;
        unit->silentSamples = silent ? unit->silentSamples + n : 0;

        if (unit->silentSamples >= (int) (EffectProcessor::trailsSilenceSeconds * sampleRate)
            || unit->elapsedSamples >= (juce::int64) (maxSpillSeconds * sampleRate))
            unit->finished = true;
    }

    // Retire the finished. done is stored only when a unit drops out of
    // `active` (or out of `pending`) -- never while the audio thread can
    // still reach it -- which is the whole contract sweep() relies on.
    int kept = 0;
    for (int i = 0; i < activeCount; ++i)
    {
        auto* unit = active[(size_t) i];
        if (unit->finished)
            unit->done.store (true, std::memory_order_release);
        else
            active[(size_t) kept++] = unit;
    }
    activeCount = kept;
}

void Spillover::reset() noexcept
{
    for (int i = 0; i < activeCount; ++i)
        active[(size_t) i]->done.store (true, std::memory_order_release);
    activeCount = 0;

    if (auto* unit = pending.exchange (nullptr, std::memory_order_acq_rel))
        unit->done.store (true, std::memory_order_release);
}

void Spillover::clear()
{
    activeCount = 0;
    pending.store (nullptr, std::memory_order_release);
    units.clear();
}

} // namespace openguitarmultifx
