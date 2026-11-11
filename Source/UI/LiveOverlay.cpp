#include "LiveOverlay.h"

#include "OverlayChrome.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "TouchSizing.h"

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr int sceneSlots = 8; // mirrors snapshotxml::maxSnapshots

    juce::Colour inkOn (juce::Colour fill)
    {
        return theme::inkOn (fill, juce::Colours::black, juce::Colours::white);
    }
}

LiveOverlay::LiveOverlay()
{
    if (auto* laf = dynamic_cast<OpenGuitarMultiFxLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        displayTypeface = laf->getExtraBoldTypeface();

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onClose) onClose(); };

    addAndMakeVisible (tunerButton);
    tunerButton.text = "TUNER";
    tunerButton.onClick = [this] { if (onTunerTapped) onTunerTapped(); };

    addAndMakeVisible (bpmValueLabel);
    bpmValueLabel.setJustificationType (juce::Justification::centred);
    bpmValueLabel.setFont (juce::Font (44.0f, juce::Font::bold));
    addAndMakeVisible (bpmUnitLabel);
    bpmUnitLabel.setText ("BPM", juce::dontSendNotification);
    bpmUnitLabel.setJustificationType (juce::Justification::centred);
    bpmUnitLabel.setFont (juce::Font (15.0f));

    addAndMakeVisible (captionLabel);
    captionLabel.setVisible (false);
    captionLabel.setText ("A-H are the preset's scenes - tap one to switch. Learned footswitch CCs (scene pedals) recall the same scenes.",
                          juce::dontSendNotification);
    captionLabel.setJustificationType (juce::Justification::centred);
    captionLabel.setFont (juce::Font (14.0f));
    captionLabel.setColour (juce::Label::textColourId, theme::current().textMuted);

    for (int i = 0; i < sceneSlots; ++i)
    {
        auto tile = std::make_unique<SceneTile>();
        tile->letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
        tile->onClick = [this, i] { if (onSceneSelected) onSceneSelected (i); };
        addAndMakeVisible (*tile);
        tiles[(size_t) i] = std::move (tile);
    }

    // Big on purpose: this is the stage screen. OverlayHost clamps it to
    // 92% x 90% of the window, which on the target 10" display is nearly
    // the whole screen.
    setSize (1180, 720);
}

LiveOverlay::~LiveOverlay()
{
    if (onDestroyed)
        onDestroyed();
}

juce::Font LiveOverlay::displayFont (float size) const
{
    return displayTypeface != nullptr
               ? juce::Font (juce::FontOptions (size).withTypeface (displayTypeface))
               : juce::Font (size, juce::Font::bold);
}

void LiveOverlay::setPresetInfo (juce::String badge, juce::String title, juce::String subtitle)
{
    presetBadge = std::move (badge);
    presetTitle = std::move (title);
    presetSubtitle = std::move (subtitle);
    repaint();
}

void LiveOverlay::setScenes (const std::array<juce::String, 8>& sceneNamesIn,
                             const std::array<juce::Colour, 8>& sceneColoursIn,
                             int count, int currentIndex)
{
    sceneNames = sceneNamesIn;
    sceneColours = sceneColoursIn;
    sceneCount = juce::jlimit (0, sceneSlots, count);
    currentScene = juce::jlimit (0, juce::jmax (0, sceneCount - 1), currentIndex);
    refreshTiles();
}

void LiveOverlay::setBpm (double bpmIn)
{
    if (std::abs (bpmIn - bpm) < 0.5)
        return;
    bpm = bpmIn;
    bpmValueLabel.setText (juce::String (bpm, bpm < 100.0 ? 1 : 0), juce::dontSendNotification);
}

void LiveOverlay::refreshTiles()
{
    for (int i = 0; i < sceneSlots; ++i)
    {
        auto& tile = tiles[(size_t) i];
        tile->name = sceneNames[(size_t) i];
        tile->colour = sceneColours[(size_t) i];
        tile->active = i == currentScene && sceneCount > 0;
        tile->setEnabled (i < sceneCount);
        tile->setButtonText (tile->name.isNotEmpty() ? tile->name : tile->letter);
        tile->repaint();
    }
}

void LiveOverlay::resized()
{
    auto area = getLocalBounds().reduced (28, 28);

    auto top = area.removeFromTop (84);
    closeButton.setBounds (top.removeFromRight (84).withSizeKeepingCentre (64, 64));
    tunerButton.setBounds (top.removeFromRight (188).withSizeKeepingCentre (188, 64));

    auto bpmArea = top.removeFromRight (150);
    auto bpmValueBox = bpmArea.removeFromTop (bpmArea.getHeight() * 3 / 5).toNearestInt();
    bpmValueLabel.setBounds (bpmValueBox);
    bpmUnitLabel.setBounds (bpmArea);

    presetArea = top.toFloat();

    area.removeFromTop (24);
    const int gap = 14;
    const int tileWidth = (area.getWidth() - gap * 3) / 4;
    const int tileHeight = (area.getHeight() - gap) / 2;
    for (int i = 0; i < sceneSlots; ++i)
    {
        const int col = i % 4;
        const int row = i / 4;
        tiles[(size_t) i]->setBounds (area.getX() + col * (tileWidth + gap),
                                      area.getY() + row * (tileHeight + gap),
                                      tileWidth, tileHeight);
    }
}

void LiveOverlay::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.background);

    // ---- the preset tile: the one thing readable from across the stage ----
    const auto area = presetArea;
    g.setColour (t.raised);
    g.fillRoundedRectangle (area, t.radius);
    g.setColour (t.lineStrong.withAlpha (0.6f));
    g.drawRoundedRectangle (area.reduced (0.5f), t.radius, 1.0f);

    const float pad = 14.0f;
    const auto chip = area.withSizeKeepingCentre (64.0f, juce::jmin (64.0f, area.getHeight() - 2 * pad));

    // The chip carries the preset number + live scene letter, in the live
    // scene's colour -- the same accent the rest of the app wears.
    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    g.setColour (accent);
    g.fillRoundedRectangle (chip, t.smallRadius);
    g.setColour (inkOn (accent));
    g.setFont (displayFont (juce::jmin (30.0f, chip.getHeight() * 0.42f)));
    g.drawText (presetBadge, chip.toNearestInt(), juce::Justification::centred);

    auto textArea = area.withTrimmedLeft (chip.getRight() - area.getX() + 18.0f)
                       .withTrimmedRight (pad)
                       .withTrimmedTop (pad)
                       .withTrimmedBottom (pad);

    // Title takes the upper ~60%, subtitle the lower 40% -- big type first.
    auto titleBox = textArea.removeFromTop (textArea.getHeight() * 0.6f).reduced (0.0f, 2.0f);
    g.setColour (t.text);
    g.setFont (displayFont (juce::jmin (28.0f, titleBox.getHeight() * 0.62f)));
    g.drawText (presetTitle, titleBox.toNearestInt(), juce::Justification::centredLeft);

    g.setColour (t.textMuted);
    g.setFont (juce::Font (juce::jmin (15.0f, textArea.getHeight() * 0.55f)));
    g.drawText (presetSubtitle, textArea.toNearestInt(), juce::Justification::centredLeft);
}

void LiveOverlay::SceneTile::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    auto bounds = getLocalBounds().toFloat();
    const bool enabled = isEnabled();
    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    const bool boldActive = active && t.id == theme::Id::boldBlocks;

    auto fill = active ? (boldActive ? accent : t.raisedHigh) : t.raised;
    auto border = active ? accent : (enabled ? colour.withAlpha (0.55f) : t.lineStrong.withAlpha (0.45f));
    auto labelColour = active ? (boldActive ? juce::Colours::black : accent) : (enabled ? t.textMuted : t.textDim);
    auto nameColour = active ? (boldActive ? juce::Colours::black : t.text) : (enabled ? t.text : t.textDim);
    const float alpha = enabled ? 1.0f : 0.42f;

    g.setColour (fill.withAlpha (alpha));
    g.fillRoundedRectangle (bounds, t.radius);

    if (t.id == theme::Id::console && active)
    {
        g.setColour (accent.withAlpha (0.9f * alpha));
        g.fillRoundedRectangle (bounds.removeFromTop (5.0f), t.smallRadius);
    }

    const auto ledCentre = juce::Point<float> (bounds.getRight() - 18.0f, bounds.getY() + 18.0f);
    if (active)
    {
        g.setColour (accent.withAlpha (0.32f * alpha));
        g.fillEllipse (ledCentre.x - 10.0f, ledCentre.y - 10.0f, 20.0f, 20.0f);
        g.setColour (accent.brighter (0.35f).withAlpha (alpha));
        g.fillEllipse (ledCentre.x - 5.0f, ledCentre.y - 5.0f, 10.0f, 10.0f);
    }
    else
    {
        g.setColour ((enabled ? t.lineStrong : t.line).withAlpha (alpha));
        g.fillEllipse (ledCentre.x - 5.0f, ledCentre.y - 5.0f, 10.0f, 10.0f);
    }

    g.setColour (border.withAlpha (alpha));
    g.drawRoundedRectangle (bounds.reduced (1.0f), t.radius, active ? 3.0f : 1.5f);

    g.setColour (labelColour.withAlpha (alpha));
    g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
    g.drawText (letter, bounds.withTrimmedLeft (16.0f).withTrimmedRight (bounds.getWidth() - 34.0f)
                         .withTrimmedTop (12.0f).withHeight (22.0f).toNearestInt(),
                juce::Justification::centredLeft);

    auto name = juce::String (getButtonText());
    if (name.isEmpty())
        name = letter;
    if (t.uppercaseLabels && ! active)
        name = name.toUpperCase();
    g.setColour (nameColour.withAlpha (alpha));
    g.setFont (juce::FontOptions ((int) juce::jmin (34.0f, bounds.getHeight() * 0.22f)).withStyle ("Bold"));
    g.drawText (name, bounds.withTrimmedTop (bounds.getHeight() * 0.38f).withTrimmedLeft (16.0f)
                         .withTrimmedRight (16.0f).withTrimmedBottom (16.0f).toNearestInt(),
                juce::Justification::centred);

    if ((highlighted || down) && enabled)
    {
        g.setColour (t.lineStrong.withAlpha (down ? 0.8f : 0.45f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, 2.0f);
    }
}

} // namespace openguitarmultifx
