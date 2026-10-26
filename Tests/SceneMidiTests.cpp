#include "UI/SceneMidi.h"

namespace openguitarmultifx
{

class SceneMidiTests : public juce::UnitTest
{
public:
    SceneMidiTests() : juce::UnitTest ("SceneMidi", "Presets") {}

    void runTest() override
    {
        using scenemidi::Bindings;

        beginTest ("a pedal bound to a scene selects it on every stomp, with or without a release");
        {
            Bindings b;
            expect (b.bind (22, 2));
            expectEquals (b.targetFor (22, 127, 0, 1000), 2);
            expectEquals (b.targetFor (22, 0, 2, 1050), -1, "release is not a press");
            expectEquals (b.targetFor (22, 127, 0, 2000), 2, "the next stomp fires again");
            expectEquals (b.targetFor (22, 127, 0, 3000), 2, "a pedal that never sends 0 still works");
            expectEquals (b.targetFor (22, 127, 0, 4000), 2);
            expectEquals (b.targetFor (99, 127, 0, 5000), -1, "unbound CC does nothing");
        }

        beginTest ("contact bounce within the debounce window counts once");
        {
            Bindings b;
            b.bind (10, 5);
            expectEquals (b.targetFor (10, 127, 0, 1000), 5);
            expectEquals (b.targetFor (10, 127, 0, 1000 + scenemidi::debounceMs - 1), -1);
            expectEquals (b.targetFor (10, 127, 0, 1000 + scenemidi::debounceMs), 5);
        }

        beginTest ("next and previous wrap around A-H");
        {
            Bindings b;
            b.bind (11, scenemidi::nextScene);
            b.bind (12, scenemidi::previousScene);
            expectEquals (b.targetFor (11, 127, 2, 1000), 3);
            expectEquals (b.targetFor (11, 0, 3, 1010), -1);
            expectEquals (b.targetFor (11, 127, 7, 2000), 0);
            expectEquals (b.targetFor (12, 127, 0, 3000), 7);
            expectEquals (b.targetFor (12, 0, 7, 3010), -1);
            expectEquals (b.targetFor (12, 127, 4, 4000), 3);
        }

        beginTest ("one CC drives one action and one action has one CC");
        {
            Bindings b;
            b.bind (10, 1);
            b.bind (10, 2);
            expectEquals (b.ccFor (1), -1, "re-binding the CC replaced scene B");
            expectEquals (b.ccFor (2), 10);
            b.bind (30, 2);
            expectEquals (b.ccFor (2), 30, "re-learning the scene moved it");
            expect (! b.hasCc (10));
        }

        beginTest ("CC 69 stays reserved and bad values are refused");
        {
            Bindings b;
            expect (! b.bind (snapshotxml::snapshotSelectCcNumber, 0));
            expect (! b.bind (10, 8));
            expect (! b.bind (10, scenemidi::noAction));
            expect (! b.bind (128, 0));
            expect (! b.bind (-1, 0));
            expect (b.empty());
        }

        beginTest ("clearAction removes the binding");
        {
            Bindings b;
            b.bind (10, 0);
            b.clearAction (0);
            expect (b.empty());
            expectEquals (b.targetFor (10, 127, 3, 1000), -1);
        }

        beginTest ("round-trips through XML and a file; garbage entries are skipped");
        {
            Bindings b;
            b.bind (10, 0);
            b.bind (22, scenemidi::nextScene);
            b.bind (24, scenemidi::previousScene);

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("ogmfx_scene_midi_test.xml");
            scenemidi::save (b, file);

            Bindings loaded;
            scenemidi::load (loaded, file);
            file.deleteFile();
            expectEquals (loaded.ccFor (0), 10);
            expectEquals (loaded.ccFor (scenemidi::nextScene), 22);
            expectEquals (loaded.ccFor (scenemidi::previousScene), 24);

            auto bad = juce::XmlDocument::parse (
                "<SceneMidi><Bind cc=\"69\" action=\"1\"/><Bind cc=\"5\" action=\"99\"/><Bind cc=\"6\" action=\"3\"/></SceneMidi>");
            Bindings fromBad;
            fromBad.fromXml (bad.get());
            expectEquals (fromBad.ccFor (3), 6);
            expect (! fromBad.hasCc (69));
            expect (! fromBad.hasCc (5));

            Bindings none;
            scenemidi::load (none, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ogmfx_nope.xml"));
            expect (none.empty(), "missing file = no bindings");
        }
    }
};

static SceneMidiTests sceneMidiTests;

} // namespace openguitarmultifx
