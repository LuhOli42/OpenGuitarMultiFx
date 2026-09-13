#pragma once

#include "../Effects/EffectProcessor.h"

#include <array>
#include <utility>
#include <vector>

namespace openguitarmultifx
{

/**
    Four parallel lanes (matching MainComponent::numRows), each a serial
    chain of EffectProcessor instances, connected by explicit lane-to-lane
    edges -- a lane's processed output can feed several other lanes at
    once (a split) and a lane can receive from several other lanes at
    once, summed (a merge), replacing the old "flatten every branch into
    one serial chain" approximation (see MainComponent::rebuildSignalGraph()'s
    former comment). SignalGraph does NOT own the processors, same as
    before -- see the ownership note that used to live here, still true.

    addLaneConnection() REQUIRES sourceLane < targetLane. This isn't a
    general-purpose graph: the UI only ever lets a row feed a LATER row
    (MainComponent's "forward-only" rule, see Source/UI/AGENTS.md), which
    means processing lanes 0,1,2,3 in that fixed order is *always* a valid
    topological order -- no cycle detection or generic topo-sort needed,
    and no cycles are constructible through this API in the first place.

    Built entirely on the control thread (the setX/addX calls + prepare())
    and only then published into AudioEngine via DeferredReclaimer -- same
    "immutable once published" contract as before.
*/
class SignalGraph
{
public:
    static constexpr int numLanes = 4;

    /** One lane's serial sub-chain, in processing order. Replaces
        whatever was set for this lane before. Control thread, before
        prepare(). */
    void setLaneProcessors (int lane, std::vector<EffectProcessor*> processorsForLane);

    /** Whether this lane's input starts from the live device input
        (in addition to whatever addLaneConnection() feeds it, if
        anything -- both can be true at once, they just sum). */
    void setLaneReadsDeviceInput (int lane, bool readsInput);

    /** Whether this lane's processed output is mixed into the buffer
        process() hands back to the device. */
    void setLaneWritesDeviceOutput (int lane, bool writesOutput);

    /** sourceLane's processed output is summed into targetLane's input.
        Call once per edge: calling it for the same source with different
        targets is a split, calling it for the same target from different
        sources is a merge -- both are just "add another edge", not a
        special case. Requires sourceLane < targetLane (see class doc). */
    void addLaneConnection (int sourceLane, int targetLane);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** `buffer` holds the live device input on entry. If at least one lane
        writes to the device output, `buffer` is overwritten with the sum
        of those lanes' output; if NONE do (nothing configured at all, or
        every configured lane only feeds other lanes), `buffer` is left
        exactly as it was on entry -- the same "empty graph is a total
        passthrough" contract AudioEngine has always relied on. */
    void process (juce::AudioBuffer<float>& buffer);

    void reset();

private:
    struct Lane
    {
        std::vector<EffectProcessor*> processors;
        bool readsDeviceInput = false;
        bool writesDeviceOutput = false;
        juce::AudioBuffer<float> buffer;
    };

    std::array<Lane, numLanes> lanes;
    std::vector<std::pair<int, int>> connections;
    juce::AudioBuffer<float> deviceInputSnapshot;
    int preparedNumChannels = 0;
};

} // namespace openguitarmultifx
