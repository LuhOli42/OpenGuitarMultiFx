#include "EffectBlockComponent.h"
#include "Theme.h"
#include "TouchSizing.h"

#include "../Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

EffectBlockComponent::EffectBlockComponent (EffectProcessor& processorToShow, juce::Colour categoryColourIn)
    : processor (processorToShow), categoryColour (categoryColourIn)
{
    // Same `*_cab` convention the Add Effect browser and MainComponent::addEffect use.
    if (auto* params = processor.getParameters())
        for (auto* p : params->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p);
                f != nullptr && juce::String (f->paramID).endsWith ("_cab"))
                cabParam = f;
    paintedHeadOnly = showsHeadOnlyIcon();
}

bool EffectBlockComponent::showsHeadOnlyIcon() const
{
    return cabParam != nullptr && cabParam->get() < 0.5f; // index 0 = Off
}

void EffectBlockComponent::refreshIconVariant()
{
    if (showsHeadOnlyIcon() != paintedHeadOnly)
        repaint();
}

void EffectBlockComponent::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void EffectBlockComponent::setPedal (int cc, bool armed)
{
    if (pedalCc != cc || pedalArmed != armed)
    {
        pedalCc = cc;
        pedalArmed = armed;
        repaint();
    }
}

void EffectBlockComponent::mouseDown (const juce::MouseEvent& event)
{
    dragger.startDraggingComponent (this, event);
    isDragging = false; // only becomes true in mouseDrag, once past a small threshold
    longPressFired = false;
    if (onLongPressed != nullptr)
        startTimer (longPressMs);
}

void EffectBlockComponent::timerCallback()
{
    stopTimer();
    if (! isDragging && isMouseButtonDown() && onLongPressed != nullptr)
    {
        longPressFired = true;
        onLongPressed();
    }
}

void EffectBlockComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! isDragging && event.getDistanceFromDragStart() > 16)
    {
        stopTimer();
        isDragging = true;
        toFront (true); // stay above sibling blocks while dragging over them
    }

    if (isDragging)
        dragger.dragComponent (this, event, nullptr); // free 2D movement -- the chain now wraps into rows
}

void EffectBlockComponent::mouseUp (const juce::MouseEvent&)
{
    stopTimer();
    if (longPressFired)
    {
        longPressFired = false;
        return;
    }

    if (isDragging)
    {
        isDragging = false;
        if (onDragEnded != nullptr)
            onDragEnded (*this);
    }
    else if (onClicked != nullptr)
    {
        onClicked();
    }
}

void EffectBlockComponent::ensureIconImages()
{
    const int variant = showsHeadOnlyIcon() ? 1 : 0;
    if (iconCacheValid && iconVariantCached == variant)
        return;

    baseIcon = renderBaseIcon();
    iconVariantCached = variant;
    iconCacheValid = true;
    tintedIcon = juce::Image();
    glowIcon = juce::Image();
    tintedFor = juce::Colours::transparentBlack;
}

juce::Image EffectBlockComponent::renderBaseIcon() const
{
    constexpr int size = 128;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);

    if (showsHeadOnlyIcon())
    {
        static const std::unique_ptr<juce::Drawable> headIcon =
            icon::loadSvg (IconData::amp_head_svg, IconData::amp_head_svgSize);
        icon::drawSvg (g, juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size), headIcon.get());
    }
    else
    {
        processor.drawIcon (g, juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size));
    }

    return img;
}

juce::Image EffectBlockComponent::tintIconImage (const juce::Image& base, juce::Colour tint, float alphaScale)
{
    auto copy = base.createCopy();
    for (int y = 0; y < copy.getHeight(); ++y)
        for (int x = 0; x < copy.getWidth(); ++x)
        {
            const auto src = base.getPixelAt (x, y);
            if (src.getFloatAlpha() <= 0.0f)
                continue;

            copy.setPixelAt (x, y, tint.withMultipliedAlpha (src.getFloatAlpha() * alphaScale));
        }
    return copy;
}

void EffectBlockComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const bool bypassed = processor.isBypassed();
    const auto accent = categoryColour;
    const auto& t = theme::current();
    const bool filledStyle = t.blockStyle == theme::BlockStyle::filled;
    const bool stripeStyle = t.blockStyle == theme::BlockStyle::topStripe;

    if (filledStyle)
    {
        g.setColour (bypassed ? t.surface : accent);
        g.fillRoundedRectangle (bounds, t.radius);
        if (bypassed)
        {
            g.setColour (accent);
            g.drawRoundedRectangle (bounds, t.radius, 2.0f);
        }
    }
    else if (stripeStyle)
    {
        g.setColour (bypassed ? t.surface : t.raised);
        g.fillRoundedRectangle (bounds, t.smallRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (bounds, t.smallRadius, 1.0f);

        juce::Path clip;
        clip.addRoundedRectangle (bounds, t.smallRadius);
        g.saveState();
        g.reduceClipRegion (clip);
        g.setColour (bypassed ? t.raisedHigh : accent);
        g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 7.0f);
        g.restoreState();
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (t.raised, bounds.getX(), bounds.getY(),
                                                 t.surface, bounds.getRight(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, t.radius);
        g.setColour (t.line);
        g.drawRoundedRectangle (bounds, t.radius, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawLine (bounds.getX() + t.radius, bounds.getY() + 1.0f,
                    bounds.getRight() - t.radius, bounds.getY() + 1.0f, 1.0f);
    }

    if (selected)
    {
        if (filledStyle)
        {
            g.setColour (t.background);
            g.drawRoundedRectangle (bounds.expanded (3.0f), t.radius + 3.0f, 3.0f);
            g.setColour (t.text);
            g.drawRoundedRectangle (bounds.expanded (6.0f), t.radius + 6.0f, 3.0f);
        }
        else if (stripeStyle)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (bounds.expanded (2.0f), t.smallRadius + 2.0f, 2.0f);
        }
        else
        {
            g.setColour (accent.withAlpha (0.30f));
            g.drawRoundedRectangle (bounds.expanded (5.0f), t.radius + 5.0f, 7.0f);
            g.setColour (accent.withAlpha (0.18f));
            g.drawRoundedRectangle (bounds.expanded (9.0f), t.radius + 9.0f, 12.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (bounds.expanded (1.0f), t.radius + 1.0f, 2.0f);
        }
    }

    if (t.blockStyle == theme::BlockStyle::glow)
    {
        const auto ledCentre = juce::Point<float> (bounds.getX() + 14.0f, bounds.getY() + 14.0f);
        if (bypassed)
        {
            g.setColour (t.textDim);
            g.fillEllipse (ledCentre.x - 3.0f, ledCentre.y - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            // Prototype stage .blk:before: 7px dot + box-shadow 0 0 8px accent.
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (ledCentre.x - 6.5f, ledCentre.y - 6.5f, 13.0f, 13.0f);
            g.setColour (accent);
            g.fillEllipse (ledCentre.x - 3.5f, ledCentre.y - 3.5f, 7.0f, 7.0f);
        }
    }

    // Prototype .blk: flex column, centred group -- 48px icon, 6px gap,
    // name (12.5px bold, ~15px line). No pinning to the block's edges.
    const float squareSize = 34.0f;
    const float nameH = 15.0f;
    const float gap = 6.0f;
    const float groupH = squareSize + gap + nameH;
    const float groupTop = bounds.getCentreY() - groupH * 0.5f;
    const auto squareIconArea = juce::Rectangle<float> (bounds.getCentreX() - squareSize * 0.5f,
                                                        groupTop, squareSize, squareSize);
    const auto nameArea = juce::Rectangle<float> (bounds.getX() + 6.0f,
                                                  groupTop + squareSize + gap,
                                                  bounds.getWidth() - 12.0f, nameH * 2.0f);

    juce::Colour iconTint;
    bool wantGlowIcon = false;
    if (filledStyle)
        iconTint = bypassed ? accent : juce::Colour (0xff111111);
    else if (stripeStyle)
        iconTint = bypassed ? t.textDim : t.text;
    else
    {
        iconTint = bypassed ? t.textDim : accent;
        wantGlowIcon = ! bypassed;
    }

    ensureIconImages();
    if (! tintedIcon.isValid() || tintedFor != iconTint)
    {
        tintedIcon = tintIconImage (baseIcon, iconTint, 1.0f);
        tintedFor = iconTint;
        if (wantGlowIcon)
        {
            // Prototype: drop-shadow(0 0 8px accent 55%) on the icon -- a real
            // blurred glow, not stacked scaled copies (that's what read as
            // a bloom blob).
            auto tinted = tintIconImage (baseIcon, accent, 1.0f);
            glowIcon = juce::Image (juce::Image::ARGB, tinted.getWidth(), tinted.getHeight(), true);
            juce::Graphics glowGraphics (glowIcon);
            juce::GlowEffect glow;
            glow.setGlowProperties (8.0f, accent.withAlpha (0.55f));
            glow.applyEffect (tinted, glowGraphics, 1.0f, 1.0f);
        }
        else
            glowIcon = juce::Image();
    }

    if (glowIcon.isValid())
    {
        // GlowEffect.applyEffect() already paints the icon itself on top of
        // its own glow -- drawing tintedIcon again doubles the glyph.
        g.drawImage (glowIcon, squareIconArea.expanded (2.0f), juce::RectanglePlacement::centred, false);
    }
    else
        g.drawImage (tintedIcon, squareIconArea, juce::RectanglePlacement::centred, false);

    juce::String name = processor.getName();
    if (t.uppercaseLabels)
        name = name.toUpperCase();

    g.setColour (filledStyle ? iconTint : (bypassed ? t.textDim : t.text));
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (name, nameArea.toNearestInt(), juce::Justification::centred, 2, 0.8f);


    if (bypassed)
    {
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("OFF", bounds.reduced (5.0f, 3.0f).toNearestInt(), juce::Justification::bottomRight);
    }

    if (pedalCc >= 0 || pedalArmed)
    {
        g.setColour (pedalArmed ? juce::Colours::orange
                                : (filledStyle ? iconTint.withAlpha (0.8f) : juce::Colours::white.withAlpha (bypassed ? 0.5f : 0.8f)));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (pedalArmed ? juce::String ("...") : "CC" + juce::String (pedalCc),
                    bounds.reduced (5.0f, 3.0f).toNearestInt(), juce::Justification::topRight);
    }
}

} // namespace openguitarmultifx
