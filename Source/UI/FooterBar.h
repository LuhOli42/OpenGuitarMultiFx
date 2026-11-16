#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/** Prototype .fbtn: raised cell, hairline border, muted caps label and a
    bold value. Content is painted by FooterBar because the three buttons
    carry different readouts (note/BPM/TAP). */
class FooterButton : public juce::Button,
                     private juce::Timer
{
public:
    FooterButton() : juce::Button ({}) {}

    std::function<void (juce::Graphics&, juce::Rectangle<float>)> contentPainter;
    /** Held for longPressMs without lifting: fires this and swallows the click that the release would have been. */
    std::function<void()> onLongPress;
    static constexpr int longPressMs = 650;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onLongPress != nullptr)
            startTimer (longPressMs);
        juce::Button::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        juce::Button::mouseUp (e);
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    void timerCallback() override
    {
        stopTimer();
        setState (juce::Button::buttonNormal); // the release no longer counts as a click
        if (onLongPress != nullptr)
            onLongPress();
    }
};

class FooterBar : public juce::Component,
                  private juce::Timer
{
public:
    FooterBar();

    void resized() override;
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void lookAndFeelChanged() override;

    void setLevels (float inLevel, float outLevel);

    /** Tap tempo from outside the screen (a MIDI footswitch): the same as pressing TAP. */
    void pressTap() { tapTempo(); }
    /** Holding TAP asks MainComponent to arm / clear the tap pedal; it then shows the pedal here (TAP pill reads "CC 24",
        or "..." while waiting for the stomp). */
    std::function<void()> onTapLongPress;
    void setTapPedal (int cc, bool armed);
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
    int tapCc = -1;
    bool tapArmed = false;
    juce::Slider masterSlider;
    juce::Label masterTitleLabel { {}, "Master" };
    juce::Label masterValueLabel;

    void timerCallback() override;
    float inLevel = 0.0f, outLevel = 0.0f;           // what is drawn (eased every frame)
    float inTarget = 0.0f, outTarget = 0.0f;         // latest reading from the engine (arrives ~20 times a second)
    double lastFrameMs = 0.0;
    double bpm = 120.0;
    double lastTapMs = 0.0;
    std::vector<double> recentTapIntervalsMs;
    float masterDb = 0.0f;

    juce::String snapshotLabel;
    juce::Colour snapshotColour;
    juce::Rectangle<float> inMeterBounds, outMeterBounds;
    juce::Rectangle<float> masterCardBounds; // prototype: Master is an .fbtn card like Tuner / Master EQ

    void drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level,
                              const juce::String& label) const;
};

} // namespace openguitarmultifx
