#include "Effects/DualRectifierStyleAmplifierProcessor.h"
#include "Effects/ENGLPowerballStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class ChannelKnobMemoryTests : public juce::UnitTest
{
public:
    ChannelKnobMemoryTests() : juce::UnitTest ("ChannelKnobMemoryAmplifier", "Effects") {}

    static juce::AudioParameterFloat* find (EffectProcessor& p, const juce::String& id)
    {
        for (auto* param : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param))
                if (f->paramID == id)
                    return f;
        return nullptr;
    }

    void runTest() override
    {
        beginTest ("each channel keeps its own knobs when switching (Dual Rectifier, 3 channels)");
        {
            DualRectifierStyleAmplifierProcessor amp;
            auto* channel = find (amp, "drec_channel");
            auto* gain = find (amp, "drec_gain");
            auto* treble = find (amp, "drec_treble");
            auto* presence = find (amp, "drec_presence");
            auto* output = find (amp, "drec_output");
            expect (channel != nullptr && gain != nullptr && treble != nullptr && presence != nullptr && output != nullptr);

            *channel = 2.0f;                 // Red
            *gain = 0.9f;
            *treble = 0.7f;
            *channel = 0.0f;                 // Clean: starts from its own defaults
            expectWithinAbsoluteError (gain->get(), 0.5f, 1.0e-6f);
            *gain = 0.2f;
            *presence = 0.8f;
            *output = 0.33f;                 // global, not per channel
            *channel = 1.0f;                 // Orange
            *gain = 0.6f;
            *channel = 2.0f;                 // back to Red
            expectWithinAbsoluteError (gain->get(), 0.9f, 1.0e-6f);
            expectWithinAbsoluteError (treble->get(), 0.7f, 1.0e-6f);
            expectWithinAbsoluteError (presence->get(), 0.3f, 1.0e-6f);
            expectWithinAbsoluteError (output->get(), 0.33f, 1.0e-6f);
            *channel = 0.0f;
            expectWithinAbsoluteError (gain->get(), 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (presence->get(), 0.8f, 1.0e-6f);
        }

        beginTest ("a preset restores every channel's knobs, not just the visible one");
        {
            DualRectifierStyleAmplifierProcessor a, b;
            *find (a, "drec_channel") = 0.0f;
            *find (a, "drec_gain") = 0.15f;
            *find (a, "drec_channel") = 2.0f;
            *find (a, "drec_gain") = 0.85f;
            *find (a, "drec_mid") = 0.25f;
            const auto xml = a.getState();

            b.setState (*xml);
            expectWithinAbsoluteError (find (b, "drec_channel")->get(), 2.0f, 1.0e-6f);
            expectWithinAbsoluteError (find (b, "drec_gain")->get(), 0.85f, 1.0e-6f);
            expectWithinAbsoluteError (find (b, "drec_mid")->get(), 0.25f, 1.0e-6f);
            *find (b, "drec_channel") = 0.0f;
            expectWithinAbsoluteError (find (b, "drec_gain")->get(), 0.15f, 1.0e-6f);
        }

        beginTest ("Powerball: Gain/Volume per channel, EQ shared by Clean+Crunch and by the two Leads");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            auto* channel = find (amp, "engl_pb_channel");
            auto* gain = find (amp, "engl_pb_gain");
            auto* bass = find (amp, "engl_pb_bass");
            *channel = 0.0f; *gain = 0.1f; *bass = 0.9f;   // Clean
            *channel = 1.0f;                                 // Crunch: own Gain, same EQ as Clean
            expectWithinAbsoluteError (gain->get(), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (bass->get(), 0.9f, 1.0e-6f);
            *gain = 0.3f; *bass = 0.7f;
            *channel = 2.0f;                                 // Lead: its own EQ section
            expectWithinAbsoluteError (bass->get(), 0.5f, 1.0e-6f);
            *bass = 0.2f;
            *channel = 3.0f;                                 // Hi Lead shares Lead's EQ
            expectWithinAbsoluteError (bass->get(), 0.2f, 1.0e-6f);
            *channel = 0.0f;
            expectWithinAbsoluteError (gain->get(), 0.1f, 1.0e-6f);
            expectWithinAbsoluteError (bass->get(), 0.7f, 1.0e-6f); // Crunch's change is Clean's too
        }

        beginTest ("a preset saved before per-channel memory loads its knobs into every channel");
        {
            DualRectifierStyleAmplifierProcessor amp;
            juce::XmlElement old ("EffectState");
            old.setAttribute ("drec_channel", 2.0);
            old.setAttribute ("drec_gain", 0.72);
            amp.setState (old);
            expectWithinAbsoluteError (find (amp, "drec_gain")->get(), 0.72f, 1.0e-6f);
            *find (amp, "drec_channel") = 0.0f;
            expectWithinAbsoluteError (find (amp, "drec_gain")->get(), 0.72f, 1.0e-6f);
        }
    }
};

static ChannelKnobMemoryTests channelKnobMemoryTests;

} // namespace openguitarmultifx
