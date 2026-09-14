#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/** One string's target pitch: a human-readable name ("E2") plus the exact
    frequency it resolves to (equal temperament, A4 = 440Hz) -- computed
    once via tunings::noteNameToFrequency() rather than hand-typed, so
    there's no chance of a typo'd constant drifting from what the name
    says. */
struct TunedString
{
    juce::String noteName;
    float frequencyHz = 0.0f;
};

/** 4 to 8 entries, low string first -- what PolyphonicPitchDetector builds
    its band filters from, and what the tuner overlay's per-string gauges
    are laid out from. Deliberately NOT tied to guitar presets (a tuning is
    a property of the instrument you've physically got plugged in, not of
    a sound you dial up) -- see TuningSettings.h for where it's saved. */
using TuningProfile = std::vector<TunedString>;

namespace tunings
{

/** Parses a note name like "E2", "F#1", "Bb3" into a MIDI note number
    (C4 = middle C = 60, matching standard scientific pitch notation's
    octave numbering). Returns -1 for anything unparseable -- the ONLY
    place a note name's letters get decoded, so noteNameToFrequency()
    below and the custom per-string note picker's item numbering both go
    through the same parse instead of two that could quietly disagree. */
inline int noteNameToMidiNote (const juce::String& note) noexcept
{
    if (note.isEmpty())
        return -1;

    int index = 0;
    int semitone;
    switch (juce::CharacterFunctions::toUpperCase (note[index++]))
    {
        case 'C': semitone = 0;  break;
        case 'D': semitone = 2;  break;
        case 'E': semitone = 4;  break;
        case 'F': semitone = 5;  break;
        case 'G': semitone = 7;  break;
        case 'A': semitone = 9;  break;
        case 'B': semitone = 11; break;
        default: return -1;
    }

    if (index < note.length() && (note[index] == '#' || note[index] == 'b'))
        semitone += (note[index++] == '#') ? 1 : -1;

    if (index >= note.length())
        return -1;

    const int octave = note.substring (index).getIntValue();
    return (octave + 1) * 12 + semitone;
}

/** Equal temperament, A4 = 440Hz. 0 for anything noteNameToMidiNote()
    can't parse. */
inline float noteNameToFrequency (const juce::String& note) noexcept
{
    const int midiNote = noteNameToMidiNote (note);
    return midiNote < 0 ? 0.0f : 440.0f * std::pow (2.0f, (float) (midiNote - 69) / 12.0f);
}

/** Inverse of noteNameToMidiNote() -- used to build the custom note
    picker's item list, always spelled with sharps (never flats) so a
    round trip through noteNameToMidiNote() is unambiguous. */
inline juce::String midiNoteToName (int midiNote) noexcept
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = midiNote / 12 - 1;
    const int semitone = ((midiNote % 12) + 12) % 12;
    return juce::String (names[semitone]) + juce::String (octave);
}

inline TuningProfile fromNoteNames (std::initializer_list<const char*> noteNames)
{
    TuningProfile profile;
    for (auto* name : noteNames)
        profile.push_back ({ name, noteNameToFrequency (name) });
    return profile;
}

// Standard tunings, low string first -- the quick-pick presets in
// PolyphonicTunerOverlay. 7/8-string just extend 6-string downward (the
// common "add a low string, keep the rest" convention), not a different
// tuning scheme.
inline TuningProfile standardGuitar6() { return fromNoteNames ({ "E2", "A2", "D3", "G3", "B3", "E4" }); }
inline TuningProfile standardGuitar7() { return fromNoteNames ({ "B1", "E2", "A2", "D3", "G3", "B3", "E4" }); }
inline TuningProfile standardGuitar8() { return fromNoteNames ({ "F#1", "B1", "E2", "A2", "D3", "G3", "B3", "E4" }); }
inline TuningProfile standardBass4()   { return fromNoteNames ({ "E1", "A1", "D2", "G2" }); }
inline TuningProfile standardBass5()   { return fromNoteNames ({ "B0", "E1", "A1", "D2", "G2" }); }
inline TuningProfile standardBass6()   { return fromNoteNames ({ "B0", "E1", "A1", "D2", "G2", "C3" }); }

constexpr int minStrings = 4;
constexpr int maxStrings = 8;

} // namespace tunings

} // namespace openguitarmultifx
