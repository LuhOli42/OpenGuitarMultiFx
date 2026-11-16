#pragma once

#include "ExpressionPedal.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"
#include "TouchSizing.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/** One learned MIDI assignment, as Settings -> MIDI lists it. The host builds these from its four binding stores
    (knobs, block on/off pedals, scene footswitches, tuner footswitch); a CC is in at most one of them. */
struct MidiMappingRow
{
    enum class Kind { knob, blockOnOff, scene, tuner };

    Kind kind = Kind::knob;
    int cc = -1;
    juce::String source; // "CC 11", "CC 11 - EXP 1"
    juce::String type;   // "Knob", "Block on/off", ...
    juce::String target; // "Wah - Freq", "Scene B"
    bool perPreset = false;
    const void* ref = nullptr; // which knob, for a CC driving several (an expression pedal)

    juce::String key() const { return juce::String ((int) kind) + ":" + juce::String (cc) + ":" + target + ":" + source; }
};

/** What the Expression page shows for one pedal; polled from the host. */
struct ExpressionPedalStatus
{
    exppedal::Pedal pedal;
    int lastRaw = -1;         // last value received on the pedal's CC, -1 = nothing yet
    bool learning = false;    // waiting for the pedal to move to take its CC
    bool calibrating = false;
    int calibrationLow = -1, calibrationHigh = -1;
    juce::String drives;      // the knobs learned on the pedal's CC in this preset, "" = none
};

/** The table of every learned MIDI assignment, one row each, with a Remove button per row. */
class MidiMappingTable : public juce::Component
{
public:
    std::function<void (const MidiMappingRow&)> onRemove;

    /** Rebuilds only when the rows changed, so polling it does not reset a button mid-press. */
    void setRows (std::vector<MidiMappingRow> newRows)
    {
        juce::String newKey;
        for (auto& r : newRows)
            newKey << r.key() << "|";
        if (newKey == rowsKey)
            return;

        rowsKey = newKey;
        rows = std::move (newRows);
        removeButtons.clear();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto* b = removeButtons.add (new juce::TextButton ("Remove"));
            addAndMakeVisible (b);
            b->onClick = [this, row = rows[i]]
            {
                if (onRemove)
                    onRemove (row);
            };
        }
        resized();
        repaint();
    }

    int getIdealHeight() const { return headerHeight + juce::jmax (1, (int) rows.size()) * rowHeight; }

    void resized() override
    {
        for (int i = 0; i < removeButtons.size(); ++i)
            removeButtons[i]->setBounds (rowBounds (i).removeFromRight (removeWidth).reduced (0, 4));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        auto header = getLocalBounds().removeFromTop (headerHeight);
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
        drawColumns (g, header, "MIDI", "TYPE", "CONTROLS", "SAVED IN");
        g.setColour (t.line);
        g.fillRect (header.removeFromBottom (1));

        if (rows.empty())
        {
            g.setColour (t.textMuted);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Nothing learned yet.", rowBounds (0).reduced (8, 0), juce::Justification::centredLeft);
            return;
        }

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto r = rowBounds (i);
            if (i % 2 == 1)
            {
                g.setColour (t.raised.withAlpha (0.5f));
                g.fillRoundedRectangle (r.toFloat(), t.smallRadius);
            }
            const auto& row = rows[(size_t) i];
            g.setColour (t.text);
            g.setFont (juce::FontOptions (15.0f));
            drawColumns (g, r, row.source, row.type, row.target, row.perPreset ? "This preset" : "All presets");
        }
    }

private:
    static constexpr int headerHeight = 30;
    static constexpr int rowHeight = touch::minTapTarget + 8;
    static constexpr int removeWidth = 110;

    juce::Rectangle<int> rowBounds (int i) const
    {
        return { 0, headerHeight + i * rowHeight, getWidth(), rowHeight };
    }

    void drawColumns (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& a, const juce::String& b,
                      const juce::String& c, const juce::String& d) const
    {
        r = r.reduced (8, 0);
        r.removeFromRight (removeWidth);
        const int w = r.getWidth();
        g.drawFittedText (a, r.removeFromLeft (w * 22 / 100), juce::Justification::centredLeft, 1);
        g.drawFittedText (b, r.removeFromLeft (w * 20 / 100), juce::Justification::centredLeft, 1);
        g.drawFittedText (d, r.removeFromRight (w * 18 / 100), juce::Justification::centredLeft, 1);
        g.drawFittedText (c, r, juce::Justification::centredLeft, 1);
    }

    std::vector<MidiMappingRow> rows;
    juce::String rowsKey { "-" };
    juce::OwnedArray<juce::TextButton> removeButtons;
};

/** One expression pedal's setup: CC, calibration, taper, polarity, auto-engage. Every change goes to the host. */
class ExpressionPedalEditor : public juce::Component
{
public:
    std::function<void()> onLearn, onClear, onCalibrate, onLinkKnobs;
    std::function<void (const exppedal::Pedal&)> onEdited;

    explicit ExpressionPedalEditor (int pedalIndex) : index (pedalIndex)
    {
        title.setText ("EXP " + juce::String (index + 1), juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
        for (auto* l : { &title, &ccLabel, &calibrationLabel, &curveLabel, &autoLabel, &drivesLabel })
            addAndMakeVisible (*l);
        for (auto* l : { &ccLabel, &calibrationLabel, &drivesLabel })
            l->setFont (juce::Font (juce::FontOptions (14.0f)));
        curveLabel.setText ("Curve", juce::dontSendNotification);
        autoLabel.setText ("Auto on/off", juce::dontSendNotification);
        drivesLabel.setJustificationType (juce::Justification::centredLeft);

        for (auto* b : { &learnButton, &clearButton, &calibrateButton, &invertButton, &autoButton, &linkButton })
            addAndMakeVisible (*b);
        linkButton.onClick = [this] { if (onLinkKnobs) onLinkKnobs(); };
        learnButton.onClick = [this] { if (onLearn) onLearn(); };
        clearButton.onClick = [this] { if (onClear) onClear(); };
        calibrateButton.onClick = [this] { if (onCalibrate) onCalibrate(); };

        invertButton.setClickingTogglesState (true);
        invertButton.onClick = [this] { edit ([this] (exppedal::Pedal& p) { p.invert = invertButton.getToggleState(); }); };
        autoButton.setClickingTogglesState (true);
        autoButton.onClick = [this] { edit ([this] (exppedal::Pedal& p) { p.autoEngage = autoButton.getToggleState(); }); };

        const char* curveNames[] = { "Linear", "Log", "Anti-log" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = curveButtons.add (new juce::TextButton (curveNames[i]));
            addAndMakeVisible (b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4800 + index * 10);
            b->onClick = [this, i] { edit ([i] (exppedal::Pedal& p) { p.curve = (exppedal::Curve) i; }); };
        }

        for (int ms : offTimesMs)
        {
            auto* b = offTimeButtons.add (new juce::TextButton (ms < 1000 ? juce::String (ms) + " ms"
                                                                          : juce::String (ms / 1000) + " s"));
            addAndMakeVisible (b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4801 + index * 10);
            b->onClick = [this, ms] { edit ([ms] (exppedal::Pedal& p) { p.autoOffMs = ms; }); };
        }
    }

    static constexpr int idealHeight = 26 + 8 + 22 + 10 + 5 * (touch::minTapTarget + 8);

    void setStatus (const ExpressionPedalStatus& s)
    {
        status = s;
        const auto& p = s.pedal;

        learnButton.setButtonText (s.learning ? "Cancel" : (p.isAssigned() ? "Learn again" : "Learn CC"));
        clearButton.setEnabled (p.isAssigned() && ! s.learning);
        calibrateButton.setButtonText (s.calibrating ? "Done" : "Calibrate");
        calibrateButton.setEnabled (p.isAssigned());
        linkButton.setEnabled (p.isAssigned() && ! s.learning);

        juce::String ccText;
        if (s.learning)
            ccText = "Move the pedal now...";
        else if (! p.isAssigned())
            ccText = "Not set. Tap Learn CC and move the pedal.";
        else
            ccText = "CC " + juce::String (p.cc) + (s.lastRaw >= 0 ? "  -  raw " + juce::String (s.lastRaw)
                                                                         + "  ->  " + juce::String (juce::roundToInt (p.shape (s.lastRaw) * 100.0f)) + "%"
                                                                       : juce::String ("  -  waiting for the pedal"));
        ccLabel.setText (ccText, juce::dontSendNotification);

        if (s.calibrating)
            calibrationLabel.setText ("Rock the pedal all the way heel to toe a few times, then tap Done."
                                      + (s.calibrationHigh >= 0 ? "  Seen " + juce::String (s.calibrationLow) + " - " + juce::String (s.calibrationHigh)
                                                                : juce::String()),
                                      juce::dontSendNotification);
        else
            calibrationLabel.setText ("Heel " + juce::String (p.heel) + "  -  Toe " + juce::String (p.toe)
                                      + (p.heel == 0 && p.toe == 127 ? "  (not calibrated)" : ""),
                                      juce::dontSendNotification);

        invertButton.setToggleState (p.invert, juce::dontSendNotification);
        invertButton.setButtonText (p.invert ? "Inverted" : "Invert");
        autoButton.setToggleState (p.autoEngage, juce::dontSendNotification);
        autoButton.setButtonText (p.autoEngage ? "On" : "Off");
        for (int i = 0; i < curveButtons.size(); ++i)
            curveButtons[i]->setToggleState ((int) p.curve == i, juce::dontSendNotification);
        for (int i = 0; i < offTimeButtons.size(); ++i)
        {
            offTimeButtons[i]->setToggleState (p.autoOffMs == offTimesMs[(size_t) i], juce::dontSendNotification);
            offTimeButtons[i]->setEnabled (p.autoEngage);
        }

        drivesLabel.setText (p.isAssigned()
                                 ? (s.drives.isNotEmpty() ? "Drives: " + s.drives : juce::String ("Drives nothing in this preset yet."))
                                       + "  Tap Link knobs to pick them on screen."
                                 : juce::String(),
                             juce::dontSendNotification);
        applyColours();
        repaint();
    }

    void applyColours()
    {
        const auto& t = theme::current();
        for (auto* l : { &title, &curveLabel, &autoLabel })
            l->setColour (juce::Label::textColourId, t.text);
        for (auto* l : { &ccLabel, &calibrationLabel, &drivesLabel })
            l->setColour (juce::Label::textColourId, t.textMuted);
    }

    void lookAndFeelChanged() override { applyColours(); }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        barBounds = area.removeFromTop (22);
        area.removeFromTop (10);

        const int rowH = touch::minTapTarget;
        auto row = area.removeFromTop (rowH);
        learnButton.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (8);
        clearButton.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (12);
        ccLabel.setBounds (row);
        area.removeFromTop (8);

        row = area.removeFromTop (rowH);
        calibrateButton.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (12);
        calibrationLabel.setBounds (row);
        area.removeFromTop (8);

        row = area.removeFromTop (rowH);
        curveLabel.setBounds (row.removeFromLeft (100));
        for (auto* b : curveButtons)
        {
            b->setBounds (row.removeFromLeft (110));
            row.removeFromLeft (6);
        }
        row.removeFromLeft (12);
        invertButton.setBounds (row.removeFromLeft (120));
        area.removeFromTop (8);

        row = area.removeFromTop (rowH);
        autoLabel.setBounds (row.removeFromLeft (100));
        autoButton.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (16);
        for (auto* b : offTimeButtons)
        {
            b->setBounds (row.removeFromLeft (90));
            row.removeFromLeft (6);
        }
        area.removeFromTop (8);

        row = area.removeFromTop (rowH);
        linkButton.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (12);
        drivesLabel.setBounds (row);
    }

    void paint (juce::Graphics& g) override
    {
        // Live pedal position: the track is the full 0-127, the light band is the calibrated heel..toe, the fill is
        // what the knobs receive (after calibration, invert and curve).
        const auto& t = theme::current();
        const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
        auto bar = barBounds.toFloat();
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (bar, t.smallRadius);

        const auto& p = status.pedal;
        if (! p.isAssigned())
            return;

        const auto xFor = [&bar] (int raw) { return bar.getX() + bar.getWidth() * (float) raw / 127.0f; };
        const int lo = status.calibrating && status.calibrationHigh >= 0 ? status.calibrationLow : juce::jmin (p.heel, p.toe);
        const int hi = status.calibrating && status.calibrationHigh >= 0 ? status.calibrationHigh : juce::jmax (p.heel, p.toe);
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (juce::Rectangle<float> (xFor (lo), bar.getY(), xFor (hi) - xFor (lo), bar.getHeight()), t.smallRadius);

        if (status.lastRaw >= 0)
        {
            const float shaped = p.shape (status.lastRaw);
            g.setColour (accent);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * shaped)).reduced (0.0f, 6.0f), t.smallRadius);
            g.setColour (t.text);
            g.fillRect (juce::Rectangle<float> (xFor (status.lastRaw) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
        }
    }

private:
    static constexpr std::array<int, 4> offTimesMs { 250, 500, 1000, 2000 };

    template <typename Fn>
    void edit (Fn&& change)
    {
        auto p = status.pedal;
        change (p);
        if (p != status.pedal && onEdited)
            onEdited (p);
    }

    int index;
    ExpressionPedalStatus status;
    juce::Rectangle<int> barBounds;

    juce::Label title, ccLabel, calibrationLabel, curveLabel, autoLabel, drivesLabel;
    juce::TextButton learnButton { "Learn CC" }, clearButton { "Clear" }, calibrateButton { "Calibrate" },
                     invertButton { "Invert" }, autoButton { "Off" }, linkButton { "Link knobs" };
    juce::OwnedArray<juce::TextButton> curveButtons, offTimeButtons;
};

} // namespace openguitarmultifx
