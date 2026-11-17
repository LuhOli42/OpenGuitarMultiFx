#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace openguitarmultifx::thermal
{

/** True while the board is hot OR the GPU is saturated. The die has one heat budget shared by the GPU, the slow cores (UI) and the
    fast cores (audio); the audio must keep its share, and the only way to lower the GPU's is to draw less: animations become
    instant and the steadily running ones (footer meters, tuner) draw every other frame.
    On at >= 90 C, or when the GPU has been >= 80% busy for two samples in a row; off again at <= 86 C and a GPU under 50% for
    three samples. (Measured: a minute of popups and scrolling took the board from 88 C to 105 C with the GPU at 98%, and from
    there the thermal guard pulls every clock down -- the stutter. Backing off at 90 C avoids reaching it.) */
inline std::atomic<bool>& economy() noexcept
{
    static std::atomic<bool> value { false };
    return value;
}

/** The PowerVR driver's own utilisation figure (debugfs, root only: the app runs as root on the board); -1 when unavailable. */
inline int readGpuBusyPercent()
{
   #if JUCE_LINUX
    const auto text = juce::File ("/sys/kernel/debug/pvr/status").loadFileAsString();
    if (const int at = text.indexOf ("GPU Utilisation:"); at >= 0)
        return text.substring (at + 16).getIntValue();
   #endif
    return -1;
}

inline void update (float celsius, int gpuBusy) noexcept
{
    static int highGpuSamples = 0, lowGpuSamples = 0;
    highGpuSamples = gpuBusy >= 80 ? highGpuSamples + 1 : 0;
    lowGpuSamples = gpuBusy < 50 ? lowGpuSamples + 1 : 0;
    if (celsius >= 90.0f || highGpuSamples >= 2)
        economy().store (true, std::memory_order_relaxed);
    else if (celsius <= 86.0f && lowGpuSamples >= 3)
        economy().store (false, std::memory_order_relaxed);
}

} // namespace openguitarmultifx::thermal
