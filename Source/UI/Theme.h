#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace bm::ui
{
struct Palette
{
    bool dark;
    juce::Colour bg, panel, panelRaised, border, grid, text, textDim, accent, good, warn, bad, inputCol, outputCol;
};

inline const Palette& darkPalette()
{
    static const Palette p { true,
        juce::Colour (0xff0e1115), juce::Colour (0xff161a20), juce::Colour (0xff1e242c), juce::Colour (0xff2a323d),
        juce::Colour (0xff262d37), juce::Colour (0xffe9edf2), juce::Colour (0xff8794a5), juce::Colour (0xffe5484d),
        juce::Colour (0xff3ecf8e), juce::Colour (0xfff5a524), juce::Colour (0xffe5484d),
        juce::Colour (0xff6aa8ff), juce::Colour (0xffff8a5c) };
    return p;
}

inline const Palette& lightPalette()
{
    static const Palette p { false,
        juce::Colour (0xffeef1f5), juce::Colour (0xffffffff), juce::Colour (0xfff4f6f9), juce::Colour (0xffd3d9e1),
        juce::Colour (0xffe2e7ed), juce::Colour (0xff1a2029), juce::Colour (0xff5d6a7b), juce::Colour (0xffd13438),
        juce::Colour (0xff1a9d62), juce::Colour (0xffd98400), juce::Colour (0xffd13438),
        juce::Colour (0xff2a6fdb), juce::Colour (0xffe0602d) };
    return p;
}

// The editor sets this before it creates or repaints anything.
inline const Palette*& currentPalette() { static const Palette* p = &darkPalette(); return p; }
inline const Palette& pal() { return *currentPalette(); }

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

// Shared drawing helpers
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (pal().panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (pal().border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (pal().textDim);
        g.setFont (font (11.0f, true));
        g.drawText (title.toUpperCase(), r.withHeight (22.0f).withTrimmedLeft (12.0f), juce::Justification::centredLeft);
    }
}

inline juce::String dbText (float v, int decimals = 1, float floorDb = -119.0f)
{
    return v <= floorDb ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (v, decimals);
}

inline juce::String signedText (float v, int decimals = 1)
{
    return (v >= 0.0f ? "+" : "") + juce::String (v, decimals);
}
} // namespace bm::ui
