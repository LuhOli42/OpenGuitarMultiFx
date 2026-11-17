#pragma once

#include "../EffectRegistry.h"

#include <map>
#include <memory>
#include <mutex>

namespace openguitarmultifx
{

/** Instances built only to read an effect's icon, accent colour and cab capability (never processed). Owned by
    MainComponent, so each one is built ONCE -- in the background at start -- instead of at every open of the Add Effect
    browser, which used to construct a real model of every registered amp on the UI thread before its first frame. */
class EffectProbeStore
{
public:
    const EffectProcessor* get (const EffectRegistry& registry, const juce::String& key)
    {
        std::lock_guard<std::mutex> lock (mutex);
        auto& probe = probes[key];
        if (probe == nullptr)
            probe = registry.create (key);
        return probe.get(); // std::map nodes never move: the pointer stays valid while other keys are added
    }

    void warm (const EffectRegistry& registry)
    {
        for (auto& key : registry.getRegisteredNames())
            get (registry, key);
    }

private:
    std::mutex mutex;
    std::map<juce::String, std::unique_ptr<EffectProcessor>> probes;
};

} // namespace openguitarmultifx
