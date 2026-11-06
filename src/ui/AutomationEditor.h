#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Automation.h"

namespace sls
{
// The curve of one automation clip, drawn large: click on empty space to add a point, drag a point to move it (the time snaps
// to a 16th note unless Alt is held, the value is free), right click a point for the shape of the line that leaves it or to delete
// it (Delete does the same). The clip is not owned: the editor asks for it every time (it may be moved, undone or deleted).
class AutomationEditor : public juce::Component
{
public:
    AutomationEditor();

    std::function<AutomationClip*()> getClip;          // nullptr when the clip is gone
    std::function<AutomationRange()> getRange;
    std::function<juce::String()> getTitle;
    std::function<juce::String (float)> valueToText;   // "-3.0 dB", "40 %"...
    std::function<void()> beforeChange;                // the state is about to change (undo step)
    std::function<void()> onChanged;                   // points changed

    void refresh() { repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> plotArea() const;
    float tickToX (double tick, const AutomationClip& c) const;
    double xToTick (float x, const AutomationClip& c) const;
    float valueToY (float value, const AutomationRange& r) const;
    float yToValue (float y, const AutomationRange& r) const;
    int pointAt (juce::Point<float> p, const AutomationClip& c, const AutomationRange& r) const;
    void showPointMenu (int index);
    void deletePoint (int index);

    int selectedPoint = -1;
    int hoverPoint = -1;
    bool dragging = false;
    bool dragStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEditor)
};

// A small free-floating window around the editor. The owner keeps it and deletes it when `onClose` is called.
class AutomationEditorWindow : public juce::DocumentWindow
{
public:
    AutomationEditorWindow();
    ~AutomationEditorWindow() override { clearContentComponent(); }

    AutomationEditor& editor() noexcept { return content; }
    std::function<void()> onClose;

    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    AutomationEditor content;
};
} // namespace sls
