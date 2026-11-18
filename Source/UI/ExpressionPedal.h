#pragma once

#include <juce_core/juce_core.h>
#include "MidiSourceCc.h"

#include <array>
#include <cmath>

namespace openguitarmultifx::exppedal
{

/**
    Expression pedal setup (Settings -> Expression). An expression pedal reaches the app as a continuous MIDI CC from
    the foot controller (the onboard EXP jack is Phase 8 hardware). What a multi-FX unit lets you set for one -- the
    Helix/POD "pedal calibration", Boss GT "PDL CALIBRATION", Fractal taper, wah auto-engage -- is kept here:

    - `cc`: which CC the pedal sends. Knobs are still assigned with MIDI Learn (hold the knob's name, move the pedal);
      every knob learned on this CC then gets the calibrated, shaped value instead of the raw 0-127.
    - `heel` / `toe`: the raw values the pedal actually reaches. Most pedals never send a clean 0 and 127, so a wah
      would never fully close or a volume pedal never fully mute without this.
    - `curve` and `invert`: the taper, and reversed polarity for pedals wired the other way round.
    - `autoEngage`: moving the pedal switches its blocks on; resting at the heel for `autoOffMs` switches them off
      (the "wah turns itself on" behaviour, no toe switch needed).

    Global, not per preset, like scene_midi.xml: it describes the pedal on the floor, not a sound.
*/

constexpr int numPedals = 2;

enum class Curve { linear = 0, log = 1, antiLog = 2 };

/** Below this (after shaping) the pedal counts as resting at the heel for auto-engage. */
constexpr float restThreshold = 0.02f;

constexpr int minAutoOffMs = 100;
constexpr int maxAutoOffMs = 5000;

/** A calibration that spans fewer raw steps than this is a mistake (pedal not moved), not a pedal. */
constexpr int minCalibrationSpan = 8;

struct Pedal
{
    int cc = -1;
    int heel = 0;
    int toe = 127;
    Curve curve = Curve::linear;
    bool invert = false;
    bool autoEngage = false;
    int autoOffMs = 500;

    bool isAssigned() const { return midicc::isValid (cc); }

    /** Raw CC value -> 0..1 at the parameter: calibrated to heel..toe, inverted if asked, then the taper. */
    float shape (int raw) const
    {
        const int span = toe - heel;
        float t = span == 0 ? (raw >= toe ? 1.0f : 0.0f) : (float) (raw - heel) / (float) span;
        t = juce::jlimit (0.0f, 1.0f, t);
        if (invert)
            t = 1.0f - t;

        switch (curve)
        {
            case Curve::log:     return t * t;                           // slow at the heel, like a volume pedal's audio taper
            case Curve::antiLog: return 1.0f - (1.0f - t) * (1.0f - t);  // fast at the heel
            case Curve::linear:  break;
        }
        return t;
    }

    bool operator== (const Pedal& o) const
    {
        return cc == o.cc && heel == o.heel && toe == o.toe && curve == o.curve && invert == o.invert
            && autoEngage == o.autoEngage && autoOffMs == o.autoOffMs;
    }
    bool operator!= (const Pedal& o) const { return ! (*this == o); }
};

struct Settings
{
    std::array<Pedal, numPedals> pedals {};

    /** Index of the pedal on `cc`, or -1. */
    int indexForCc (int cc) const
    {
        for (int i = 0; i < numPedals; ++i)
            if (pedals[(size_t) i].isAssigned() && pedals[(size_t) i].cc == cc)
                return i;
        return -1;
    }

    /** Puts pedal `index` on `cc`; another pedal already on that CC loses it. */
    void assignCc (int index, int cc)
    {
        if (index < 0 || index >= numPedals || ! midicc::isValid (cc))
            return;
        for (auto& p : pedals)
            if (p.cc == cc)
                p.cc = -1;
        pedals[(size_t) index].cc = cc;
    }
};

/** Heel/toe learning: rock the pedal fully while this watches, then finish. */
class Calibrator
{
public:
    void begin() { lo = 128; hi = -1; }
    void observe (int raw)
    {
        lo = juce::jmin (lo, raw);
        hi = juce::jmax (hi, raw);
    }
    bool hasRange() const { return hi - lo >= minCalibrationSpan; }
    int lowest() const { return lo; }
    int highest() const { return hi; }

    /** Writes the observed range into `pedal`. False (pedal untouched) when the pedal was not moved enough. */
    bool finish (Pedal& pedal) const
    {
        if (! hasRange())
            return false;
        pedal.heel = lo;
        pedal.toe = hi;
        return true;
    }

private:
    int lo = 128, hi = -1;
};

/** Auto-engage state of one pedal. Feed it every shaped value, and tick it from the UI timer. */
class AutoEngage
{
public:
    enum class Action { none, engage, disengage };

    Action onValue (float shaped, juce::uint32 nowMs)
    {
        if (shaped > restThreshold)
        {
            resting = false;
            if (! engaged)
            {
                engaged = true;
                return Action::engage;
            }
            return Action::none;
        }

        if (! resting)
        {
            resting = true;
            restSinceMs = nowMs;
        }
        return Action::none;
    }

    Action tick (juce::uint32 nowMs, int autoOffMs)
    {
        if (engaged && resting && nowMs - restSinceMs >= (juce::uint32) juce::jmax (0, autoOffMs))
        {
            engaged = false;
            return Action::disengage;
        }
        return Action::none;
    }

    void reset() { engaged = false; resting = true; restSinceMs = 0; }

private:
    bool engaged = false;
    bool resting = true;
    juce::uint32 restSinceMs = 0;
};

inline std::unique_ptr<juce::XmlElement> toXml (const Settings& settings)
{
    auto xml = std::make_unique<juce::XmlElement> ("ExpressionPedals");
    for (int i = 0; i < numPedals; ++i)
    {
        const auto& p = settings.pedals[(size_t) i];
        auto* e = xml->createNewChildElement ("Pedal");
        e->setAttribute ("index", i);
        e->setAttribute ("cc", p.cc);
        e->setAttribute ("heel", p.heel);
        e->setAttribute ("toe", p.toe);
        e->setAttribute ("curve", (int) p.curve);
        e->setAttribute ("invert", p.invert);
        e->setAttribute ("autoEngage", p.autoEngage);
        e->setAttribute ("autoOffMs", p.autoOffMs);
    }
    return xml;
}

/** Anything out of range falls back to the default for that field rather than failing the whole file. */
inline Settings fromXml (const juce::XmlElement* xml)
{
    Settings settings;
    if (xml == nullptr || ! xml->hasTagName ("ExpressionPedals"))
        return settings;

    for (auto* e : xml->getChildWithTagNameIterator ("Pedal"))
    {
        const int index = e->getIntAttribute ("index", -1);
        if (index < 0 || index >= numPedals)
            continue;

        Pedal p;
        const int heel = e->getIntAttribute ("heel", 0);
        const int toe = e->getIntAttribute ("toe", 127);
        if (heel >= 0 && heel <= 127 && toe >= 0 && toe <= 127 && std::abs (toe - heel) >= minCalibrationSpan)
        {
            p.heel = heel;
            p.toe = toe;
        }
        const int curve = e->getIntAttribute ("curve", 0);
        p.curve = curve >= 0 && curve <= 2 ? (Curve) curve : Curve::linear;
        p.invert = e->getBoolAttribute ("invert", false);
        p.autoEngage = e->getBoolAttribute ("autoEngage", false);
        p.autoOffMs = juce::jlimit (minAutoOffMs, maxAutoOffMs, e->getIntAttribute ("autoOffMs", 500));
        settings.pedals[(size_t) index] = p;

        const int cc = e->getIntAttribute ("cc", -1);
        if (midicc::isValid (cc) && ! midicc::isSnapshotSelect (cc))
            settings.assignCc (index, cc);
    }
    return settings;
}

inline juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("expression_pedals.xml");
}

inline Settings load (const juce::File& file = settingsFile())
{
    if (! file.existsAsFile())
        return {};
    return fromXml (juce::XmlDocument::parse (file).get());
}

inline void save (const Settings& settings, const juce::File& file = settingsFile())
{
    file.getParentDirectory().createDirectory();
    if (const auto xml = toXml (settings))
        xml->writeTo (file);
}

} // namespace openguitarmultifx::exppedal
