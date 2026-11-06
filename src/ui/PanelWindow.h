#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/PanelLayout.h"

namespace sls
{
// A floating panel inside the main window (like FL Studio's Channel Rack / Piano roll / Mixer):
// drag it by the title bar, resize it from the edges, close it with the x, double-click the title to
// maximise/restore. Panels may overlap; clicking anywhere on one brings it to the front.
class PanelWindow : public juce::Component
{
public:
    explicit PanelWindow (juce::String panelTitle);
    ~PanelWindow() override;

    // The content is not owned; it must outlive the panel or be removed first.
    void setContent (juce::Component* newContent);
    void setMinimumSize (int w, int h);
    void setTitle (const juce::String& newTitle);

    PanelLayout currentLayout() const;
    void applyLayout (const PanelLayout& layout); // clamps into the parent; does not notify

    void clampToParent();
    void setOpen (bool shouldBeOpen);
    bool isOpen() const { return isVisible(); }
    void setActive (bool shouldBeActive);
    bool isActive() const noexcept { return active; }
    void toggleMaximised();

    void activate(); // bring to front and report it through onActivated

    std::function<void (PanelWindow&)> onActivated;     // the panel was clicked / opened
    std::function<void (PanelWindow&)> onLayoutChanged; // moved, resized, maximised, closed

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kBorder = 4;
    static constexpr int kTitleHeight = 24;

private:
    class Constrainer : public juce::ComponentBoundsConstrainer
    {
    public:
        void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                          const juce::Rectangle<int>& limits, bool stretchTop, bool stretchLeft,
                          bool stretchBottom, bool stretchRight) override;
        void resizeStart() override { if (onStart) onStart(); }
        void resizeEnd() override { if (onEnd) onEnd(); }

        std::function<void()> onStart, onEnd;
    };

    // Lets a click anywhere inside the content raise the panel.
    class RaiseOnClick : public juce::MouseListener
    {
    public:
        explicit RaiseOnClick (PanelWindow& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent&) override { owner.activate(); }

    private:
        PanelWindow& owner;
    };

    juce::Rectangle<int> titleBar() const;
    void notifyLayoutChanged();

    juce::String title;
    juce::Component* content = nullptr;

    bool active = false;
    bool dragging = false;
    bool maximised = false;
    juce::Rectangle<int> restoreBounds;
    juce::Point<int> dragStartScreen;
    juce::Point<int> dragStartPosition;

    int minWidth = 200;
    int minHeight = 120;

    juce::TextButton closeButton;
    RaiseOnClick raiseOnClick { *this };
    Constrainer constrainer;                    // must be declared before the resizer
    juce::ResizableBorderComponent resizer { this, &constrainer };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelWindow)
};

// The area panels float in. Keeps them inside it when the window is resized and tracks the active one.
class Workspace : public juce::Component
{
public:
    void addPanel (PanelWindow& panel);
    void activate (PanelWindow* panel); // nullptr: no active panel

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void giveFocusToParentIfUnfocused();

    std::vector<PanelWindow*> panels;
    PanelWindow* activePanel = nullptr;
};
} // namespace sls
