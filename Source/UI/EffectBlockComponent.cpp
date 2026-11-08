#include "EffectBlockComponent.h"
#include "Theme.h"

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
    if (! isDragging && event.getDistanceFromDragStart() > 4)
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
    // Rendered at 2x the largest on-screen size (~100px icon on a 122px
    // block) so the tinted draw stays crisp on the 10" panel's DPI.
    constexpr int size = 128;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);

    // Every cab-capable amp draws amp.svg (head+cabinet); with its Cab
    // selector Off it is just a head -- same rule the old direct-draw
    // paint() used.
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
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    const bool bypassed = processor.isBypassed();
    const auto accent = categoryColour;
    const auto& t = theme::current();

    const bool filledStyle = t.blockStyle == theme::BlockStyle::filled;
    const bool stripeStyle = t.blockStyle == theme::BlockStyle::topStripe;

    // --- Card body -------------------------------------------------------
    if (filledStyle)
    {
        // Bold Blocks: the whole block IS the category colour while active;
        // bypass drops back to a surface card with a coloured outline.
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
        // Console: flat graphite, small corners.
        g.setColour (bypassed ? t.surface : t.raised);
        g.fillRoundedRectangle (bounds, t.smallRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (bounds, t.smallRadius, 1.0f);

        // The 7px category stripe on top replaces the plain top border --
        // clipped to the rounded card so its ends follow the corner radius.
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
        // Stage Glass: dark glass card with a light vertical gradient.
        g.setGradientFill (juce::ColourGradient (t.raised, bounds.getX(), bounds.getY(),
                                                 t.surface, bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, t.radius);
        g.setColour (t.line);
        g.drawRoundedRectangle (bounds, t.radius, 1.0f);
    }

    // --- Selection -------------------------------------------------------
    if (selected)
    {
        if (filledStyle)
        {
            // Double ring: a background-coloured gap, then the text ring --
            // reads as a sticker outline on any category colour.
            g.setColour (t.background);
            g.drawRoundedRectangle (bounds.expanded (3.0f), t.radius + 3.0f, 3.0f);
            g.setColour (t.text);
            g.drawRoundedRectangle (bounds.expanded (6.0f), t.radius + 6.0f, 3.0f);
        }
        else if (stripeStyle)
        {
            // White outline floating just off the card.
            g.setColour (t.text.withAlpha (0.9f));
            g.drawRoundedRectangle (bounds.expanded (2.0f), t.smallRadius + 2.0f, 2.0f);
        }
        else
        {
            // Category-colour border plus a soft outer glow approximation.
            g.setColour (accent.withAlpha (0.22f));
            g.drawRoundedRectangle (bounds.expanded (4.0f), t.radius + 4.0f, 6.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (bounds, t.radius, 2.0f);
        }
    }

    // --- Stage Glass LED (top-left) --------------------------------------
    if (t.blockStyle == theme::BlockStyle::glow)
    {
        const auto ledCentre = juce::Point<float> (bounds.getX() + 9.0f, bounds.getY() + 9.0f);
        if (bypassed)
        {
            g.setColour (t.textDim);
            g.fillEllipse (ledCentre.x - 3.0f, ledCentre.y - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            g.setColour (accent.withAlpha (0.28f));
            g.fillEllipse (ledCentre.x - 7.0f, ledCentre.y - 7.0f, 14.0f, 14.0f);
            g.setColour (accent);
            g.fillEllipse (ledCentre.x - 3.5f, ledCentre.y - 3.5f, 7.0f, 7.0f);
        }
    }

    // --- Icon + name -----------------------------------------------------
    auto iconArea = bounds.reduced (6.0f);
    // Keep clear of the LED dot on Stage Glass.
    if (t.blockStyle == theme::BlockStyle::glow)
        iconArea = iconArea.withTrimmedLeft (10.0f);
    auto nameArea = iconArea.removeFromBottom (26.0f);
    iconArea.removeFromBottom (2.0f);

    const float squareSize = juce::jmin (iconArea.getWidth(), iconArea.getHeight()) * 0.82f;
    const auto squareIconArea = iconArea.withSizeKeepingCentre (squareSize, squareSize);

    // Icon tint per theme: Stage Glass tints with the category colour (dim
    // when bypassed), Console keeps it in the text colour, Bold Blocks puts
    // dark ink on the filled card and the category colour on the bypass
    // outline.
    juce::Colour iconTint;
    bool wantGlowIcon = false;
    if (filledStyle)
        iconTint = bypassed ? accent : accent.contrasting (juce::Colours::black, juce::Colours::white);
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
        glowIcon = wantGlowIcon ? tintIconImage (baseIcon, iconTint, 0.25f) : juce::Image();
    }

    if (glowIcon.isValid())
        g.drawImage (glowIcon, squareIconArea.expanded (7.0f), juce::RectanglePlacement::centred, false);
    g.drawImage (tintedIcon, squareIconArea, juce::RectanglePlacement::centred, false);

    // Name: Console and Bold Blocks set it in caps (theme token), Stage
    // Glass keeps the processor's own casing. Bold Blocks reuses the icon
    // tint (ink on the fill, category colour on the bypass outline).
    juce::String name = processor.getName();
    if (t.uppercaseLabels)
        name = name.toUpperCase();

    g.setColour (filledStyle ? iconTint : (bypassed ? t.textDim : t.text));
    g.setFont (18.0f);
    g.drawFittedText (name, nameArea.toNearestInt(), juce::Justification::centred, 2);

    if (pedalCc >= 0 || pedalArmed)
    {
        g.setColour (pedalArmed ? juce::Colours::orange
                                : (filledStyle ? iconTint.withAlpha (0.8f) : juce::Colours::white.withAlpha (bypassed ? 0.5f : 0.8f)));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (pedalArmed ? juce::String ("...") : "CC" + juce::String (pedalCc),
                    bounds.reduced (5.0f, 3.0f).toNearestInt(), juce::Justification::topRight);
    }
}

} // namespace openguitarmultifx
