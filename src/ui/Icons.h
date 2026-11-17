#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// Icons for channels, playlist tracks and mixer tracks (0.72.0, as FL Studio's): small drawn glyphs, the same at every size.
// Icon 0 is "none"; 1 .. kIconCount are the glyphs below.
constexpr int kIconCount = 16;

const char* iconName (int icon) noexcept;
void drawIcon (juce::Graphics& g, int icon, juce::Rectangle<float> area, juce::Colour colour);

// The menu items of the icons (ids from `firstId`: firstId is "None", firstId + i is icon i), each with its glyph.
void addIconItems (juce::PopupMenu& menu, int firstId, int current);
} // namespace sls
