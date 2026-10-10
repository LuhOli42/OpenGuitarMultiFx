#include "UI/MidiLearnPresetXml.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"
#include "Effects/TremoloProcessor.h"

namespace openguitarmultifx
{

class MidiLearnPresetXmlTests : public juce::UnitTest
{
public:
    MidiLearnPresetXmlTests() : juce::UnitTest ("MidiLearnPresetXml", "Presets") {}

    void runTest() override
    {
        beginTest ("bindings round-trip onto freshly-created processors, page-2 parameters included");
        {
            TremoloProcessor tremolo;
            BassmanStyleAmplifierProcessor amp;
            auto* depth = tremolo.getParameterPages()[0].back();
            auto ampPages = amp.getParameterPages();
            expect (ampPages.size() > 1, "the Bassman should have a page-2 sub-group");
            auto* pageTwoParam = ampPages.back().front();

            juce::XmlElement preset ("Preset");
            midilearnxml::write (preset, { { 1, depth }, { 11, pageTwoParam } },
                                 { tremolo.getParameters(), amp.getParameters() });

            TremoloProcessor reloadedTremolo;
            BassmanStyleAmplifierProcessor reloadedAmp;
            auto bindings = midilearnxml::read (preset, { reloadedTremolo.getParameters(), reloadedAmp.getParameters() });

            expectEquals ((int) bindings.size(), 2);
            expect (bindings[1] != nullptr && reloadedTremolo.getParameters()->getParameters (true).contains (bindings[1]));
            expect (bindings[11] != nullptr && reloadedAmp.getParameters()->getParameters (true).contains (bindings[11]));
            expectEquals (bindings[1]->paramID, depth->paramID);
            expectEquals (bindings[11]->paramID, pageTwoParam->paramID);
        }

        beginTest ("no bindings writes nothing; a preset without <MidiLearn> reads as no bindings");
        {
            TremoloProcessor tremolo;
            juce::XmlElement preset ("Preset");
            midilearnxml::write (preset, {}, { tremolo.getParameters() });
            expect (preset.getChildByName ("MidiLearn") == nullptr);
            expect (midilearnxml::read (preset, { tremolo.getParameters() }).empty());
        }

        beginTest ("a binding to a parameter outside the saved blocks is not written");
        {
            TremoloProcessor saved, notInPreset;
            juce::XmlElement preset ("Preset");
            midilearnxml::write (preset, { { 7, notInPreset.getParameterPages()[0].front() } }, { saved.getParameters() });
            expect (preset.getChildByName ("MidiLearn") == nullptr);
        }

        beginTest ("a skipped block keeps later indices aligned, and stale entries are dropped instead of failing");
        {
            TremoloProcessor first, second;
            auto* rate = second.getParameterPages()[0].front();

            juce::XmlElement preset ("Preset");
            midilearnxml::write (preset, { { 4, rate } }, { first.getParameters(), second.getParameters() });
            auto* midiXml = preset.getChildByName ("MidiLearn");
            auto* unknownParam = midiXml->createNewChildElement ("Binding");
            unknownParam->setAttribute ("cc", 5);
            unknownParam->setAttribute ("block", 1);
            unknownParam->setAttribute ("param", "no_such_param");
            auto* missingBlock = midiXml->createNewChildElement ("Binding");
            missingBlock->setAttribute ("cc", 6);
            missingBlock->setAttribute ("block", 9);
            missingBlock->setAttribute ("param", rate->paramID);

            // Block 0 failed to load (unknown registry key) -- block 1 must still resolve as block 1.
            TremoloProcessor reloaded;
            auto bindings = midilearnxml::read (preset, { nullptr, reloaded.getParameters() });
            expectEquals ((int) bindings.size(), 1);
            expect (bindings.count (4) == 1 && bindings[4]->paramID == rate->paramID);
            expect (reloaded.getParameters()->getParameters (true).contains (bindings[4]));
        }
    }
};

static MidiLearnPresetXmlTests midiLearnPresetXmlTests;

} // namespace openguitarmultifx
