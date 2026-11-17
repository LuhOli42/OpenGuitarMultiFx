#pragma once

#include <juce_core/juce_core.h>

#if JUCE_LINUX
 #include <sched.h>
#endif

namespace openguitarmultifx::cpuaffinity
{

/**
    big.LITTLE split for the device (Cubie A7S: 2 fast Cortex-A76 + 6 slow A55): the audio callback and its lane workers
    stay on the fast cores, everything else -- UI/message thread (drawing, the touchscreen's events, the tuner's pitch
    analysis) -- runs on the slow ones, so a screen update or a finger on the glass can never take the audio thread's core.

    Driven by two CPU lists, so a desktop build is untouched (no env, no effect):
      OGMFX_AUDIO_CPUS="6-7"   the audio callback and the parallel-lane workers
      OGMFX_UI_CPUS="0-5"      the main/message thread (and the threads it creates after that)
    Call init() once, on the main thread, before any audio device is opened.
*/
#if JUCE_LINUX
struct Masks
{
    cpu_set_t audio {}, ui {};
    bool hasAudio = false, hasUi = false;
    int audioCpus[CPU_SETSIZE] {}; // the audio CPU ids in ascending order, for one-core-per-thread pinning
    int audioCount = 0;
};

inline Masks& masks()
{
    static Masks m;
    return m;
}

inline bool parse (const juce::String& list, cpu_set_t& out)
{
    CPU_ZERO (&out);
    bool any = false;
    for (const auto& part : juce::StringArray::fromTokens (list, ",", ""))
    {
        const auto p = part.trim();
        const int dash = p.indexOfChar ('-');
        const int lo = p.substring (0, dash < 0 ? p.length() : dash).getIntValue();
        const int hi = dash < 0 ? lo : p.substring (dash + 1).getIntValue();
        for (int c = lo; c <= hi && c < CPU_SETSIZE; ++c)
        {
            CPU_SET (c, &out);
            any = true;
        }
    }
    return any;
}

inline void init()
{
    auto& m = masks();
    m.hasAudio = parse (juce::SystemStats::getEnvironmentVariable ("OGMFX_AUDIO_CPUS", {}), m.audio);
    m.audioCount = 0;
    for (int c = 0; c < CPU_SETSIZE && m.audioCount < CPU_SETSIZE; ++c)
        if (CPU_ISSET (c, &m.audio))
            m.audioCpus[m.audioCount++] = c;
    m.hasUi = parse (juce::SystemStats::getEnvironmentVariable ("OGMFX_UI_CPUS", {}), m.ui);
    if (m.hasUi)
        sched_setaffinity (0, sizeof (cpu_set_t), &m.ui); // this thread, and every thread it creates from now on
}

/** Pins the CALLING thread to ONE audio core: slot 0 is the first fast core, slot 1 the second, and so on (wrapping when there
    are more threads than cores). The audio callback is slot 0 and each parallel worker gets the next slot, so parallelised
    effects always run on different cores instead of leaving it to the scheduler (which may stack two on one core). */
inline void pinThisThreadToAudioSlot (int slot) noexcept
{
    const auto& m = masks();
    if (! m.hasAudio || m.audioCount <= 0)
        return;
    cpu_set_t one;
    CPU_ZERO (&one);
    CPU_SET (m.audioCpus[(slot % m.audioCount + m.audioCount) % m.audioCount], &one);
    sched_setaffinity (0, sizeof (cpu_set_t), &one);
}

/** Pins the CALLING thread to the audio CPUs. One syscall, no allocation: safe from the first audio callback. */
inline void pinThisThreadToAudio() noexcept
{
    const auto& m = masks();
    if (m.hasAudio)
        sched_setaffinity (0, sizeof (cpu_set_t), &m.audio);
}
#else
inline void init() {}
inline void pinThisThreadToAudio() noexcept {}
inline void pinThisThreadToAudioSlot (int) noexcept {}
#endif

} // namespace openguitarmultifx::cpuaffinity
