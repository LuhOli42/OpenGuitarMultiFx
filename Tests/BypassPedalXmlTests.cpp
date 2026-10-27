#include "UI/BypassPedalXml.h"
#include "UI/SceneMidi.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"
#include "Effects/TremoloProcessor.h"

namespace openguitarmultifx
{

class BypassPedalXmlTests : public juce::UnitTest
{
public:
    BypassPedalXmlTests() : juce::UnitTest ("BypassPedalXml", "Presets") {}

    void runTest() override
    {
        beginTest ("pedals round-trip onto freshly-created blocks, by block index");
        {
            TremoloProcessor tremolo;
            BassmanStyleAmplifierProcessor amp;

            juce::XmlElement preset ("Preset");
            bypasspedal::write (preset, { { 22, &tremolo }, { 24, &amp } }, { &tremolo, &amp });
            expect (preset.getChildByName ("BypassPedals") != nullptr);

            TremoloProcessor reloadedTremolo;
            BassmanStyleAmplifierProcessor reloadedAmp;
            const auto bindings = bypasspedal::read (preset, { &reloadedTremolo, &reloadedAmp });
            expectEquals ((int) bindings.size(), 2);
            expect (bindings.at (22) == &reloadedTremolo);
            expect (bindings.at (24) == &reloadedAmp);
        }

        beginTest ("no pedals writes nothing; a preset without <BypassPedals> reads as none");
        {
            TremoloProcessor tremolo;
            juce::XmlElement preset ("Preset");
            bypasspedal::write (preset, {}, { &tremolo });
            expect (preset.getChildByName ("BypassPedals") == nullptr);
            expect (bypasspedal::read (preset, { &tremolo }).empty());
        }

        beginTest ("a pedal on a block outside the saved ones is not written");
        {
            TremoloProcessor saved, notInPreset;
            juce::XmlElement preset ("Preset");
            bypasspedal::write (preset, { { 7, &notInPreset } }, { &saved });
            expect (preset.getChildByName ("BypassPedals") == nullptr);
        }

        beginTest ("a skipped block keeps later indices aligned; bad entries are dropped");
        {
            TremoloProcessor tremolo, other;
            auto xml = juce::XmlDocument::parse (
                "<Preset><BypassPedals>"
                "<Pedal cc=\"10\" block=\"0\"/>"   // skipped block (nullptr)
                "<Pedal cc=\"11\" block=\"1\"/>"   // fine
                "<Pedal cc=\"11\" block=\"2\"/>"   // repeated CC
                "<Pedal cc=\"69\" block=\"1\"/>"   // reserved
                "<Pedal cc=\"12\" block=\"9\"/>"   // no such block
                "<Pedal cc=\"200\" block=\"1\"/>"  // not a CC
                "</BypassPedals></Preset>");
            const auto bindings = bypasspedal::read (*xml, { nullptr, &tremolo, &other });
            expectEquals ((int) bindings.size(), 1);
            expect (bindings.at (11) == &tremolo);
        }

        beginTest ("the stomp detector counts every stomp, with or without a release");
        {
            scenemidi::StompDetector stomps;
            expect (stomps.isPress (20, 127, 1000));
            expect (! stomps.isPress (20, 0, 1050), "a release is not a stomp");
            expect (stomps.isPress (20, 127, 2000));
            expect (stomps.isPress (20, 127, 3000), "a pedal that never sends 0 still works");
            expect (! stomps.isPress (20, 127, 3000 + scenemidi::debounceMs - 1), "bounce");
            expect (stomps.isPress (21, 127, 3001), "other CCs are independent");
        }
    }
};

static BypassPedalXmlTests bypassPedalXmlTests;

} // namespace openguitarmultifx
