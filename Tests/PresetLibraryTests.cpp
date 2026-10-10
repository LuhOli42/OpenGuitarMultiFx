#include "Presets/PresetLibrary.h"
#include "Presets/PresetMetadataXml.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class PresetLibraryTests : public juce::UnitTest
{
public:
    PresetLibraryTests() : juce::UnitTest ("PresetLibrary", "Presets") {}

    void runTest() override
    {
        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("OGMFxPresetLibraryTests_" + juce::String (juce::Random::getSystemRandom().nextInt64()));

        PresetManager presets (tempDir);
        PresetLibrary library (tempDir);

        auto savePreset = [&presets] (const juce::String& name, int number)
        {
            juce::XmlElement xml ("Preset");
            xml.setAttribute ("number", number);
            xml.createNewChildElement ("Block")->setAttribute ("key", "sfx.gate");
            presets.savePreset (name, xml);
        };

        beginTest ("a preset saved before metadata existed reads as untagged and unassigned");
        {
            savePreset ("Old Preset", 1);

            const auto entries = library.entries();
            expectEquals ((int) entries.size(), 1);
            expect (entries[0].tags.isEmpty());
            expect (entries[0].bankSlot.bank < 0);
            expect (entries[0].setlists.empty());
            expect (library.setlistNames().isEmpty());
            expect (library.allTags().isEmpty());
        }

        beginTest ("tags round-trip through the preset file");
        {
            expect (library.setTags ("Old Preset", { "lead", "gig", "lead", "  ", "crunch" }));

            const auto entries = library.entries();
            expectEquals ((int) entries.size(), 1);
            // writeTags trims, dedupes and drops blanks.
            expectEquals (entries[0].tags.size(), 3);
            expect (entries[0].tags.contains ("lead"));
            expect (entries[0].tags.contains ("gig"));
            expect (entries[0].tags.contains ("crunch"));
            expectEquals (library.allTags().size(), 3);

            // The sound content is untouched by a metadata edit.
            auto xml = presets.loadPreset ("Old Preset");
            expect (xml != nullptr);
            expect (xml->getChildByName ("Block") != nullptr);

            expect (library.setTags ("Old Preset", {}));
            expect (library.entries()[0].tags.isEmpty());
            xml = presets.loadPreset ("Old Preset");
            expect (xml->getChildByName ("Tags") == nullptr);
        }

        beginTest ("bank/slot assignment is stored, evicts the previous occupant, and clears");
        {
            savePreset ("Rhythm", 2);
            savePreset ("Solo", 3);

            expect (library.assignToSlot ("Rhythm", 0, 0));
            expectEquals (library.presetInSlot (0, 0), juce::String ("Rhythm"));

            // A foot-switch position can only mean one preset.
            expect (library.assignToSlot ("Solo", 0, 0));
            expectEquals (library.presetInSlot (0, 0), juce::String ("Solo"));
            for (auto& e : library.entries())
                if (e.name == "Rhythm")
                    expect (e.bankSlot.bank < 0);

            // Reassigning moves the preset instead of duplicating it.
            expect (library.assignToSlot ("Solo", 0, 3));
            expect (library.presetInSlot (0, 0).isEmpty());
            expectEquals (library.presetInSlot (0, 3), juce::String ("Solo"));

            expect (library.clearSlotAssignment ("Solo"));
            expect (library.presetInSlot (0, 3).isEmpty());

            // Out-of-range writes are refused, and so are unknown presets.
            expect (! library.assignToSlot ("Solo", -1, 0));
            expect (! library.assignToSlot ("Solo", 0, PresetLibrary::maxSlotsPerBank));
            expect (! library.assignToSlot ("Nonexistent", 0, 0));
        }

        beginTest ("setlists keep member order and step through with wraparound");
        {
            savePreset ("Intro", 4);
            savePreset ("Verse", 5);
            savePreset ("Bridge", 6);

            expect (library.addToSetlist ("Verse", "Gig A"));
            expect (library.addToSetlist ("Intro", "Gig A"));
            expect (library.addToSetlist ("Bridge", "Gig A"));

            expectEquals (library.setlistNames().size(), 1);
            expect (library.setlistMembers ("Gig A") == juce::StringArray ({ "Verse", "Intro", "Bridge" }));

            // Re-adding is a no-op, not a duplicate seat.
            expect (library.addToSetlist ("Verse", "Gig A"));
            expectEquals (library.setlistMembers ("Gig A").size(), 3);

            // Stepping wraps at both ends; a non-member current steps to an end.
            expectEquals (library.stepSetlist ("Gig A", "Verse", 1), juce::String ("Intro"));
            expectEquals (library.stepSetlist ("Gig A", "Verse", -1), juce::String ("Bridge"));
            expectEquals (library.stepSetlist ("Gig A", "Bridge", 1), juce::String ("Verse"));
            expectEquals (library.stepSetlist ("Gig A", "Unrelated", 1), juce::String ("Verse"));
            expectEquals (library.stepSetlist ("Gig A", "Unrelated", -1), juce::String ("Bridge"));
            expect (library.stepSetlist ("Empty List", "Verse", 1).isEmpty());
        }

        beginTest ("a preset can belong to several setlists; deleting a preset drops its seat");
        {
            expect (library.addToSetlist ("Verse", "Gig B"));
            expect (library.setlistNames() == juce::StringArray ({ "Gig A", "Gig B" }));
            expect (library.setlistMembers ("Gig B") == juce::StringArray ({ "Verse" }));

            expect (presets.deletePreset ("Verse"));
            expect (library.setlistMembers ("Gig A") == juce::StringArray ({ "Intro", "Bridge" }));
            // Gig B's only member is gone, so the setlist itself is gone too.
            expect (! library.setlistNames().contains ("Gig B"));
        }

        beginTest ("moveInSetlist swaps seats and refuses to move past the ends");
        {
            expect (library.moveInSetlist ("Gig A", "Bridge", -1));
            expect (library.setlistMembers ("Gig A") == juce::StringArray ({ "Bridge", "Intro" }));

            expect (! library.moveInSetlist ("Gig A", "Bridge", -1)); // already first
            expect (! library.moveInSetlist ("Gig A", "Intro", 1));   // already last
            expect (! library.moveInSetlist ("Gig A", "Nonexistent", 1));
            expect (library.setlistMembers ("Gig A") == juce::StringArray ({ "Bridge", "Intro" }));
        }

        beginTest ("renameSetlist/deleteSetlist keep the presets but move the seats");
        {
            expect (library.renameSetlist ("Gig A", "Tour"));
            expect (library.setlistNames() == juce::StringArray ({ "Tour" }));
            expect (library.setlistMembers ("Tour") == juce::StringArray ({ "Bridge", "Intro" }));

            expect (! library.renameSetlist ("Tour", "Tour"));
            expect (! library.renameSetlist ("Nonexistent", "Other"));

            expect (library.deleteSetlist ("Tour"));
            expect (library.setlistNames().isEmpty());
            expect (presets.presetExists ("Bridge"));
            expect (presets.presetExists ("Intro"));
        }

        beginTest ("search matches name substrings case-insensitively and filters by tag");
        {
            savePreset ("Clean Tone", 10);
            savePreset ("Crunch Tone", 11);
            savePreset ("Lead Solo", 12);
            library.setTags ("Clean Tone", { "clean", "gig" });
            library.setTags ("Crunch Tone", { "crunch", "gig" });
            library.setTags ("Lead Solo", { "lead" });

            expect (library.search ("tone", {}) == juce::StringArray ({ "Clean Tone", "Crunch Tone" }));
            expect (library.search ("TONE", {}) == juce::StringArray ({ "Clean Tone", "Crunch Tone" }));
            expect (library.search ("solo", {}) == juce::StringArray ({ "Solo", "Lead Solo" }));
            // Number order, not alphabetical: 1,2,3,4,6,10,11,12 (Verse was deleted above).
            expect (library.search ({}, {}) == juce::StringArray ({ "Old Preset", "Rhythm", "Solo", "Intro", "Bridge",
                                                                   "Clean Tone", "Crunch Tone", "Lead Solo" }));
            expect (library.search ({}, "gig") == juce::StringArray ({ "Clean Tone", "Crunch Tone" }));
            expect (library.search ("tone", "crunch") == juce::StringArray ({ "Crunch Tone" }));
            expect (library.search ("tone", "lead").isEmpty());
        }

        beginTest ("renamePreset/duplicatePreset carry metadata; clearMemberships strips seats but keeps tags");
        {
            library.setTags ("Clean Tone", { "clean", "gig" });
            library.assignToSlot ("Clean Tone", 1, 2);
            library.addToSetlist ("Clean Tone", "Gig C");

            expect (presets.renamePreset ("Clean Tone", "Sparkle"));
            {
                const auto entries = library.entries();
                const PresetLibrary::Entry* e = nullptr;
                for (auto& it : entries)
                    if (it.name == "Sparkle")
                        e = &it;
                expect (e != nullptr);
                if (e != nullptr)
                {
                    expect (e->tags.contains ("clean"));
                    expect (e->bankSlot.bank == 1 && e->bankSlot.slot == 2);
                    expect (e->isInSetlist ("Gig C"));
                }
            }

            expect (presets.duplicatePreset ("Sparkle", "Sparkle Copy"));
            expect (library.clearMemberships ("Sparkle Copy"));
            {
                const auto entries = library.entries();
                const PresetLibrary::Entry* e = nullptr;
                for (auto& it : entries)
                    if (it.name == "Sparkle Copy")
                        e = &it;
                expect (e != nullptr);
                if (e != nullptr)
                {
                    expect (e->tags.contains ("clean"));      // tags travel with the copy
                    expect (e->bankSlot.bank < 0);            // but it can't steal the slot
                    expect (! e->isInSetlist ("Gig C"));      // or the setlist seat
                }
            }
            expectEquals (library.presetInSlot (1, 2), juce::String ("Sparkle"));
            expect (library.setlistMembers ("Gig C") == juce::StringArray ({ "Sparkle" }));
        }

        beginTest ("a resave keeps metadata via copyOrganisation (the savePresetAs merge path)");
        {
            // Simulate what MainComponent::savePresetAs does: buildPresetXml()
            // emits only the sound, then the saved file's organisation is
            // merged into the fresh XML before writing.
            auto existing = presets.loadPreset ("Sparkle");
            expect (existing != nullptr);

            juce::XmlElement freshSound ("Preset");
            freshSound.setAttribute ("number", existing->getIntAttribute ("number"));
            freshSound.createNewChildElement ("Block")->setAttribute ("key", "sfx.gate");
            presetmeta::copyOrganisation (*existing, freshSound);
            expect (presets.savePreset ("Sparkle", freshSound));

            const auto entries = library.entries();
            const PresetLibrary::Entry* e = nullptr;
            for (auto& it : entries)
                if (it.name == "Sparkle")
                    e = &it;
            expect (e != nullptr);
            if (e != nullptr)
            {
                expect (e->tags.contains ("clean"));
                expect (e->bankSlot.bank == 1 && e->bankSlot.slot == 2);
                expect (e->isInSetlist ("Gig C"));
            }
        }

        tempDir.deleteRecursively();
    }
};

static PresetLibraryTests presetLibraryTests;

} // namespace openguitarmultifx
