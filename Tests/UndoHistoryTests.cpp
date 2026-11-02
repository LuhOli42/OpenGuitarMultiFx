#include "UI/UndoHistory.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class UndoHistoryTests : public juce::UnitTest
{
public:
    UndoHistoryTests() : juce::UnitTest ("UndoHistory", "UI") {}

    static juce::XmlElement state (int v)
    {
        juce::XmlElement x ("Preset");
        x.setAttribute ("v", v);
        return x;
    }

    void runTest() override
    {
        beginTest ("a change becomes an entry only after it settles; one knob turn is one step");
        {
            UndoHistory h (50, 2);
            expect (h.noteState (state (0))); // the first state is the baseline
            expect (! h.noteState (state (1)));
            expect (! h.noteState (state (2))); // still moving
            expect (! h.noteState (state (3)));
            expect (! h.noteState (state (3)));
            expect (h.noteState (state (3)));   // stayed put for two polls
            expectEquals (h.size(), 2);
        }

        beginTest ("undo / redo walk the entries; a new edit drops the redo branch");
        {
            UndoHistory h (50, 1);
            h.noteState (state (0));
            for (int v : { 1, 2 })
            {
                h.noteState (state (v));
                h.noteState (state (v));
            }
            expectEquals (h.size(), 3);
            auto* back = h.undo();
            expect (back != nullptr && back->getIntAttribute ("v") == 1);
            h.adopt (*back);
            expect (h.canRedo());
            auto* fwd = h.redo();
            expect (fwd != nullptr && fwd->getIntAttribute ("v") == 2);
            h.adopt (*fwd);
            h.undo();
            h.adopt (state (1));
            h.noteState (state (9));
            h.noteState (state (9));
            expect (! h.canRedo());
            expectEquals (h.size(), 3); // 0, 1, 9
        }

        beginTest ("restoring is not an edit; the oldest entries fall off past the limit");
        {
            UndoHistory h (3, 1);
            h.noteState (state (0));
            for (int v = 1; v <= 5; ++v)
            {
                h.noteState (state (v));
                h.noteState (state (v));
            }
            expectEquals (h.size(), 3);
            const juce::XmlElement restored (*h.undo()); // copy: adopt() replaces the entry the pointer refers to
            h.adopt (restored);
            expect (! h.noteState (restored));
            expect (! h.noteState (restored));
            expectEquals (h.size(), 3);
        }
    }
};

static UndoHistoryTests undoHistoryTests;

} // namespace openguitarmultifx
