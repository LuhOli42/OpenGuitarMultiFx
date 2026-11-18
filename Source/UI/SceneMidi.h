#pragma once

#include "SnapshotPresetXml.h"
#include "MidiSourceCc.h"

static_assert (openguitarmultifx::midicc::snapshotSelectCcNumber == openguitarmultifx::snapshotxml::snapshotSelectCcNumber);

#include <juce_core/juce_core.h>

#include <iterator>
#include <map>
#include <memory>

namespace openguitarmultifx::scenemidi
{

/**
    Foot controller -> scene (snapshot) selection. CC 69 (value 0-7) stays the
    Line 6 convention for a controller that can send a value per scene; this is
    for the controllers that can't: a footswitch sends one fixed CC, so each
    scene (or "next" / "previous") gets its own CC, learned from the pedal.

    Global, not per preset: a pedal's wiring describes the hardware on the
    floor, not a sound -- the same footswitch should mean "scene B" in every
    preset. Saved next to the other flat settings (scene_midi.xml).
*/

/** Actions are 0-7 (scenes A-H) or one of these. */
constexpr int nextScene = -1;
constexpr int previousScene = -2;
constexpr int noAction = -3;

inline bool isValidAction (int action)
{
    return action == nextScene || action == previousScene
        || (action >= 0 && action < snapshotxml::maxSnapshots);
}

/** A momentary footswitch sends a value >= this when pressed (127 on every controller seen so far). */
constexpr int pressThreshold = 64;

/** A second stomp on the same CC this soon after the first is contact bounce, not a new stomp. */
constexpr juce::uint32 debounceMs = 150;

/** A low value this soon after a high one on the same CC is a momentary switch being released, not a stomp. */
constexpr juce::uint32 releaseWindowMs = 400;

/** Footswitch press detection shared by every "pedal does X" binding (scenes here, effect on/off in BypassPedalXml.h). */
class StompDetector
{
public:
    /** True when this message is a stomp. Three kinds of footswitch exist:
          - momentary, 127 on press and 0 on release: the release is not a stomp;
          - 127 on every stomp, never 0: every message is a stomp (a press-after-release rule would fire once and
            then ignore the pedal forever);
          - latching/toggle (M-Vave Chocolate): 127 on one stomp, 0 on the next -- or 0 first -- so a low value IS
            a stomp; a threshold-only rule missed every other stomp, and learning never saw a stomp sent as 0.
        A high value is always a stomp. A low value is a release when it arrives within releaseWindowMs of a high one
        on that CC -- that also marks the CC momentary, so its later releases are ignored however long it was held --
        and a stomp otherwise. The only miss: a momentary switch held longer than the window before its first quick
        tap fires once more on that release. Stomps within debounceMs of the previous one on the CC are bounce. */
    bool isPress (int cc, int value, juce::uint32 nowMs)
    {
        auto& s = states[cc];
        if (value >= pressThreshold)
        {
            s.lastHighMs = nowMs;
            s.sawHigh = true;
        }
        else
        {
            if (s.momentary)
                return false;
            if (s.sawHigh && nowMs - s.lastHighMs < releaseWindowMs)
            {
                s.momentary = true;
                return false;
            }
        }

        if (s.pressed && nowMs - s.lastPressMs < debounceMs)
            return false;

        s.lastPressMs = nowMs;
        s.pressed = true;
        return true;
    }

    void clear() { states.clear(); }

private:
    struct State
    {
        juce::uint32 lastPressMs = 0, lastHighMs = 0;
        bool pressed = false, sawHigh = false, momentary = false;
    };
    std::map<int, State> states;
};

class Bindings
{
public:
    /** Binds `cc` to `action`. One CC drives one action and one action has one CC, so any earlier binding of either is
        replaced. CC 69 is the reserved snapshot-select CC and cannot be bound. Returns false if refused. */
    bool bind (int cc, int action)
    {
        if (! midicc::isValid (cc) || midicc::isSnapshotSelect (cc) || ! isValidAction (action))
            return false;

        clearAction (action);
        byCc[cc] = action;
        return true;
    }

    void clearAction (int action)
    {
        for (auto it = byCc.begin(); it != byCc.end();)
            it = it->second == action ? byCc.erase (it) : std::next (it);
    }

    void clearCc (int cc) { byCc.erase (cc); }

    /** The CC bound to `action`, or -1. */
    int ccFor (int action) const
    {
        for (auto& [cc, bound] : byCc)
            if (bound == action)
                return cc;
        return -1;
    }

    /** Marks a stomp as seen without acting on it (the stomp that was just used to learn the binding). */
    bool isPress (int cc, int value, juce::uint32 nowMs) { return stomps.isPress (cc, value, nowMs); }

    bool hasCc (int cc) const { return byCc.find (cc) != byCc.end(); }
    bool empty() const { return byCc.empty(); }

    /** What a CC message should do right now: the scene to switch to, or -1 for nothing (unbound CC, a release, or
        a bounce). Call it once per incoming message. */
    int targetFor (int cc, int value, int currentScene, juce::uint32 nowMs = juce::Time::getMillisecondCounter(),
                   int sceneCount = snapshotxml::maxSnapshots)
    {
        const auto it = byCc.find (cc);
        if (it == byCc.end())
            return -1;
        if (! stomps.isPress (cc, value, nowMs))
            return -1;

        // Next / previous wrap over the scenes in use, not all eight slots; a scene pedal past them does nothing.
        const int count = juce::jlimit (1, snapshotxml::maxSnapshots, sceneCount);
        switch (it->second)
        {
            case nextScene:     return (currentScene + 1) % count;
            case previousScene: return (currentScene + count - 1) % count;
            default:            return it->second < count ? it->second : -1;
        }
    }

    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("SceneMidi");
        for (auto& [cc, action] : byCc)
        {
            auto* bind = xml->createNewChildElement ("Bind");
            bind->setAttribute ("cc", cc);
            bind->setAttribute ("action", action);
        }
        return xml;
    }

    /** Replaces everything. Entries that are out of range, duplicated or reserved are skipped, not fatal. */
    void fromXml (const juce::XmlElement* xml)
    {
        byCc.clear();
        stomps.clear();
        if (xml == nullptr || ! xml->hasTagName ("SceneMidi"))
            return;

        for (auto* entry : xml->getChildWithTagNameIterator ("Bind"))
            bind (entry->getIntAttribute ("cc", -1), entry->getIntAttribute ("action", noAction));
    }

private:
    std::map<int, int> byCc;
    StompDetector stomps;
};

inline juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("scene_midi.xml");
}

inline void load (Bindings& bindings, const juce::File& file = settingsFile())
{
    if (file.existsAsFile())
        bindings.fromXml (juce::XmlDocument::parse (file).get());
}

inline void save (const Bindings& bindings, const juce::File& file = settingsFile())
{
    file.getParentDirectory().createDirectory();
    if (const auto xml = bindings.toXml())
        xml->writeTo (file);
}

} // namespace openguitarmultifx::scenemidi
