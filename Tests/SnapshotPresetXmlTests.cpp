#include "UI/SnapshotPresetXml.h"
#include "Effects/TremoloProcessor.h"
#include "Effects/NoiseGates/GateProcessor.h"

namespace openguitarmultifx
{

class SnapshotPresetXmlTests : public juce::UnitTest
{
public:
    SnapshotPresetXmlTests() : juce::UnitTest ("SnapshotPresetXml", "Presets") {}

    static std::pair<juce::String, float> findParam (const snapshotxml::BlockSnapshot& block,
                                                     const juce::String& paramId)
    {
        for (const auto& p : block.params)
            if (p.first == paramId)
                return p;
        return { {}, 0.0f };
    }

    void runTest() override
    {
        beginTest ("captureBlock grabs every float param plus the bypass flag");
        {
            TremoloProcessor tremolo;
            tremolo.setBypassed (true);
            auto snapshot = snapshotxml::captureBlock (tremolo);

            expect (snapshot.bypassed);
            expectEquals ((int) snapshot.params.size(), 2);
            expectEquals (findParam (snapshot, "tremolo_rate").second, 4.0f);
            expectEquals (findParam (snapshot, "tremolo_depth").second, 0.6f);
        }

        beginTest ("applyBlock restores captured params and bypass onto a fresh processor");
        {
            TremoloProcessor source;
            *source.getParameterPages()[0][0] = 8.5f; // rate
            source.setBypassed (true);
            const auto snapshot = snapshotxml::captureBlock (source);

            TremoloProcessor target; // defaults: rate 4.0, not bypassed
            snapshotxml::applyBlock (target, snapshot);

            expect (target.isBypassed());
            // operator= round-trips through NormalisableRange denormalisation,
            // so get() can come back an ulp off the value we wrote.
            expect (juce::approximatelyEqual (target.getParameterPages()[0][0]->get(), 8.5f));
            expect (juce::approximatelyEqual (target.getParameterPages()[0][1]->get(), 0.6f));
        }

        beginTest ("applyBlock skips unknown paramIDs instead of failing");
        {
            TremoloProcessor target;
            snapshotxml::BlockSnapshot snapshot;
            snapshot.params.emplace_back ("no_such_param", 3.0f);
            snapshot.params.emplace_back ("tremolo_rate", 9.0f);
            snapshotxml::applyBlock (target, snapshot);
            expect (juce::approximatelyEqual (target.getParameterPages()[0][0]->get(), 9.0f));
        }

        beginTest ("preset XML round-trips all eight snapshots with names, colours and block states");
        {
            std::array<snapshotxml::Snapshot, snapshotxml::maxSnapshots> snapshots;
            for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
            {
                snapshots[(size_t) i].name = "Snap " + juce::String (i);
                snapshots[(size_t) i].colour = "ff1122" + juce::String (i * 16);

                snapshotxml::BlockSnapshot block;
                block.bypassed = (i % 2) == 0;
                block.params.emplace_back ("tremolo_rate", 1.0f + (float) i);
                block.params.emplace_back ("tremolo_depth", 0.1f * (float) i);
                snapshots[(size_t) i].blocks.push_back (block);
            }

            juce::XmlElement preset ("Preset");
            snapshotxml::write (preset, snapshots, 5);

            std::array<snapshotxml::Snapshot, snapshotxml::maxSnapshots> loaded;
            int current = -1;
            expect (snapshotxml::read (preset, loaded, current));
            expectEquals (current, 5);

            for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
            {
                const auto& s = loaded[(size_t) i];
                expectEquals (s.name, juce::String ("Snap ") + juce::String (i));
                expectEquals (s.colour, juce::String ("ff1122") + juce::String (i * 16));
                expectEquals ((int) s.blocks.size(), 1);
                expect (s.blocks[0].bypassed == ((i % 2) == 0));
                expectEquals (findParam (s.blocks[0], "tremolo_rate").second, 1.0f + (float) i);
            }
        }

        beginTest ("a preset without <Snapshots> reads as absent -- the old-format fallback");
        {
            // Exactly what every pre-snapshots preset looks like: <Block>
            // elements at the top level and nothing else.
            juce::XmlElement preset ("Preset");
            auto* blockXml = preset.createNewChildElement ("Block");
            blockXml->setAttribute ("key", "tremolo");
            blockXml->setAttribute ("bypassed", false);

            std::array<snapshotxml::Snapshot, snapshotxml::maxSnapshots> loaded;
            loaded[2].name = "keepme";
            int current = 9;
            expect (! snapshotxml::read (preset, loaded, current));
            expectEquals (current, 9); // untouched on failure -- caller owns the default
            expectEquals (loaded[2].name, juce::String ("keepme"));
        }

        beginTest ("snapshot indices outside 0-7 are skipped, current is clamped");
        {
            juce::XmlElement preset ("Preset");
            auto* snapshotsXml = preset.createNewChildElement ("Snapshots");
            snapshotsXml->setAttribute ("current", 42);

            auto* valid = snapshotsXml->createNewChildElement ("Snapshot");
            valid->setAttribute ("index", 3);
            auto* stale = snapshotsXml->createNewChildElement ("Snapshot");
            stale->setAttribute ("index", 11); // a preset from a future build with more slots
            stale->setAttribute ("name", "future");

            std::array<snapshotxml::Snapshot, snapshotxml::maxSnapshots> loaded;
            int current = 0;
            expect (snapshotxml::read (preset, loaded, current));
            expectEquals (current, snapshotxml::maxSnapshots - 1);
            for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
                expect (i == 3 || loaded[(size_t) i].name != "future");
        }

        beginTest ("CC 69 maps values 0-7 onto snapshots A-H and ignores the rest");
        {
            expectEquals (snapshotxml::snapshotSelectCcNumber, 69);
            for (int v = 0; v < 8; ++v)
                expectEquals (snapshotxml::indexForCcValue (v), v);
            expectEquals (snapshotxml::indexForCcValue (8), -1);
            expectEquals (snapshotxml::indexForCcValue (127), -1);
            expectEquals (snapshotxml::indexForCcValue (-1), -1);
            expectEquals (snapshotxml::letterFor (0), juce::String ("A"));
            expectEquals (snapshotxml::letterFor (7), juce::String ("H"));
        }

        beginTest ("two processors with different param sets capture independently");
        {
            // A gate and a tremolo share no paramIDs -- captures must not
            // cross-contaminate when a chain mixes them.
            GateProcessor gate;
            TremoloProcessor tremolo;
            const auto gateSnapshot = snapshotxml::captureBlock (gate);
            const auto tremoloSnapshot = snapshotxml::captureBlock (tremolo);

            expect (findParam (gateSnapshot, "tremolo_rate").first.isEmpty());
            expect (findParam (tremoloSnapshot, "tremolo_rate").first.isNotEmpty());
        }
    }
};

static SnapshotPresetXmlTests snapshotPresetXmlTests;

} // namespace openguitarmultifx
