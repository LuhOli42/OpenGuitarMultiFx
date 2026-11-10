#include "OutputMatrix.h"

namespace openguitarmultifx
{

bool OutputMatrix::addRoute (int sourceLane, int outputPairStart) noexcept
{
    if (sourceLane < 0 || sourceLane >= SignalGraph::numLanes
        || outputPairStart < 0 || (outputPairStart % 2) != 0
        || routeCount >= maxRoutes)
        return false;

    routes[(size_t) routeCount++] = { sourceLane, outputPairStart };
    return true;
}

void OutputMatrix::render (juce::AudioBuffer<float>& deviceBuffer, const SignalGraph& graph) const noexcept
{
    deviceBuffer.clear();

    const int numSamples = deviceBuffer.getNumSamples();
    const int numDeviceChannels = deviceBuffer.getNumChannels();
    if (numSamples <= 0 || numDeviceChannels <= 0)
        return;

    for (int r = 0; r < routeCount; ++r)
    {
        const auto& route = routes[(size_t) r];
        const auto* laneBuffer = graph.getLaneBuffer (route.sourceLane);
        if (laneBuffer == nullptr)
            continue;

        const int laneChannels = laneBuffer->getNumChannels();
        const int samples = juce::jmin (numSamples, laneBuffer->getNumSamples());
        if (laneChannels <= 0 || samples <= 0)
            continue;

        for (int offset = 0; offset < 2; ++offset)
        {
            const int dstChannel = route.outputPairStart + offset;
            if (dstChannel >= numDeviceChannels)
                break;

            // A stereo lane maps L->pair L, R->pair R; a mono lane duplicates
            // onto both pair channels, matching the old spreadInto() behaviour.
            const int srcChannel = offset % laneChannels;
            deviceBuffer.addFrom (dstChannel, 0, *laneBuffer, srcChannel, 0, samples);
        }
    }
}

} // namespace openguitarmultifx
