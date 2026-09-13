#include "Presets/PresetManager.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class PresetManagerTests : public juce::UnitTest
{
public:
    PresetManagerTests() : juce::UnitTest ("PresetManager", "Presets") {}

    void runTest() override
    {
        // A fresh temp directory per test run -- real file I/O, cleaned up
        // at the end, not a fixture checked into the repo (unlike
        // NAMProcessorTests' real .nam file -- there's nothing to load a
        // preset FROM until this test itself creates one).
        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("OGMFxPresetManagerTests_" + juce::String (juce::Random::getSystemRandom().nextInt64()));

        beginTest ("an empty/nonexistent directory reports no presets, not an error");
        {
            PresetManager presets (tempDir);
            expect (presets.listPresetNames().isEmpty());
            expect (presets.nameForNumber (1).isEmpty());
            expectEquals (presets.nextAvailableNumber(), 1);
        }

        beginTest ("save then load round-trips the same XML content");
        {
            PresetManager presets (tempDir);

            juce::XmlElement xml ("Preset");
            xml.setAttribute ("number", 3);
            xml.setAttribute ("someField", "hello");

            expect (presets.savePreset ("My Preset", xml));

            auto loaded = presets.loadPreset ("My Preset");
            expect (loaded != nullptr);
            expectEquals (loaded->getIntAttribute ("number", -1), 3);
            expectEquals (loaded->getStringAttribute ("someField"), juce::String ("hello"));
        }

        beginTest ("nameForNumber finds the preset saved under that number, and nothing for an unused one");
        {
            PresetManager presets (tempDir);

            juce::XmlElement first ("Preset");
            first.setAttribute ("number", 5);
            presets.savePreset ("Lead Tone", first);

            juce::XmlElement second ("Preset");
            second.setAttribute ("number", 12);
            presets.savePreset ("Clean Tone", second);

            expectEquals (presets.nameForNumber (5), juce::String ("Lead Tone"));
            expectEquals (presets.nameForNumber (12), juce::String ("Clean Tone"));
            expect (presets.nameForNumber (99).isEmpty());
        }

        beginTest ("numberForExistingPreset/nextAvailableNumber track the highest saved number");
        {
            PresetManager presets (tempDir);
            expectEquals (presets.numberForExistingPreset ("Lead Tone"), 5);
            expectEquals (presets.numberForExistingPreset ("Clean Tone"), 12);
            expectEquals (presets.numberForExistingPreset ("Nonexistent"), 0);
            expectEquals (presets.nextAvailableNumber(), 13);
        }

        beginTest ("deletePreset removes it from both listing and lookup");
        {
            PresetManager presets (tempDir);
            expect (presets.deletePreset ("Clean Tone"));
            expect (! presets.listPresetNames().contains ("Clean Tone"));
            expect (presets.nameForNumber (12).isEmpty());
        }

        tempDir.deleteRecursively();
    }
};

static PresetManagerTests presetManagerTests;

} // namespace openguitarmultifx
