#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    The prototype's .preset chip: preset number + snapshot letter in the
    accent tile, preset name, and bank/setlist subtitle. Custom-painted for
    the same reason as before -- Sora weights and the accent tile are not
    something juce::TextButton's LookAndFeel font cap can express.
*/
class PresetBadge : public juce::Component
{
public:
    /** 0 = no preset loaded. */
    void setNumber (int number);

    /** Which snapshot letter to draw after the number ("12C"): -1 hides it. */
    void setSnapshotIndex (int index);

    /** Preset name and optional bank/setlist subtitle. */
    void setPresetText (juce::String name, juce::String subtitle);

    /** The heavier display weight for the number tile -- see
        OpenGuitarMultiFxLookAndFeel::getExtraBoldTypeface(). Falls back to the
        current LookAndFeel's bold if never set. */
    void setDisplayTypeface (juce::Typeface::Ptr typeface);

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onClicked;

private:
    int currentNumber = 0;
    int snapshotIndex = -1;
    juce::String presetName, presetSubtitle;
    juce::Typeface::Ptr displayTypeface;
};

} // namespace openguitarmultifx
