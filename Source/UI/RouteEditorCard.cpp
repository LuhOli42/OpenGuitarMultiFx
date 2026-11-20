#include "RouteEditorCard.h"
#include "Theme.h"
#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    constexpr int lineHeight = touch::minTapTarget + 12;
    constexpr int nameWidth = 120;
    constexpr int extraWidth = 190;   // pan fader or channel-map picker
    constexpr int invertWidth = 64;
    constexpr int gap = 8;
}

class RouteEditorCard::Line : public juce::Component
{
public:
    Line (int lineIndex, Entry e, std::function<void (int, const Entry&)> changed)
        : index (lineIndex), entry (std::move (e)), onChange (std::move (changed))
    {
        nameLabel.setText (entry.name, juce::dontSendNotification);
        nameLabel.setFont (juce::Font (16.0f, juce::Font::bold));
        addAndMakeVisible (nameLabel);

        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, touch::minTapTarget);
        level.setRange (minGainDb, maxGainDb, 0.5);
        level.setDoubleClickReturnValue (true, 0.0);
        level.textFromValueFunction = [] (double db) { return db <= minGainDb ? juce::String ("-inf") : juce::String (db, 1) + " dB"; };
        level.setValue (entry.gainDb, juce::dontSendNotification);
        level.updateText(); // the value may equal the default, which skips the formatter
        bindApplyOnRelease (level, [this] { entry.gainDb = (float) level.getValue(); });
        addAndMakeVisible (level);

        if (entry.kind == Entry::Kind::stereoOutput)
        {
            pan.setSliderStyle (juce::Slider::LinearHorizontal);
            pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, touch::minTapTarget);
            pan.setRange (-100.0, 100.0, 1.0);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.textFromValueFunction = [] (double v)
            {
                const int n = juce::roundToInt (v);
                return n == 0 ? juce::String ("C") : (n < 0 ? "L" + juce::String (-n) : "R" + juce::String (n));
            };
            pan.setValue (entry.pan * 100.0, juce::dontSendNotification);
            pan.updateText();
            bindApplyOnRelease (pan, [this] { entry.pan = (float) pan.getValue() / 100.0f; });
            addAndMakeVisible (pan);
        }
        else if (entry.kind == Entry::Kind::rowLink)
        {
            map.addItemList ({ "Stereo", "Mono sum", "Left only", "Right only", "Swap L/R" }, 1);
            map.setSelectedItemIndex ((int) entry.map, juce::dontSendNotification);
            map.onChange = [this]
            {
                entry.map = (LaneLink::ChannelMap) map.getSelectedItemIndex();
                onChange (index, entry);
            };
            addAndMakeVisible (map);
        }

        invert.setButtonText (juce::CharPointer_UTF8 ("\xc3\x98")); // the polarity glyph
        invert.setClickingTogglesState (true);
        invert.setToggleState (entry.invert, juce::dontSendNotification);
        invert.setTooltip ("Invert polarity");
        invert.onClick = [this]
        {
            entry.invert = invert.getToggleState();
            onChange (index, entry);
        };
        addAndMakeVisible (invert);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 6);
        nameLabel.setBounds (area.removeFromLeft (nameWidth));
        area.removeFromLeft (gap);
        invert.setBounds (area.removeFromRight (invertWidth));
        area.removeFromRight (gap);
        auto extra = area.removeFromRight (extraWidth);
        if (pan.isVisible())
            pan.setBounds (extra);
        if (map.isVisible())
            map.setBounds (extra);
        area.removeFromRight (gap);
        level.setBounds (area);
    }

private:
    /** A drag applies once, on release (each apply rebuilds the graph); a tap
        on the track or a double-click reset applies at once. */
    void bindApplyOnRelease (juce::Slider& s, std::function<void()> store)
    {
        s.onValueChange = [this, &s, store]
        {
            store();
            if (! s.isMouseButtonDown())
                onChange (index, entry);
        };
        s.onDragEnd = [this, store]
        {
            store();
            onChange (index, entry);
        };
    }

    int index;
    Entry entry;
    std::function<void (int, const Entry&)> onChange;
    juce::Label nameLabel;
    juce::Slider level, pan;
    juce::ComboBox map;
    juce::TextButton invert;
};

RouteEditorCard::RouteEditorCard (const juce::String& title, std::vector<Entry> entries,
                                  std::function<void (int, const Entry&)> onChange, std::function<void()> onDone)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    for (auto* header : { &levelHeader, &extraHeader, &invertHeader })
    {
        header->setFont (juce::Font (12.0f, juce::Font::bold));
        header->setColour (juce::Label::textColourId, theme::current().textMuted);
        addAndMakeVisible (*header);
    }
    levelHeader.setText ("LEVEL", juce::dontSendNotification);
    extraHeader.setText ("BALANCE / CHANNELS", juce::dontSendNotification);
    invertHeader.setText ("PHASE", juce::dontSendNotification);
    invertHeader.setJustificationType (juce::Justification::centred);

    for (int i = 0; i < (int) entries.size(); ++i)
        content.addAndMakeVisible (lines.add (new Line (i, entries[(size_t) i], onChange)));

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // Copied to the stack first: the handler pops this card, destroying the button and this lambda.
    doneButton.onClick = [onDone] { if (auto callback = onDone) callback(); };
    addAndMakeVisible (doneButton);

    const int visibleLines = juce::jlimit (1, 6, (int) entries.size());
    setSize (760, 14 + 30 + 18 + visibleLines * lineHeight + 10 + touch::minTapTarget + 14);
}

RouteEditorCard::~RouteEditorCard()
{
    viewport.setViewedComponent (nullptr, false);
}

void RouteEditorCard::resized()
{
    auto area = getLocalBounds().reduced (14);
    titleLabel.setBounds (area.removeFromTop (30));

    // Column headers over the same columns Line::resized() lays out.
    auto headers = area.removeFromTop (18);
    headers.removeFromLeft (nameWidth + gap);
    invertHeader.setBounds (headers.removeFromRight (invertWidth));
    headers.removeFromRight (gap);
    extraHeader.setBounds (headers.removeFromRight (extraWidth));
    headers.removeFromRight (gap);
    levelHeader.setBounds (headers);

    auto bottom = area.removeFromBottom (touch::minTapTarget);
    doneButton.setBounds (bottom.removeFromRight (140));
    area.removeFromBottom (10);

    viewport.setBounds (area);
    const int width = area.getWidth() - (lines.size() > 6 ? viewport.getScrollBarThickness() : 0);
    content.setSize (width, lines.size() * lineHeight);
    for (int i = 0; i < lines.size(); ++i)
        lines[i]->setBounds (0, i * lineHeight, width, lineHeight);
}

void RouteEditorCard::paint (juce::Graphics& g)
{
    g.fillAll (theme::current().surface);
}

} // namespace openguitarmultifx
