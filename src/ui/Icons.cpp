#include "ui/Icons.h"

namespace sls
{
const char* iconName (int icon) noexcept
{
    static const char* const names[] = { "None", "Kick", "Snare", "Hi-hat", "Drums", "Piano", "Synth", "Bass", "Guitar", "Strings",
                                         "Brass", "Vocal", "FX", "Audio", "Pad", "Bell", "Star" };
    return icon >= 0 && icon <= kIconCount ? names[icon] : names[0];
}

void drawIcon (juce::Graphics& g, int icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (icon <= 0 || icon > kIconCount || area.isEmpty())
        return;
    // drawn in a 24 x 24 box, scaled into the middle of `area`
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size);
    const float k = size / 24.0f;
    const auto at = [&] (float x, float y) { return juce::Point<float> (box.getX() + x * k, box.getY() + y * k); };
    const float line = std::max (1.0f, 1.8f * k);
    juce::Path p;
    bool filled = false;
    switch (icon)
    {
        case 1: // kick: a drum seen from the front
            p.addEllipse (box.getX() + 3 * k, box.getY() + 3 * k, 18 * k, 18 * k);
            p.addEllipse (box.getX() + 9 * k, box.getY() + 9 * k, 6 * k, 6 * k);
            break;
        case 2: // snare: a short cylinder
            p.addEllipse (box.getX() + 3 * k, box.getY() + 6 * k, 18 * k, 5 * k);
            p.startNewSubPath (at (3, 8.5f));
            p.lineTo (at (3, 16));
            p.quadraticTo (at (12, 21), at (21, 16));
            p.lineTo (at (21, 8.5f));
            break;
        case 3: // hi-hat: two cymbals on a stand
            p.addEllipse (box.getX() + 2 * k, box.getY() + 5 * k, 20 * k, 4 * k);
            p.addEllipse (box.getX() + 2 * k, box.getY() + 9 * k, 20 * k, 4 * k);
            p.startNewSubPath (at (12, 13));
            p.lineTo (at (12, 22));
            break;
        case 4: // drums: two drums side by side
            p.addEllipse (box.getX() + 1 * k, box.getY() + 8 * k, 11 * k, 11 * k);
            p.addEllipse (box.getX() + 12 * k, box.getY() + 5 * k, 11 * k, 11 * k);
            p.startNewSubPath (at (4, 2));
            p.lineTo (at (10, 9));
            break;
        case 5: // piano: keys
            p.addRectangle (box.getX() + 2 * k, box.getY() + 5 * k, 20 * k, 14 * k);
            for (const float x : { 7.0f, 12.0f, 17.0f })
            {
                p.startNewSubPath (at (x, 5));
                p.lineTo (at (x, 19));
            }
            for (const float x : { 5.5f, 10.5f, 15.5f })
                p.addRectangle (box.getX() + x * k, box.getY() + 5 * k, 3 * k, 8 * k);
            break;
        case 6: // synth: a saw wave
            p.startNewSubPath (at (2, 17));
            p.lineTo (at (8, 7));
            p.lineTo (at (8, 17));
            p.lineTo (at (14, 7));
            p.lineTo (at (14, 17));
            p.lineTo (at (20, 7));
            p.lineTo (at (20, 17));
            break;
        case 7: // bass: a slow, deep wave
            p.startNewSubPath (at (2, 12));
            p.cubicTo (at (6, 2), at (10, 2), at (12, 12));
            p.cubicTo (at (14, 22), at (18, 22), at (22, 12));
            break;
        case 8: // guitar: a body and a neck
            p.addEllipse (box.getX() + 2 * k, box.getY() + 11 * k, 11 * k, 11 * k);
            p.addEllipse (box.getX() + 5 * k, box.getY() + 7 * k, 7 * k, 7 * k);
            p.startNewSubPath (at (10, 14));
            p.lineTo (at (21, 3));
            break;
        case 9: // strings: a violin's outline and its bow
            p.startNewSubPath (at (9, 3));
            p.cubicTo (at (4, 6), at (9, 10), at (5, 14));
            p.cubicTo (at (3, 21), at (15, 21), at (13, 14));
            p.cubicTo (at (9, 10), at (14, 6), at (9, 3));
            p.startNewSubPath (at (17, 2));
            p.lineTo (at (21, 22));
            break;
        case 10: // brass: a horn's bell
            p.startNewSubPath (at (2, 10));
            p.lineTo (at (12, 10));
            p.quadraticTo (at (18, 10), at (22, 4));
            p.lineTo (at (22, 20));
            p.quadraticTo (at (18, 14), at (12, 14));
            p.lineTo (at (2, 14));
            p.closeSubPath();
            break;
        case 11: // vocal: a microphone
            p.addRoundedRectangle (box.getX() + 8 * k, box.getY() + 2 * k, 8 * k, 12 * k, 4 * k);
            p.startNewSubPath (at (5, 11));
            p.quadraticTo (at (5, 18), at (12, 18));
            p.quadraticTo (at (19, 18), at (19, 11));
            p.startNewSubPath (at (12, 18));
            p.lineTo (at (12, 22));
            break;
        case 12: // FX: a star burst
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 8.0f;
                const float r = i % 2 == 0 ? 10.0f : 5.0f;
                p.startNewSubPath (at (12, 12));
                p.lineTo (at (12 + r * std::sin (a), 12 - r * std::cos (a)));
            }
            break;
        case 13: // audio: a waveform
            for (int i = 0; i < 9; ++i)
            {
                const float h = 2.0f + 8.0f * std::abs (std::sin (static_cast<float> (i) * 1.3f));
                p.startNewSubPath (at (3.0f + 2.25f * static_cast<float> (i), 12 - h));
                p.lineTo (at (3.0f + 2.25f * static_cast<float> (i), 12 + h));
            }
            break;
        case 14: // pad: a cloud
            p.addEllipse (box.getX() + 2 * k, box.getY() + 10 * k, 8 * k, 8 * k);
            p.addEllipse (box.getX() + 7 * k, box.getY() + 5 * k, 10 * k, 10 * k);
            p.addEllipse (box.getX() + 14 * k, box.getY() + 9 * k, 8 * k, 8 * k);
            filled = true;
            break;
        case 15: // bell
            p.startNewSubPath (at (6, 17));
            p.quadraticTo (at (6, 5), at (12, 4));
            p.quadraticTo (at (18, 5), at (18, 17));
            p.lineTo (at (21, 19));
            p.lineTo (at (3, 19));
            p.closeSubPath();
            p.addEllipse (box.getX() + 10.5f * k, box.getY() + 19.5f * k, 3 * k, 3 * k);
            break;
        default: // star
            for (int i = 0; i < 10; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 10.0f;
                const float r = i % 2 == 0 ? 10.0f : 4.2f;
                const auto pt = at (12 + r * std::sin (a), 13 - r * std::cos (a));
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            p.closeSubPath();
            filled = true;
            break;
    }
    g.setColour (colour);
    if (filled)
        g.fillPath (p);
    else
        g.strokePath (p, juce::PathStrokeType (line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

namespace
{
// A menu item's picture: the glyph, in the menu's text colour.
class IconDrawable : public juce::Drawable
{
public:
    explicit IconDrawable (int iconToDraw) : icon (iconToDraw) { setBounds (0, 0, 16, 16); }
    void paint (juce::Graphics& g) override { drawIcon (g, icon, getLocalBounds().toFloat(), juce::Colour (0xffd8dce4)); }
    std::unique_ptr<juce::Drawable> createCopy() const override { return std::make_unique<IconDrawable> (icon); }
    juce::Rectangle<float> getDrawableBounds() const override { return { 0.0f, 0.0f, 16.0f, 16.0f }; }
    juce::Path getOutlineAsPath() const override { return {}; }
    bool replaceColour (juce::Colour, juce::Colour) override { return false; }

private:
    int icon = 0;
};
} // namespace

void addIconItems (juce::PopupMenu& menu, int firstId, int current)
{
    menu.addItem (firstId, "None", true, current <= 0);
    for (int i = 1; i <= kIconCount; ++i)
    {
        juce::PopupMenu::Item item (iconName (i));
        item.itemID = firstId + i;
        item.isTicked = current == i;
        item.image = std::make_unique<IconDrawable> (i);
        menu.addItem (std::move (item));
    }
}
} // namespace sls
