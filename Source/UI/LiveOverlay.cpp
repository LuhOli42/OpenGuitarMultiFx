#include "LiveOverlay.h"

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
        return fill.contrasting (juce::Colours::black, juce::Colours::white);
    }
}

LiveOverlay::LiveOverlay()
{
    if (auto* laf = dynamic_cast<OpenGuitarMultiFxLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        displayTypeface = laf->getExtraBoldTypeface();

    addAndMakeVisible (closeButton);
    closeButton.setTooltip ("Back to the grid");
    closeButton.onClick = [this] { if (onClose) onClose(); };

    addAndMakeVisible (tunerButton);
    tunerButton.setTooltip ("Open the tuner");
    tunerButton.onClick = [this] { if (onTunerTapped) onTunerTapped(); };

    addAndMakeVisible (bpmValueLabel);
    bpmValueLabel.setJustificationType (juce::Justification::centred);
    bpmValueLabel.setFont (juce::Font (44.0f, juce::Font::bold));
    addAndMakeVisible (bpmUnitLabel);
    bpmUnitLabel.setText ("BPM", juce::dontSendNotification);
    bpmUnitLabel.setJustificationType (juce::Justification::centred);
    bpmUnitLabel.setFont (juce::Font (15.0f));

    addAndMakeVisible (captionLabel);
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
    // Vertically centred block: header, scene row, caption -- the stage
    // screen breathes instead of stretching its tiles across the panel.
    const int headerHeight = 128;
    const int tileHeight = juce::jmin (220, juce::jmax (140, getHeight() - 40 - headerHeight - 14 - 12 - 24));
    const int contentHeight = headerHeight + 14 + tileHeight + 12 + 24;
    const int top = juce::jmax (20, (getHeight() - contentHeight) / 2);

    auto area = getLocalBounds().withY (top).withHeight (contentHeight).reduced (28, 0);

    // ---- header: preset tile (painted) + TUNER + BPM + close ----
    auto header = area.removeFromTop (headerHeight);

    auto closeArea = header.removeFromRight (64);
    closeButton.setBounds (closeArea.withSizeKeepingCentre (64, touch::minTapTarget));
    header.removeFromRight (10);

    auto tunerArea = header.removeFromRight (170);
    tunerButton.setBounds (tunerArea.withSizeKeepingCentre (170, touch::minTapTarget));
    header.removeFromRight (10);

    auto bpmArea = header.removeFromRight (150);
    auto bpmValueBox = bpmArea.removeFromTop (bpmArea.getHeight() * 3 / 5).toNearestInt();
    bpmValueLabel.setBounds (bpmValueBox);
    bpmUnitLabel.setBounds (bpmArea);
    header.removeFromRight (10);

    presetArea = header.toFloat();

    // ---- scene tiles ----
    area.removeFromTop (14);
    const int gap = 10;
    const int tileWidth = (area.getWidth() - gap * (sceneSlots - 1)) / sceneSlots;
    for (int i = 0; i < sceneSlots; ++i)
        tiles[(size_t) i]->setBounds (area.getX() + i * (tileWidth + gap), area.getY(), tileWidth, tileHeight);

    // ---- caption ----
    area.removeFromTop (tileHeight + 12);
    captionLabel.setBounds (area.removeFromTop (24));
    captionLabel.setColour (juce::Label::textColourId, theme::current().textMuted);
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
    const auto chip = area.withSizeKeepingCentre (juce::jmin (96.0f, area.getHeight() - 2 * pad),
                                                  juce::jmin (96.0f, area.getHeight() - 2 * pad));

    // The chip carries the preset number + live scene letter, in the live
    // scene's colour -- the same accent the rest of the app wears.
    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    g.setColour (accent);
    g.fillRoundedRectangle (chip, t.smallRadius);
    g.setColour (inkOn (accent));
    g.setFont (displayFont (juce::jmin (34.0f, chip.getHeight() * 0.4f)));
    g.drawText (presetBadge, chip.toNearestInt(), juce::Justification::centred);

    auto textArea = area.withTrimmedLeft (chip.getRight() - area.getX() + 18.0f)
                       .withTrimmedRight (pad)
                       .withTrimmedTop (pad)
                       .withTrimmedBottom (pad);

    // Title takes the upper ~60%, subtitle the lower 40% -- big type first.
    auto titleBox = textArea.removeFromTop (textArea.getHeight() * 0.6f).reduced (0.0f, 2.0f);
    g.setColour (t.text);
    g.setFont (displayFont (juce::jmin (44.0f, titleBox.getHeight() * 0.78f)));
    g.drawText (presetTitle, titleBox.toNearestInt(), juce::Justification::centredLeft);

    g.setColour (t.textMuted);
    g.setFont (juce::Font (juce::jmin (20.0f, textArea.getHeight() * 0.8f)));
    g.drawText (presetSubtitle, textArea.toNearestInt(), juce::Justification::centredLeft);
}

void LiveOverlay::SceneTile::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const bool enabled = isEnabled();

    const auto borderColour = enabled ? colour.withAlpha (active ? 1.0f : 0.55f) : t.lineStrong.withAlpha (0.5f);

    g.setColour (active ? colour.withAlpha (0.22f) : t.raised);
    g.fillRoundedRectangle (bounds, t.radius);

    // LED, top-right corner: lit in the scene's colour on the live scene.
    const auto ledCentre = juce::Point<float> (bounds.getRight() - 16.0f, bounds.getY() + 16.0f);
    if (active)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (ledCentre.x - 9.0f, ledCentre.y - 9.0f, 18.0f, 18.0f);
        g.setColour (colour.brighter (0.4f));
        g.fillEllipse (ledCentre.x - 5.0f, ledCentre.y - 5.0f, 10.0f, 10.0f);
    }
    else
    {
        g.setColour (enabled ? t.lineStrong : t.line);
        g.fillEllipse (ledCentre.x - 5.0f, ledCentre.y - 5.0f, 10.0f, 10.0f);
    }

    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds.reduced (1.0f), t.radius, active ? 3.0f : 1.5f);

    // Letter chip, top-left.
    const float chipSize = juce::jmin (44.0f, bounds.getHeight() * 0.28f);
    const auto chip = juce::Rectangle<float> (bounds.getX() + 14.0f,
                                              bounds.getCentreY() - chipSize * 0.5f,
                                              chipSize, chipSize);
    if (enabled)
    {
        g.setColour (colour.withAlpha (active ? 1.0f : 0.8f));
        g.fillRoundedRectangle (chip, t.smallRadius);
        g.setColour (inkOn (colour));
        g.setFont (juce::Font (juce::FontOptions (chipSize * 0.5f).withStyle ("Bold")));
        g.drawText (letter, chip.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (chip, t.smallRadius);
        g.setColour (t.textDim);
        g.setFont (juce::Font (juce::FontOptions (chipSize * 0.5f).withStyle ("Bold")));
        g.drawText (letter, chip.toNearestInt(), juce::Justification::centred);
    }

    // Scene name under the chip, uppercase in Console/Bold Blocks.
    auto name = juce::String (getButtonText());
    if (t.uppercaseLabels)
        name = name.toUpperCase();
    g.setColour (enabled ? (active ? t.text : t.textMuted) : t.textDim);
    g.setFont (juce::Font (juce::FontOptions (juce::jmin (24.0f, bounds.getHeight() * 0.16f)).withStyle ("Bold")));
    g.drawText (name, bounds.withTrimmedTop (chip.getBottom() - bounds.getY() + 12.0f)
                         .withTrimmedLeft (10.0f)
                         .withTrimmedRight (10.0f),
                juce::Justification::centred);

    if ((highlighted || down) && enabled)
    {
        g.setColour (t.lineStrong.withAlpha (down ? 0.8f : 0.45f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, 2.0f);
    }
}

} // namespace openguitarmultifx
