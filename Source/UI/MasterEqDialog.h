#pragma once

#include "../Engine/MasterEq.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    Master-EQ editor card -- a ReaEQ/FabFilter-style free-form parametric EQ on
    the output bus (user request 2026-10-11: "um botao q entra num master eq,
    que seria q nem um fabfilter eq, ou reaeq do reaper"). Pushed onto
    OverlayHost from the footer's EQ button, like every other card.

    The response curve is the main interaction surface: each band is a node on
    the summed curve (the node y-position IS the combined response at that
    frequency, like Pro-Q). Drag horizontally for frequency, vertically for
    gain on gain-carrying types; mouse wheel on a node trims its Q; double-tap
    toggles the band off. A strip below edits the selected band's numbers
    directly and a row of add-buttons creates bands of each type.

    Knows nothing about audio: it owns a working copy of the band list and
    reports every edit through onBandsChanged -- MainComponent pushes it to
    AudioEngine::setMasterEqBands() and persists it (MasterBusSettings.h).
*/
class MasterEqDialog : public juce::Component
{
public:
    MasterEqDialog();
    ~MasterEqDialog() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    /** Initial state (the saved band list). */
    void setBands (const std::vector<MasterEqBand>& newBands);
    const std::vector<MasterEqBand>& getBands() const noexcept { return bands; }

    std::function<void()> onPopOverlay;
    std::function<void (const std::vector<MasterEqBand>&)> onBandsChanged;
    /** The running sample rate for response-curve maths (0 -> 48 kHz assumed). */
    std::function<double()> getSampleRate;

private:
    // Graph coordinate mapping (log-frequency 20 Hz .. 20 kHz, linear dB).
    static constexpr double minFreq = 20.0, maxFreq = 20000.0;
    static constexpr float minDb = -24.0f, maxDb = 24.0f;

    double freqAtX (float x) const noexcept;
    float xAtFreq (double freq) const noexcept;
    float yAtDb (float db) const noexcept;
    float dbAtY (float y) const noexcept;
    juce::Point<float> nodePosition (int bandIndex) const noexcept;
    int bandAt (juce::Point<float> pos) const noexcept;
    juce::Colour bandColour (int bandIndex) const noexcept;

    bool typeHasGain (MasterEqBand::Type t) const noexcept { return t == MasterEqBand::Type::lowShelf || t == MasterEqBand::Type::peak || t == MasterEqBand::Type::highShelf; }
    bool typeIsCut (MasterEqBand::Type t) const noexcept { return t == MasterEqBand::Type::lowCut || t == MasterEqBand::Type::highCut; }

    void addBand (MasterEqBand::Type type);
    void deleteBand (int index);
    void selectBand (int index);
    void bandsEdited();
    void refreshSelectedStrip();
    void paintGraph (juce::Graphics& g, juce::Rectangle<float> area);
    void paintNode (juce::Graphics& g, int bandIndex);

    double curveDbAt (double freq, int skipBand = -1) const noexcept;
    double effectiveSampleRate() const noexcept
    {
        const double sr = getSampleRate != nullptr ? getSampleRate() : 0.0;
        return sr > 1000.0 ? sr : 48000.0;
    }

    std::vector<MasterEqBand> bands;
    int selectedBand = -1;
    int draggedBand = -1;

    juce::Rectangle<float> graphArea;

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::TextButton closeButton { "Close" };
    juce::TextButton clearButton { "Clear" };

    // "+ band" row, one per type, in typeForIndex() order.
    std::array<juce::TextButton, 6> addButtons { juce::TextButton { "+" }, juce::TextButton { "+" }, juce::TextButton { "+" },
                                                 juce::TextButton { "+" }, juce::TextButton { "+" }, juce::TextButton { "+" } };

    // Selected-band strip.
    juce::Label selectedLabel;
    juce::TextButton bandEnabledButton;
    juce::TextButton bandDeleteButton { "Delete" };
    juce::Label freqLabel, gainLabel, qLabel;
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label noSelectionLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEqDialog)
};

} // namespace openguitarmultifx
