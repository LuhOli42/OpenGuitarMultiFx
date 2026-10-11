#include "EffectRegistry.h"
#include "PedalStress.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/** Filter-design frequencies must stay safely below Nyquist at every supported host rate: a voicing
    constant past ~fs/2 makes the cookbook biquads' poles leave the unit circle and the output grows
    without bound (Devin Review on the audit-regression fix: the 20-24 kHz corners added for
    EPStyleBooster's bandwidth guard and the Bassman/JTM45/AC15 cab+OT+presence tails diverged under a
    32 kHz WAV through RenderFx, cab voicing on AND off). These units are the ones whose registered
    voicings carry corners that high; runPedalStress throws random plucks + hot bursts at them, so any
    unstable section blows up inside the first blocks. */
class LowSampleRateStabilityTests : public juce::UnitTest
{
public:
    LowSampleRateStabilityTests() : juce::UnitTest ("LowSampleRateStability", "Effects") {}

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        const juce::StringArray keys { "EPStyleBooster", "BassmanStyleAmplifier", "JTM45StyleAmplifier", "AC15StyleAmplifier" };
        const std::vector<double> rates { 32000.0, 44100.0, 48000.0, 96000.0 };

        for (const auto& key : keys)
            for (double rate : rates)
            {
                beginTest (key + " stays finite and bounded at " + juce::String ((int) rate) + " Hz");
                auto fx = registry.create (key);
                fx->prepare (rate, 128, 2);
                expectEquals (runPedalStress (*fx, 2.0, (int) rate, rate), 0);
            }
    }
};

static LowSampleRateStabilityTests lowSampleRateStabilityTests;

} // namespace openguitarmultifx
