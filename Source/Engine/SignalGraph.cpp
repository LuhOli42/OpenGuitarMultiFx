#include "SignalGraph.h"

namespace openguitarmultifx
{

namespace
{
    /** Adds up to `dst`'s own channel count from `src`, whichever is
        fewer -- lane buffers and the device buffer handed to process()
        don't always have the same channel count (a lane is always
        `preparedNumChannels` wide; the device buffer can have more
        channels on a multi-output interface), and this is the one place
        that mismatch needs handling. */
    void addClamped (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src, int numSamples) noexcept
    {
        const int numChannels = juce::jmin (dst.getNumChannels(), src.getNumChannels());
        for (int ch = 0; ch < numChannels; ++ch)
            dst.addFrom (ch, 0, src, ch, 0, numSamples);
    }

    /** Same idea, but for writing a (possibly narrower) mix INTO a
        (possibly wider) device buffer -- every device channel gets SOME
        content rather than the extras being left silent, matching the
        pre-existing behaviour of duplicating a mono input across every
        output channel. */
    void spreadInto (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src, int numSamples) noexcept
    {
        if (src.getNumChannels() == 0)
            return;

        for (int ch = 0; ch < dst.getNumChannels(); ++ch)
            dst.copyFrom (ch, 0, src, ch % src.getNumChannels(), 0, numSamples);
    }
}

void SignalGraph::setLaneProcessors (int lane, std::vector<EffectProcessor*> processorsForLane)
{
    lanes[(size_t) lane].processors = std::move (processorsForLane);
}

void SignalGraph::setLaneReadsDeviceInput (int lane, bool readsInput)
{
    lanes[(size_t) lane].readsDeviceInput = readsInput;
}

void SignalGraph::setLaneWritesDeviceOutput (int lane, bool writesOutput)
{
    lanes[(size_t) lane].writesDeviceOutput = writesOutput;
}

void SignalGraph::addLaneConnection (int sourceLane, int targetLane)
{
    jassert (sourceLane < targetLane); // see the class doc comment -- forward-only by construction
    connections.emplace_back (sourceLane, targetLane);
}

void SignalGraph::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    preparedNumChannels = numChannels;
    deviceInputSnapshot.setSize (numChannels, maxBlockSize, false, true, true);

    for (auto& lane : lanes)
    {
        lane.buffer.setSize (numChannels, maxBlockSize, false, true, true);
        for (auto* p : lane.processors)
            p->prepare (sampleRate, maxBlockSize, numChannels);
    }
}

void SignalGraph::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    deviceInputSnapshot.clear();
    addClamped (deviceInputSnapshot, buffer, numSamples);

    bool anyLaneWritesOutput = false;

    // Lanes 0..3 in that fixed order is always a valid topological order
    // -- see the class doc comment on addLaneConnection() for why a
    // proper topo-sort is unnecessary here.
    for (int i = 0; i < numLanes; ++i)
    {
        auto& lane = lanes[(size_t) i];
        lane.buffer.clear();

        if (lane.readsDeviceInput)
            addClamped (lane.buffer, deviceInputSnapshot, numSamples);

        for (auto& connection : connections)
            if (connection.second == i)
                addClamped (lane.buffer, lanes[(size_t) connection.first].buffer, numSamples);

        for (auto* p : lane.processors)
            if (! p->isBypassed())
                p->process (lane.buffer);

        if (lane.writesDeviceOutput)
            anyLaneWritesOutput = true;
    }

    if (! anyLaneWritesOutput)
        return; // leave `buffer` exactly as it came in -- the passthrough contract

    // Reuse deviceInputSnapshot as the output-mix scratch buffer -- its
    // job as the input snapshot is already done for this block, and
    // allocating a second scratch buffer here would be pointless.
    deviceInputSnapshot.clear();
    for (int i = 0; i < numLanes; ++i)
        if (lanes[(size_t) i].writesDeviceOutput)
            addClamped (deviceInputSnapshot, lanes[(size_t) i].buffer, numSamples);

    spreadInto (buffer, deviceInputSnapshot, numSamples);
}

void SignalGraph::reset()
{
    for (auto& lane : lanes)
        for (auto* p : lane.processors)
            p->reset();
}

} // namespace openguitarmultifx
