#pragma once

#include <atomic>

namespace openguitarmultifx::thermal
{

/** True while the board is hot (>= 95 C, back to normal below 91 C). The die has one heat budget shared by the GPU, the slow
    cores (UI) and the fast cores (audio); the audio must keep its share, and the only way to lower the GPU's is to draw less:
    animations become instant and the steadily running ones (footer meters, tuner) draw every other frame. */
inline std::atomic<bool>& economy() noexcept
{
    static std::atomic<bool> value { false };
    return value;
}

inline void update (float celsius) noexcept
{
    if (celsius >= 95.0f)
        economy().store (true, std::memory_order_relaxed);
    else if (celsius > 0.0f && celsius <= 91.0f)
        economy().store (false, std::memory_order_relaxed);
}

} // namespace openguitarmultifx::thermal
