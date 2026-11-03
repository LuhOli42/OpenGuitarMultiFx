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
        beginTest ("each channel keeps its own knobs when switching (Dual Rectifier, Orange / Red)");
        {
            DualRectifierStyleAmplifierProcessor amp;
            auto* channel = find (amp, "drec_channel");
            auto* gain = find (amp, "drec_gain");
            auto* treble = find (amp, "drec_treble");
            auto* presence = find (amp, "drec_presence");
            auto* output = find (amp, "drec_output");
            expect (channel != nullptr && gain != nullptr && treble != nullptr && presence != nullptr && output != nullptr);

            *channel = 1.0f;                 // Red
            *gain = 0.9f;
            *treble = 0.7f;
            *channel = 0.0f;                 // Orange: starts from its own defaults
            expectWithinAbsoluteError (gain->get(), 0.5f, 1.0e-6f);
            *gain = 0.2f;
            *presence = 0.8f;
            *output = 0.33f;                 // global, not per channel
            *channel = 1.0f;                 // back to Red
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
            *find (a, "drec_channel") = 1.0f;
            *find (a, "drec_gain") = 0.85f;
            *find (a, "drec_mid") = 0.25f;
            const auto xml = a.getState();

            b.setState (*xml);
            expectWithinAbsoluteError (find (b, "drec_channel")->get(), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (find (b, "drec_gain")->get(), 0.85f, 1.0e-6f);
            expectWithinAbsoluteError (find (b, "drec_mid")->get(), 0.25f, 1.0e-6f);
            *find (b, "drec_channel") = 0.0f;
            expectWithinAbsoluteError (find (b, "drec_gain")->get(), 0.15f, 1.0e-6f);
        }

        beginTest ("a 3-channel Dual Rectifier preset (Clean / Orange / Red) migrates to Orange+Clean / Orange / Red");
        {
            juce::XmlElement old ("EffectState");
            old.setAttribute ("drec_channel", 0.0);           // old Clean
            old.setAttribute ("drec_gain", 0.11);
            old.setAttribute ("drec_gain_ch0", 0.11);
            old.setAttribute ("drec_gain_ch1", 0.44);
            old.setAttribute ("drec_gain_ch2", 0.77);
            DualRectifierStyleAmplifierProcessor amp;
            amp.setState (old);
            expectWithinAbsoluteError (find (amp, "drec_channel")->get(), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (find (amp, "drec_orange_gain")->get(), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (find (amp, "drec_gain")->get(), 0.11f, 1.0e-6f);
            *find (amp, "drec_channel") = 1.0f;
            expectWithinAbsoluteError (find (amp, "drec_gain")->get(), 0.77f, 1.0e-6f);

            old.setAttribute ("drec_channel", 2.0);           // old Red
            DualRectifierStyleAmplifierProcessor red;
            red.setState (old);
            expectWithinAbsoluteError (find (red, "drec_channel")->get(), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (find (red, "drec_gain")->get(), 0.77f, 1.0e-6f);
            *find (red, "drec_channel") = 0.0f;
            expectWithinAbsoluteError (find (red, "drec_gain")->get(), 0.44f, 1.0e-6f); // old Orange row
            expectWithinAbsoluteError (find (red, "drec_orange_gain")->get(), 1.0f, 1.0e-6f);
        }

        beginTest ("Powerball: the real panel's sharing (Gain / Bass / Middle per main channel, Treble Clean / Crunch / Lead, Volume per mode)");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            auto* channel = find (amp, "engl_pb_channel");
            auto* gain = find (amp, "engl_pb_gain");
            auto* bass = find (amp, "engl_pb_bass");
            auto* treble = find (amp, "engl_pb_treble");
            auto* volume = find (amp, "engl_pb_master");
            *channel = 0.0f; *gain = 0.1f; *bass = 0.9f; *treble = 0.2f; *volume = 0.3f; // Clean
            *channel = 1.0f;                                 // Crunch: Ch I's Gain and Bass, its own Treble and Volume
            expectWithinAbsoluteError (gain->get(), 0.1f, 1.0e-6f);
            expectWithinAbsoluteError (bass->get(), 0.9f, 1.0e-6f);
            expectWithinAbsoluteError (treble->get(), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (volume->get(), 0.5f, 1.0e-6f);
            *treble = 0.7f; *volume = 0.8f;
            *channel = 2.0f;                                 // Lo Lead: Ch II's own Gain / Bass / Treble
            expectWithinAbsoluteError (gain->get(), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (bass->get(), 0.5f, 1.0e-6f);
            *gain = 0.6f; *treble = 0.4f; *volume = 0.25f;
            *channel = 3.0f;                                 // Hi Lead shares Lo Lead's Gain and Treble, not its Volume
            expectWithinAbsoluteError (gain->get(), 0.6f, 1.0e-6f);
            expectWithinAbsoluteError (treble->get(), 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (volume->get(), 0.5f, 1.0e-6f);
            *channel = 0.0f;
            expectWithinAbsoluteError (treble->get(), 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (volume->get(), 0.3f, 1.0e-6f);
        }

        beginTest ("a preset saved before per-channel memory loads its knobs into every channel");
        {
            DualRectifierStyleAmplifierProcessor amp;
            juce::XmlElement old ("EffectState");
            old.setAttribute ("drec_channel", 1.0);
            old.setAttribute ("drec_style", 0.0);
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
