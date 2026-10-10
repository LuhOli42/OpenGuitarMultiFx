#pragma once

#include "SignalGraph.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Preset spillover (docs/PresetsAndSnapshots.md, stage 5): when a preset
    switch rebuilds the chain, the OUTGOING preset's trail-capable
    processors (delays, reverbs -- EffectProcessor::hasTrails()) keep
    processing on silence in parallel with the new chain until their tails
    fall under the same -100 dBFS / 0.5 s convention a bypassed trails
    block already uses, instead of being cut dead with the old chain.

    Thread roles mirror DeferredReclaimer's: the control thread builds and
    owns Units (add()/sweep()/clear()), the audio thread only ever reaches
    them through process() -- a fixed array of raw pointers into the
    control thread's storage plus one atomic hand-off slot. `done` is the
    reachability handshake: it is only ever set at the moment a Unit
    becomes unreachable to the audio thread (retired inside process(),
    displaced while still pending, or reset() with the stream stopped),
    which is exactly what makes the control thread's sweep() safe to
    delete it.
*/
class Spillover
{
public:
    /** One outgoing chain's ringing tails. Built and destroyed on the
        control thread; the audio thread only calls graph->process() into
        `scratch` and writes the plain bookkeeping fields. */
    struct Unit
    {
        /** The trails processors in their old lanes, on the old
            lane-to-lane routing, fed silence -- never the live input,
            which the new preset now owns (the "instant-apply" rule). */
        std::unique_ptr<SignalGraph> graph;
        /** Keeps the graph's processors alive for the tail's whole life --
            they outlive the ~600 ms graveyard by seconds. */
        std::vector<std::unique_ptr<EffectProcessor>> owners;
        /** Process-on-silence scratch, sized once on the control thread --
            the audio thread wraps a non-owning view of it, never resizes. */
        juce::AudioBuffer<float> scratch;
        /** Quiet-output window before this unit is done: trailsSilenceSeconds
            + the retained processors' summed trailLatencySeconds(), so a
            long delay's pending repeat -- including one still sitting in
            the last of serial delays -- can't be mistaken for a dead tail.
            Computed in build() on the control thread. */
        double silenceWindowSeconds = EffectProcessor::trailsSilenceSeconds;

        /** Control thread. prepareIfNeeded() keeps every processor's tail
            state (same sample rate/block size/channels = no-op), and the
            scratch is sized here, where allocating is allowed. */
        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            graph->prepare (sampleRate, maxBlockSize, numChannels);
            scratch.setSize (numChannels, maxBlockSize, false, true, true);
        }

        std::atomic<bool> done { false }; // see the class doc -- the "unreachable" handshake
        bool finished = false;            // audio thread only
        int silentSamples = 0;            // audio thread only
        juce::int64 elapsedSamples = 0;   // audio thread only

        /** Audio thread: the tail's output stayed under
            trailsSilenceThreshold for a whole silence window, or the hard
            cap was hit -- the unit can stop processing. */
        void updateFinished (float tailMagnitude, int numSamples, double sampleRate) noexcept
        {
            elapsedSamples += numSamples;
            silentSamples = (tailMagnitude < EffectProcessor::trailsSilenceThreshold)
                            ? silentSamples + numSamples
                            : 0;
            if (silentSamples >= (int) (silenceWindowSeconds * sampleRate)
                || elapsedSamples >= (juce::int64) (Spillover::maxSpillSeconds * sampleRate))
                finished = true;
        }
    };

    /** The processors of an outgoing chain for one lane, in chain order. */
    struct LaneSpec
    {
        std::vector<EffectProcessor*> processors;
        bool writesDeviceOutput = false;
    };

    /** Control thread. Builds the unit for an outgoing preset: keeps the
        hasTrails() processors in their old lanes (their unique_ptrs move
        out of `retired` into the unit; a non-trail block stays in `retired`
        and dies with the switch, per the spec), reconnecting every old
        lane->lane edge so a tail still flows through an emptied lane into
        a downstream trails block. Every lane's writesDeviceOutput flag is
        copied too -- including an emptied one -- or the passthrough
        contract leaves the buffer untouched and an upstream tail never
        reaches the device. Returns nullptr when nothing on the outgoing
        chain has trails -- then there is simply nothing to spill. */
    static std::unique_ptr<Unit> build (
        const std::array<LaneSpec, SignalGraph::numLanes>& lanes,
        const std::vector<std::pair<int, int>>& connections,
        std::vector<std::unique_ptr<EffectProcessor>>& retired);

    /** CPU bound: how many outgoing chains may ring at once (switching
        A -> B -> C fast enough can overlap two). Exceeding it cuts the
        OLDEST -- it has been ringing longest -- deterministically. */
    static constexpr int maxActiveUnits = 2;
    /** A 100%-feedback delay never goes silent on its own; a hard cap
        keeps the CPU bound finite no matter what the tail does. */
    static constexpr double maxSpillSeconds = 30.0;

    /** Control thread. Takes ownership and publishes the unit to the audio
        thread's hand-off slot. If an earlier unit is still pending (the
        audio thread has not run since), the newest wins: the displaced
        unit is marked done and freed by sweep(), never heard. */
    void add (std::unique_ptr<Unit> unit);

    /** Control thread, called periodically (AudioEngine's 100 ms timer --
        the same cadence DeferredReclaimer's sweep already runs at). Frees
        every unit the audio thread has finished with. */
    void sweep();

    /** Audio thread. Sums every ringing tail into `buffer`, on top of the
        new chain's output. Never allocates, never blocks: the pending
        pick-up is a single atomic exchange, `active` is a fixed array, and
        `scratch` is pre-sized. */
    void process (juce::AudioBuffer<float>& buffer, double sampleRate) noexcept;

    /** Called where the audio callback is provably not running (device
        (re)start): cuts every spill -- they were prepared for the old
        stream's rate/block size anyway. */
    void reset() noexcept;

    /** Control thread with the audio callback stopped (engine teardown):
        drops everything immediately. */
    void clear();

    /** Control thread; exists for the unit tests to verify retirements
        were actually freed by sweep(). */
    int numUnitsForTesting() const { return (int) units.size(); }

private:
    std::vector<std::unique_ptr<Unit>> units;      // control thread only
    std::atomic<Unit*> pending { nullptr };        // control -> audio hand-off slot
    std::array<Unit*, maxActiveUnits> active {};   // audio thread only
    int activeCount = 0;                           // audio thread only
};

} // namespace openguitarmultifx
