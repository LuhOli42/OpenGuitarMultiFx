#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/** Prototype .fbtn: raised cell, hairline border, muted caps label and a
    bold value. Content is painted by FooterBar because the three buttons
    carry different readouts (note/BPM/TAP). */
class FooterButton : public juce::Button
{
public:
    FooterButton() : juce::Button ({}) {}

    std::function<void (juce::Graphics&, juce::Rectangle<float>)> contentPainter;

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;
};

class FooterBar : public juce::Component
{
public:
    FooterBar();

    void resized() override;
    void paint (juce::Graphics& g) override;

    void setLevels (float inLevel, float outLevel);
    double getBpm() const noexcept { return bpm; }

    std::function<void()> onTunerTapped;
    std::function<void()> onMasterEqTapped;
    std::function<void (float)> onMasterGainChanged;
    void setMasterGainDb (float db);
    void setActiveSnapshot (const juce::String& label, juce::Colour colour);

private:
    void tapTempo();
    void updateMasterReadout();

    FooterButton tunerButton, eqButton, tapButton;
    juce::Slider masterSlider;
    juce::Label masterTitleLabel { {}, "Master" };
    juce::Label masterValueLabel;

    float inLevel = 0.0f, outLevel = 0.0f;
    double bpm = 120.0;
    double lastTapMs = 0.0;
    std::vector<double> recentTapIntervalsMs;
    float masterDb = 0.0f;

    juce::String snapshotLabel;
    juce::Colour snapshotColour;
    juce::Rectangle<float> inMeterBounds, outMeterBounds;

    void drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level,
                              const juce::String& label) const;
};

} // namespace openguitarmultifx
