#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Undo / Redo over whole-board snapshots (the preset XML: chain, knobs, bypass, routing, snapshots, MIDI Learn).
    The owner polls the live state and calls noteState() with it; a state that differs from the current entry and
    has stayed the same for `settlePolls` polls in a row becomes a new entry, so one knob turn is one step, not fifty.
    Undo / Redo hand back the entry to restore; the owner then calls adopt() with what the board actually reads after
    restoring, so a restore never counts as a new edit. Message thread only.
*/
class UndoHistory
{
public:
    explicit UndoHistory (int maxEntries = 50, int settlePolls = 2) : limit (maxEntries), settle (settlePolls) {}

    /** Feed the live state once per poll. Returns true when it became a new entry. */
    bool noteState (const juce::XmlElement& live)
    {
        const auto text = live.toString();
        if (entries.empty())
        {
            push (live, text);
            return true;
        }
        if (text != lastSeen)
        {
            lastSeen = text;
            stablePolls = 0;
            return false;
        }
        if (++stablePolls < settle || text == entries[(size_t) index].text)
            return false;
        push (live, text);
        return true;
    }

    bool canUndo() const noexcept { return index > 0; }
    bool canRedo() const noexcept { return index >= 0 && index + 1 < (int) entries.size(); }

    /** The entry to restore, or nullptr. Valid until the next adopt() / noteState() / clear(). */
    const juce::XmlElement* undo()
    {
        if (! canUndo())
            return nullptr;
        return entries[(size_t) --index].xml.get();
    }

    const juce::XmlElement* redo()
    {
        if (! canRedo())
            return nullptr;
        return entries[(size_t) ++index].xml.get();
    }

    /** After restoring: what the board reads now becomes the current entry's text (a restore can normalise details),
        so the next poll does not see a change. */
    void adopt (const juce::XmlElement& live)
    {
        if (index < 0)
            return;
        auto& e = entries[(size_t) index];
        auto copy = std::make_unique<juce::XmlElement> (live); // `live` may BE e.xml (adopting what undo() returned)
        e.text = copy->toString();
        e.xml = std::move (copy);
        lastSeen = e.text;
        stablePolls = 0;
    }

    void clear()
    {
        entries.clear();
        index = -1;
        lastSeen.clear();
        stablePolls = 0;
    }

    int size() const noexcept { return (int) entries.size(); }
    int position() const noexcept { return index; }

private:
    struct Entry
    {
        std::unique_ptr<juce::XmlElement> xml;
        juce::String text;
    };

    void push (const juce::XmlElement& live, const juce::String& text)
    {
        entries.erase (entries.begin() + (index + 1), entries.end()); // a new edit drops the redo branch
        entries.push_back ({ std::make_unique<juce::XmlElement> (live), text });
        if ((int) entries.size() > limit)
            entries.erase (entries.begin());
        index = (int) entries.size() - 1;
        lastSeen = text;
        stablePolls = 0;
    }

    std::vector<Entry> entries;
    int index = -1;
    juce::String lastSeen;
    int stablePolls = 0;
    const int limit, settle;
};

} // namespace openguitarmultifx
