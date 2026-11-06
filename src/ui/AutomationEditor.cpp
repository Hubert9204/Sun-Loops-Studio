#include "ui/AutomationEditor.h"

#include "audio/TimeFormat.h"

namespace sls
{
namespace
{
constexpr int kMargin = 10;
constexpr int kAxisWidth = 56;
constexpr int kHeader = 24;
constexpr float kGrab = 8.0f;
const juce::Colour kCurve { 0xff9b6bd1 };
}

AutomationEditor::AutomationEditor()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
}

juce::Rectangle<int> AutomationEditor::plotArea() const
{
    return getLocalBounds().withTrimmedLeft (kAxisWidth).withTrimmedTop (kHeader).reduced (kMargin, kMargin);
}

float AutomationEditor::tickToX (double tick, const AutomationClip& c) const
{
    const auto a = plotArea();
    return static_cast<float> (a.getX() + tick / static_cast<double> (std::max (1, c.lengthTicks)) * a.getWidth());
}

double AutomationEditor::xToTick (float x, const AutomationClip& c) const
{
    const auto a = plotArea();
    return std::clamp (static_cast<double> (x - static_cast<float> (a.getX())) / std::max (1, a.getWidth()), 0.0, 1.0)
           * static_cast<double> (c.lengthTicks);
}

float AutomationEditor::valueToY (float value, const AutomationRange& r) const
{
    const auto a = plotArea();
    const float t = (value - r.min) / std::max (1.0e-9f, r.max - r.min);
    return static_cast<float> (a.getBottom()) - std::clamp (t, 0.0f, 1.0f) * static_cast<float> (a.getHeight());
}

float AutomationEditor::yToValue (float y, const AutomationRange& r) const
{
    const auto a = plotArea();
    const float t = (static_cast<float> (a.getBottom()) - y) / static_cast<float> (std::max (1, a.getHeight()));
    return r.min + std::clamp (t, 0.0f, 1.0f) * (r.max - r.min);
}

int AutomationEditor::pointAt (juce::Point<float> p, const AutomationClip& c, const AutomationRange& r) const
{
    int best = -1;
    float bestDistance = kGrab;
    for (int i = 0; i < static_cast<int> (c.points.size()); ++i)
    {
        const auto& q = c.points[static_cast<size_t> (i)];
        const float d = p.getDistanceFrom ({ tickToX (q.tick, c), valueToY (q.value, r) });
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void AutomationEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));
    auto* clip = getClip ? getClip() : nullptr;
    if (clip == nullptr)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("This automation clip no longer exists.", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto range = getRange ? getRange() : AutomationRange {};
    const auto area = plotArea();
    const auto text = [this] (float v) { return valueToText ? valueToText (v) : juce::String (v, 2); };

    g.setColour (juce::Colour (0xff22262e));
    g.fillRect (area);

    // Beat lines (a stronger one on every bar) and four value lines.
    const int beats = std::max (1, clip->lengthTicks / kTicksPerBeat);
    const int beatStep = beats > 64 ? 16 : (beats > 32 ? 4 : 1);
    for (int b = 0; b <= beats; b += beatStep)
    {
        const float x = tickToX (static_cast<double> (b) * kTicksPerBeat, *clip);
        g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.16f : 0.07f));
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
    }
    for (int i = 0; i <= 4; ++i)
    {
        const float v = range.min + (range.max - range.min) * static_cast<float> (i) / 4.0f;
        const float y = valueToY (v, range);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (static_cast<int> (y), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text (v), 2, static_cast<int> (y) - 8, kAxisWidth - 6, 16, juce::Justification::centredRight, false);
    }
    if (range.def >= range.min && range.def <= range.max)
    {
        g.setColour (juce::Colour (0xffe0b030).withAlpha (0.35f));
        g.drawHorizontalLine (static_cast<int> (valueToY (range.def, range)), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
    }

    // The curve, one segment per pixel column.
    juce::Path path;
    for (int x = area.getX(); x <= area.getRight(); ++x)
    {
        const double tick = xToTick (static_cast<float> (x), *clip);
        const float y = valueToY (automationValueAt (clip->points, tick, range.def), range);
        if (x == area.getX())
            path.startNewSubPath (static_cast<float> (x), y);
        else
            path.lineTo (static_cast<float> (x), y);
    }
    g.setColour (kCurve);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    for (int i = 0; i < static_cast<int> (clip->points.size()); ++i)
    {
        const auto& p = clip->points[static_cast<size_t> (i)];
        const juce::Point<float> c (tickToX (p.tick, *clip), valueToY (p.value, range));
        const bool hot = i == selectedPoint || i == hoverPoint;
        g.setColour (hot ? juce::Colours::white : kCurve.brighter (0.4f));
        g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f, 1.0f);
    }

    // Title and the point under the cursor / selected.
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (getTitle ? getTitle() : juce::String ("Automation"), kMargin, 2, getWidth() * 2 / 5, kHeader - 4, juce::Justification::centredLeft, true);
    const int shown = dragging || hoverPoint < 0 ? selectedPoint : hoverPoint;
    juce::String readout = "Click: add   Drag: move (Alt: free time)   Right click: shape / delete";
    if (shown >= 0 && shown < static_cast<int> (clip->points.size()))
    {
        const auto& p = clip->points[static_cast<size_t> (shown)];
        const char* shapes[] = { "linear", "hold", "smooth" };
        readout = "Point " + juce::String (shown + 1) + "   " + juce::String (formatBarsBeatsTicks (std::llround (p.tick)))
                  + "   " + text (p.value) + "   line: " + shapes[std::min (2, static_cast<int> (p.shape))];
    }
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (readout, getWidth() * 2 / 5, 2, getWidth() * 3 / 5 - kMargin, kHeader - 4, juce::Justification::centredRight, true);
}

void AutomationEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto* clip = getClip ? getClip() : nullptr;
    if (clip == nullptr)
        return;
    const auto range = getRange ? getRange() : AutomationRange {};
    const int hit = pointAt (e.position, *clip, range);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            selectedPoint = hit;
            showPointMenu (hit);
        }
        return;
    }

    dragging = true;
    dragStarted = false;
    if (hit >= 0)
    {
        selectedPoint = hit;
    }
    else if (plotArea().expanded (4).contains (e.getPosition()))
    {
        if (beforeChange) beforeChange();
        clip = getClip();
        if (clip == nullptr) return;
        double tick = xToTick (e.position.x, *clip);
        if (! e.mods.isAltDown())
            tick = std::round (tick / kSixteenthTicks) * kSixteenthTicks;
        selectedPoint = insertAutomationPoint (*clip, tick, yToValue (e.position.y, range), range);
        dragStarted = true; // the undo step is already taken
        if (onChanged) onChanged();
    }
    repaint();
}

void AutomationEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || selectedPoint < 0 || e.mods.isPopupMenu())
        return;
    auto* clip = getClip ? getClip() : nullptr;
    if (clip == nullptr)
        return;
    if (! dragStarted)
    {
        if (e.getDistanceFromDragStart() < 3)
            return;
        if (beforeChange) beforeChange();
        dragStarted = true;
        clip = getClip();
        if (clip == nullptr) return;
    }
    const auto range = getRange ? getRange() : AutomationRange {};
    double tick = xToTick (e.position.x, *clip);
    if (! e.mods.isAltDown())
        tick = std::round (tick / kSixteenthTicks) * kSixteenthTicks;
    moveAutomationPoint (*clip, selectedPoint, tick, yToValue (e.position.y, range), range);
    if (onChanged) onChanged();
    repaint();
}

void AutomationEditor::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    dragStarted = false;
}

void AutomationEditor::mouseMove (const juce::MouseEvent& e)
{
    auto* clip = getClip ? getClip() : nullptr;
    const int hit = clip != nullptr ? pointAt (e.position, *clip, getRange ? getRange() : AutomationRange {}) : -1;
    if (hit != hoverPoint)
    {
        hoverPoint = hit;
        repaint();
    }
}

void AutomationEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverPoint != -1)
    {
        hoverPoint = -1;
        repaint();
    }
}

bool AutomationEditor::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedPoint >= 0)
    {
        deletePoint (selectedPoint);
        return true;
    }
    return false;
}

void AutomationEditor::deletePoint (int index)
{
    auto* clip = getClip ? getClip() : nullptr;
    if (clip == nullptr || clip->points.size() <= 1)
        return;
    if (beforeChange) beforeChange();
    clip = getClip();
    if (clip == nullptr) return;
    removeAutomationPoint (*clip, index);
    selectedPoint = -1;
    hoverPoint = -1;
    if (onChanged) onChanged();
    repaint();
}

void AutomationEditor::showPointMenu (int index)
{
    auto* clip = getClip ? getClip() : nullptr;
    if (clip == nullptr || index < 0 || index >= static_cast<int> (clip->points.size()))
        return;
    const auto current = clip->points[static_cast<size_t> (index)].shape;

    juce::PopupMenu menu;
    menu.addItem (1, "Line to next point: linear", true, current == AutomationShape::Linear);
    menu.addItem (2, "Line to next point: hold", true, current == AutomationShape::Hold);
    menu.addItem (3, "Line to next point: smooth", true, current == AutomationShape::Smooth);
    menu.addSeparator();
    menu.addItem (4, "Delete point", clip->points.size() > 1);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = SafePointer<AutomationEditor> (this), index] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result == 4)
        {
            safe->deletePoint (index);
            return;
        }
        if (safe->beforeChange) safe->beforeChange();
        auto* c = safe->getClip ? safe->getClip() : nullptr;
        if (c == nullptr || index >= static_cast<int> (c->points.size()))
            return;
        c->points[static_cast<size_t> (index)].shape = result == 2 ? AutomationShape::Hold
                                                      : (result == 3 ? AutomationShape::Smooth : AutomationShape::Linear);
        if (safe->onChanged) safe->onChanged();
        safe->repaint();
    });
}

AutomationEditorWindow::AutomationEditorWindow()
    : juce::DocumentWindow ("Automation", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, false);
    setResizable (true, true);
    setResizeLimits (360, 220, 1600, 900);
    centreWithSize (640, 340);
    setAlwaysOnTop (true);
}
} // namespace sls
