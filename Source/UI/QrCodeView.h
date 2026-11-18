#pragma once

#include <qrcodegen.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace openguitarmultifx
{

/**
    Draws a QR code for a short text (a URL). Encoded once in setText(), painted as plain rectangles -- no image, so it
    stays sharp at any size and scale factor.

    Always dark modules on a white square with the 4-module quiet zone the spec asks for, whatever the theme: phone
    cameras read inverted (light-on-dark) codes badly, so the colours deliberately ignore the theme. Medium error
    correction (15%) is plenty on a screen and keeps a ~60-character URL at version 4 (33x33), big modules.
*/
class QrCodeView : public juce::Component
{
public:
    void setText (const juce::String& text)
    {
        if (text == encodedText)
            return;
        encodedText = text;
        code.reset();
        if (text.isNotEmpty())
        {
            try
            {
                code = qrcodegen::QrCode::encodeText (text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
            }
            catch (const std::exception&) // only "data too long", impossible for a login URL
            {
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! code)
            return;

        constexpr int quietZone = 4;
        const int modules = code->getSize() + 2 * quietZone;
        const int side = juce::jmin (getWidth(), getHeight());
        // Whole pixels per module: fractional modules blur into each other and some phone readers then give up.
        const int cell = juce::jmax (1, side / modules);
        const int drawn = cell * modules;
        const int x0 = (getWidth() - drawn) / 2, y0 = (getHeight() - drawn) / 2;

        g.setColour (juce::Colours::white);
        g.fillRect (x0, y0, drawn, drawn);
        g.setColour (juce::Colours::black);

        juce::RectangleList<int> dark;
        for (int y = 0; y < code->getSize(); ++y)
            for (int x = 0; x < code->getSize(); ++x)
                if (code->getModule (x, y))
                    dark.addWithoutMerging ({ x0 + (x + quietZone) * cell, y0 + (y + quietZone) * cell, cell, cell });
        g.fillRectList (dark);
    }

private:
    juce::String encodedText;
    std::optional<qrcodegen::QrCode> code;
};

} // namespace openguitarmultifx
